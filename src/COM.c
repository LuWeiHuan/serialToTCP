/******************************************************************************
  * @file    文件 COM.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "COM.h"
#include "main.h"
#include "public.h"
#include "logPrint.h"
#include "client.h"
#include "TrafficStats.h"
#include "Queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <setupapi.h>
#include <devguid.h>

#include <windows.h> 
#include <initguid.h>
#include <tchar.h>

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
ComPortInfo_t comPort = { INVALID_HANDLE_VALUE, FALSE, "", {0}, NULL, 0, 0 };

/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static  CRITICAL_SECTION csComPort;
static AsyncSendQueue_t asyncSendQueue = {0};
static AsyncSendQueue_t asyncRecvQueue = {0};

/*================== 本地函数声明    ========================================*/
static DWORD WINAPI ComRecvDataThread(LPVOID lpParam);
static void ProcessReceivedData(char *data,  DWORD len);
static void get_COM_VID_PID_REV(const TCHAR* portName, char *, char *, char *);

/*================== 外部函数和变量声明    ==================================*/


void ComPortResourceInit(bool start) 
{
  if( start ){ 
    InitializeCriticalSection(&csComPort);
    // COM_UseAsyncRecv(100);
  }
  else{
    COM_UseAsyncRecv(0);
    CloseComPort("clear");
    DeleteCriticalSection(&csComPort);
  }
}

void CloseComPort(const char * reason) 
{
  if (comPort.isOpen == FALSE) 
    return;

  comPort.isOpen = FALSE;
  WINBOOL closeComRet = FALSE, closeThreadRet = FALSE ;

  EnterCriticalSection(&csComPort);
  if (comPort.hThread) {
    // 等待线程退出
    WaitForSingleObject(comPort.hThread, 1000);
    closeThreadRet = CloseHandle(comPort.hThread);
    comPort.hThread = NULL;
  }
  
  if (comPort.hCom != INVALID_HANDLE_VALUE) {
      closeComRet = CloseHandle(comPort.hCom);
      comPort.hCom = INVALID_HANDLE_VALUE;
  }

  printfSend(NULL, "closed %s %s, thread exit %s. reason: %s\n", comPort.portName, 
     closeComRet == FALSE? "failed":"success", 
     closeThreadRet == FALSE? "failed":"success",
      reason? reason:"unknown");
  memset(comPort.portName, 0, sizeof comPort.portName);
  
  LeaveCriticalSection(&csComPort);
}

void sendComPortsListToClient(SOCKET *socket, bool VPID)
{
  const char *comList = getComPortList(VPID);
  printfSend(socket, "%s\n", comList? comList: "Failed to get COM port list");
  
  if( comPort.isOpen && strstr(comList, comPort.portName) == NULL ){
    SafePrintf("%s disconnection!\n", comPort.portName);

    char reason[50];
    memset(reason, 0, sizeof reason);
    snprintf(reason, sizeof reason, "%s disconnection!", comPort.portName);
    SafePrintf("%s\n",reason);
    CloseComPort(reason);
  }
}

// 获取Win系统串口列表。
// VPID 是否需要PID、VID和REV信息，真需要，否不需要
const char *getComPortList(bool VPID) 
{
  EnterCriticalSection(&csComPort);
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) 
      return "COM Ports: GUID_DEVCLASS_PORTS NULL";
        
    bool exist = false;  
    static char response[2048];
    memset(response, 0, sizeof response);
    strcpy(response, VPID? "COM Ports:\n": "COM Ports: ");

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof deviceInfoData;
    BYTE buffer[256];

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); i++) { 
        memset(buffer, 0, sizeof buffer);
        DWORD dataType, bufferSize = sizeof buffer;

        WINBOOL ret = SetupDiGetDeviceRegistryPropertyA(hDevInfo, &deviceInfoData, 
            SPDRP_FRIENDLYNAME, &dataType, buffer, bufferSize, &bufferSize);
            
        if (ret == FALSE) 
            continue;
        
        // 更精确地查找COM端口号 - 查找最后一个括号内的COMX
        char* start = strrchr((char*)buffer, '(');
        if (start == NULL)
            continue;
            
        char* end = strchr(start, ')');
        if (end == NULL)
            continue;
        
        *end = '\0';// 临时截断字符串
        
        // 检查括号内的内容是否为COM端口
        char* portName = start + 1; // 跳过'('
        if (strncmp(portName, "COM", 3) != 0) {
            *end = ')'; // 如果不是COM端口，恢复字符串并跳过
            continue;
        }
        
        // 确认这是有效的COM端口号（后面跟着数字）
        if (strlen(portName) > 3 && isdigit(portName[3])) {
            if (exist) 
                strcat(response, VPID? ",\n":", ");
            
            static char retID[3][5], IDstring[50];
            if( VPID ){
              memset(retID, 0, sizeof retID);
              memset(IDstring, 0, sizeof IDstring);
              get_COM_VID_PID_REV(portName, retID[0], retID[1], retID[2]);
              snprintf(IDstring, sizeof IDstring, "%-6s [VID_%-4s PID_%-4s REV_%-4s]", 
                portName, retID[0], retID[1], retID[2]);
            }

            strcat(response, VPID? IDstring:portName);
            exist = true;
        }

        *end = ')';// 恢复原始字符串
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);

    if (exist == false)
        strcat(response, "No COM ports found");

    LeaveCriticalSection(&csComPort);
    return response;
}



int8_t OpenComPort(const char* portName, uint32_t baudRate, 
  uint8_t dataBits, uint8_t stopBits, uint8_t parity)
{
    char fullPortName[20];
    sprintf(fullPortName, "\\\\.\\%s", portName);

    EnterCriticalSection(&csComPort);
    
    comPort.hCom = CreateFileA(fullPortName, 
      GENERIC_READ | GENERIC_WRITE,
      0,
      NULL,
      OPEN_EXISTING,
      FILE_FLAG_OVERLAPPED, // FILE_FLAG_OVERLAPPED 异步模式，同步模式写0
      NULL);

    if (comPort.hCom == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&csComPort);
        return -1;
    }

    // 设置串口参数
    memset(&comPort.dcb, 0, sizeof comPort.dcb);
    comPort.dcb.DCBlength = sizeof comPort.dcb;
    if (!GetCommState(comPort.hCom, &comPort.dcb)) {
        CloseHandle(comPort.hCom);
        comPort.hCom = INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&csComPort);
        return -2;
    }

    // 确保 DCB 正确配置
    comPort.dcb.BaudRate = baudRate;          // 波特率（如 9600, 115200）
    comPort.dcb.ByteSize = (BYTE)dataBits;    // 数据位（5,6,7,8）
    comPort.dcb.StopBits = stopBits == 1 ? ONESTOPBIT : TWOSTOPBITS;  // 停止位（1 或 2）
    comPort.dcb.Parity = (BYTE)parity;        // 校验位（0=NONE, 1=ODD, 2=EVEN, 3=MARK, 4=SPACE）
    
    // 必须设置的标志位
    comPort.dcb.fBinary = TRUE;               // 必须为 TRUE（Windows 串口仅支持二进制模式）
    comPort.dcb.fOutxCtsFlow = FALSE;         // 禁用 CTS 流控
    comPort.dcb.fOutxDsrFlow = FALSE;         // 禁用 DSR 流控
    comPort.dcb.fDtrControl = DTR_CONTROL_ENABLE;  // DTR 信号控制
    comPort.dcb.fRtsControl = RTS_CONTROL_ENABLE;  // RTS 信号控制
    comPort.dcb.fOutX = FALSE;                // 禁用 XON/XOFF 输出流控
    comPort.dcb.fInX = FALSE;                 // 禁用 XON/XOFF 输入流控
    comPort.dcb.fErrorChar = FALSE;           // 禁用错误替换字符
    comPort.dcb.fNull = FALSE;                // 禁止丢弃 NULL 字节
    comPort.dcb.fAbortOnError = FALSE;        // 发生错误时不终止读写操作

    if (!SetCommState(comPort.hCom, &comPort.dcb)) {
        CloseHandle(comPort.hCom);
        comPort.hCom = INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&csComPort);
        return -3;
    }

    // 设置超时
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(comPort.hCom, &timeouts);

    memset(comPort.portName, 0, sizeof comPort.portName);
    strcpy(comPort.portName, portName);
    comPort.isOpen = TRUE;

    // 创建串口读取线程
    comPort.hThread = CreateThread(NULL, 0, ComRecvDataThread, NULL, 0, &comPort.threadId);
    if (comPort.hThread == NULL) {
        CloseHandle(comPort.hCom);
        comPort.hCom = INVALID_HANDLE_VALUE;
        comPort.isOpen = FALSE;
        LeaveCriticalSection(&csComPort);
        return -4;
    }

    LeaveCriticalSection(&csComPort);
    return 0;
}

// 处理串口收到的数据并发给客户端
static DWORD WINAPI ComRecvDataThread(LPVOID lpParam) 
{
  (void)lpParam;
  static char comRecvBuffer[RECV_BUFFER_SIZE];
  DWORD bytesRead = 0;
  OVERLAPPED overlapped = {0};
  overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
  WINBOOL readRet;
  DWORD lastUpdateTime = 0, currentTime = 0;
  const DWORD updateInterval = 1500; // 1.5秒更新一次线程状态
  comPort.sendCount = 0;
  
  while ( comPort.isOpen ) {
    if( bytesRead == 0 )
      Sleep(1); // 1ms也能使CPU占用降低
    CloseHandle(overlapped.hEvent);

    // 重置重叠结构
    memset(&overlapped, 0, sizeof overlapped);
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    
    // 发起异步读取
    readRet = ReadFile(comPort.hCom, comRecvBuffer, 
      (sizeof comRecvBuffer) - 1, &bytesRead, &overlapped);
    
    if (!readRet) {
        DWORD error = GetLastError();
        if (error == ERROR_IO_PENDING) {  // 等待读取完成或超时 
            DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 1000);
            if (waitResult == WAIT_TIMEOUT) { 
                updataConsoleTitle("COM: Timeout", GetCurrentThreadId()); 
                continue;
            }
            else if (waitResult == WAIT_OBJECT_0) { // 读取完成 
                if (!GetOverlappedResult(comPort.hCom, &overlapped, &bytesRead, FALSE)) {
                    error = GetLastError();
                    if (error != ERROR_OPERATION_ABORTED) {
                        char reason[50];
                        memset(reason, 0, sizeof reason);
                        snprintf(reason, sizeof reason, "%s read error %ld", 
                          comPort.portName, error);
                        CloseComPort(reason);
                        break;
                    }
                }
            }
        }
        else if (error != ERROR_OPERATION_ABORTED) { 
          char reason[50];
          memset(reason, 0, sizeof reason);
          snprintf(reason, sizeof reason, "%s read error %ld", comPort.portName, error);
          CloseComPort(reason);
          break;
        }
    }

    if (bytesRead == 0) { // 处理接收到的数据如果是空读取就重新读 
        currentTime = GetTickCount();   // 按间隔更新线程状态
        if (currentTime - lastUpdateTime >= updateInterval) {
            updataConsoleTitle(comPort.portName, GetCurrentThreadId());
            lastUpdateTime = currentTime;
        }
        continue;
    }

    // 处理串口接收到的数据
    if( asyncRecvQueue.running &&   // 使用异步队列处理收到的数据
        AddDataToAsyncQueue(&asyncRecvQueue, comRecvBuffer, bytesRead)) 
        continue;
    
    ProcessReceivedData(comRecvBuffer, bytesRead); 
  }

  CloseHandle(overlapped.hEvent); 
  return 0;
}

// 处理串口发过来的数据
static void ProcessReceivedData(char *comRecvBuffer, DWORD len)
{
  #ifdef __TRAFFIC_STATS_H_
  trafficStats.com.totalBytesReceived += len;
  #endif
  
  int sendRet = 0;
  if( getClientNum() ){  // 没有客户端不发送数据
    // 发送给指定客户端或所有客户端
    if (runInfo.monopolizeSocket != NULL) {
        sendRet = SendDataToClients(runInfo.monopolizeSocket, comRecvBuffer, len);
        
        // 如果发送失败，检查是否是独占客户端断开
        if (sendRet <= 0) {
          
            // 检查独占客户端是否还存在
            examineMonopolizeClient();
            
            // 尝试发送给所有客户端
            sendRet = SendDataToClients(NULL, comRecvBuffer, len);
        }
    } else {
        sendRet = SendDataToClients(NULL, comRecvBuffer, len);
    }
  }

  char *Direct = getSendRecvDirectionStr("[COM --> TCP]", 0);
  char *timeStr = getCurrentTime();
  timeStr[ strlen(timeStr) ] = ' '; 
  
  SafePrintf("%s%6I64d [%s]  %-6d/%-6ld Byte (%s : %ld)%s\n", 
      timeStr, ++comPort.sendCount, Direct, sendRet, len, 
      sendRet == (int)len ? "OK":"Fail", len - sendRet,
      runInfo.serverPrintData != 0? " data:":" ");
  
  if (runInfo.serverPrintData == 0) 
    return;

  if (runInfo.serverPrintData == 1)
      SafePrintf("%s", comRecvBuffer);
  if (runInfo.serverPrintData == 2)
      printf_hex8((uint8_t*)comRecvBuffer, len, 40, 2);
}


// 串口阻塞形发数据
static DWORD ComPortSendDataObstruct(char const *tcpRecvBuffer, int bytesReceived, DWORD *retError)
{
  DWORD bytesWritten = 0;
  OVERLAPPED writeOverlapped = {0};
  
  EnterCriticalSection(&csComPort);
  writeOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL); 
  WINBOOL WriteRet = WriteFile(comPort.hCom, tcpRecvBuffer, 
    bytesReceived, &bytesWritten, &writeOverlapped);

  // 处理异步写入
  DWORD error = 0;
  if (!WriteRet && (error = GetLastError()) == ERROR_IO_PENDING)  
      error = GetOverlappedResult(comPort.hCom, &writeOverlapped, 
        &bytesWritten, TRUE)? 0:GetLastError();

  CloseHandle(writeOverlapped.hEvent); 
  LeaveCriticalSection(&csComPort);
  
  if (error == ERROR_BAD_COMMAND || // 当串口拔掉后错误值是22
      error == ERROR_OPERATION_ABORTED || 
      error == ERROR_INVALID_HANDLE) {
      SafePrintf("Serial port error: %lu, closing port\n", error);
      CloseComPort("Serial port error");
  }
  
  if (retError) *retError = error;
  return bytesWritten;
}







DWORD ComPortSendData(char const *tcpRecvBuffer, int bytesReceived, DWORD *retError) {

  if (!comPort.isOpen) {  // 检查串口是否打开
      SafePrintf("COM not open, discarding data\n");
      return FALSE;
  }
  
  // 使用异步队列发送数据
  if( asyncSendQueue.running &&
      AddDataToAsyncQueue(&asyncSendQueue, tcpRecvBuffer, bytesReceived) ) {
      if (retError) // 返回成功添加，实际发送由线程处理
        *retError = 0;
      return bytesReceived;
  }
  
  return ComPortSendDataObstruct(tcpRecvBuffer, bytesReceived, retError);
}

static void COMAsyncSendQueueCallBack(queueData_t *data)
{
  updataConsoleTitle("COM Async Send", GetCurrentThreadId());

  if (comPort.isOpen == FALSE) 
    return;

  // 发送数据到串口
  DWORD error = 0;
  DWORD bytesWritten = ComPortSendDataObstruct(data->buff, data->size, &error);
  
  if (bytesWritten != data->size) 
    SafePrintf("COM Async send error: written %lu/%u bytes, error: %lu\n", 
              bytesWritten, data->size, error);
}

// 启用异步发送数据到COM口， 传入0代表关闭异步发送，大于10代表启动异步发送
BOOL COM_UseAsyncSend(uint16_t num)
{
  if( num < 10 ){
    FreeAsyncSendQueue(&asyncSendQueue);
    return num == 0? true:false;
  }
    
  return startAsyncDataHandleThread(&asyncSendQueue, COMAsyncSendQueueCallBack, num);
}








 

static void COMAsyncRecvQueueCallBack(queueData_t *data)
{
  //updataConsoleTitle("COM Async recv", GetCurrentThreadId());
  ProcessReceivedData(data->buff, data->size); 
}

// 启用异步发送数据到COM口， 传入0代表关闭异步发送，大于10代表启动异步发送
BOOL COM_UseAsyncRecv(uint16_t num)
{
  if( num < 10 ){
    FreeAsyncSendQueue(&asyncRecvQueue);
    return num == 0? true:false;
  }
    
  return startAsyncDataHandleThread(&asyncRecvQueue, COMAsyncRecvQueueCallBack, num);
}









// 获取设备属性
static LPTSTR GetDeviceProperty(HDEVINFO hDevInfo, PSP_DEVINFO_DATA pDevInfoData, DWORD Property)
{
    static TCHAR buffer[1024];
    DWORD nSize = 0, dataType = 0;
    memset(buffer, 0, sizeof buffer);
    // 第一次调用获取所需缓冲区大小
    if (!SetupDiGetDeviceRegistryProperty(hDevInfo, pDevInfoData, Property, NULL, NULL, 0, &nSize))
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return NULL;
    
    // 检查是否需要缓冲区超出静态数组大小
    if (nSize > sizeof buffer)
        return NULL;

    // 第二次调用获取实际数据
    if (!SetupDiGetDeviceRegistryProperty(hDevInfo, pDevInfoData, 
      Property, &dataType, (PBYTE)buffer, sizeof buffer, NULL))
        return NULL;
    
    return buffer;
}

// 获取COM串口设备VID和PID，VID和PID长度大概在5个字符，可以给多一点
static void get_COM_VID_PID_REV(const TCHAR* portName, char *retVID, char *retPID, char *retREV)
{
  if( portName == NULL )
    return;

  // 获取所有端口设备信息
  HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
  if (hDevInfo == INVALID_HANDLE_VALUE) {
      SafePrintf("SetupDiGetClassDevs failed. Error: %ld\n", GetLastError());
      return;
  }

  SP_DEVINFO_DATA devInfoData;
  devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
  // 枚举所有端口设备 
  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
    // 获取设备友好名称，并 检查是否是指定的串口
    LPTSTR DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
    if (DeviceInfo == NULL || _tcsstr(DeviceInfo, portName) == NULL )
      continue;
    
    #if 0
    SafePrintf("Found port: %s\n", DeviceInfo);
    // 获取设备描述
    DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_DEVICEDESC);
    if (DeviceInfo != NULL) 
        SafePrintf("Device Description: %s\n", DeviceInfo); 

    // 获取制造商信息
    DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_MFG);
    if (DeviceInfo != NULL) 
        SafePrintf("Manufacturer: %s\n", DeviceInfo);

    // 获取硬件ID
    DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_HARDWAREID);
    if (DeviceInfo != NULL) 
        SafePrintf("Hardware ID: %s\n", DeviceInfo);
    SafePrintf("\n");

    for (uint8_t j = 0; j < SPDRP_MAXIMUM_PROPERTY; j++) { 
      DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, j);
      if (DeviceInfo != NULL) 
          SafePrintf("DeviceInfo 0x%02X: %s\n", j, DeviceInfo);
    }
    SafePrintf("\n");
    #endif

    // 获取硬件ID
    DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_HARDWAREID);
    if (DeviceInfo != NULL) {   // 从硬件ID中提取VID和PID 
        TCHAR* vidPos = _tcsstr(DeviceInfo, _T("VID_"));
        TCHAR* pidPos = _tcsstr(DeviceInfo, _T("PID_"));
        TCHAR* revPos = _tcsstr(DeviceInfo, _T("REV_"));
        if( retVID )
          _tcsncpy(retVID, vidPos? vidPos + 4 :"NULL", 4);
        if( retPID )
          _tcsncpy(retPID, pidPos? pidPos + 4 :"NULL", 4);
        if( retREV )
          _tcsncpy(retREV, revPos? revPos + 4 :"NULL", 4);
    }
    
    break;
  }

  if (GetLastError() != NO_ERROR && GetLastError() != ERROR_NO_MORE_ITEMS)
      SafePrintf("SetupDiEnumDeviceInfo failed. Error: %ld\n", GetLastError());

  SetupDiDestroyDeviceInfoList(hDevInfo);
}
