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
#include "traffic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <setupapi.h>
#include <devguid.h>

typedef struct {
    uint32_t size;     
    char buff[RECV_BUFFER_SIZE];     // 数据内容
} sendCom_t;

typedef struct {
    sendCom_t *queue;         // 队列数组
    int capacity;             // 队列容量
    int front;                // 队列头指针
    int rear;                 // 队列尾指针
    HANDLE hMutex;            // 队列互斥锁
    HANDLE hDataEvent;        // 数据可用事件
    HANDLE hSpaceEvent;       // 空间可用事件
    HANDLE hThread;           // 发送线程句柄
    volatile BOOL running;    // 线程运行标志 
} AsyncSendQueue_t;

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static  CRITICAL_SECTION csComPort;
ComPortInfo_t comPort = { INVALID_HANDLE_VALUE, FALSE, "", {0}, NULL, 0 };

/*================== 本地函数声明    ========================================*/
static DWORD WINAPI ComRecvDataThread(LPVOID lpParam);
static DWORD WINAPI AsyncSendThreadProc(LPVOID lpParam);
static BOOL AddDataToAsyncQueue(const char *data, uint32_t size);

/*================== 外部函数和变量声明    ==================================*/

void ComPortResourceInit(bool start) 
{
  if( start )
    InitializeCriticalSection(&csComPort);
  else{
    CloseComPort();
    DeleteCriticalSection(&csComPort);
  }
}

void CloseComPort(void) 
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

  printfSend(NULL, "closed %s %s, thread exit %s.\n", comPort.portName, 
     closeComRet == FALSE? "failed":"success", closeThreadRet == FALSE? "failed":"success");
  memset(comPort.portName, 0, sizeof comPort.portName);
  
  LeaveCriticalSection(&csComPort);
}


char *getComPortList(void) 
{
  HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
  if (hDevInfo == INVALID_HANDLE_VALUE) 
    return NULL;
    
  bool first = true;  
  static char response[1024];
  memset(response, 0, sizeof response);
  strcpy(response,  "COM Ports: ");

  SP_DEVINFO_DATA deviceInfoData;
  deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); i++) { 

      BYTE buffer[256];
      DWORD dataType, bufferSize = sizeof buffer;

      WINBOOL ret = SetupDiGetDeviceRegistryPropertyA(hDevInfo, &deviceInfoData, 
          SPDRP_FRIENDLYNAME, &dataType, buffer, bufferSize, &bufferSize);
      if ( ret == FALSE ) 
        continue;

      char* portName = strstr((char*)buffer, "COM");
      if (portName == NULL) 
        continue;

      char* end = strchr(portName, ')');
      if (end) *end = '\0';
      if (!first) 
          strcat(response, ", ");
      strcat(response, portName);
      first = false;
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);

  if (first)
    strcat(response, "No COM ports found");
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
                              FILE_FLAG_OVERLAPPED, // 异步模式，同步模式写0
                              NULL);

    if (comPort.hCom == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&csComPort);
        return -1;
    }

    // 设置串口参数
    memset(&comPort.dcb, 0, sizeof(DCB));
    comPort.dcb.DCBlength = sizeof(DCB);
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
    DWORD bytesRead;
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    WINBOOL readRet;
    DWORD lastUpdateTime = 0, currentTime = 0;
    const DWORD updateInterval = 1500; // 1.5秒更新一次
    uint64_t sendCount = 0;
    while ( comPort.isOpen ) {
        // 重置重叠结构
        memset(&overlapped, 0, sizeof(OVERLAPPED));
        overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        
        // 发起异步读取
        readRet = ReadFile(comPort.hCom, comRecvBuffer, 
          (sizeof comRecvBuffer) - 1, &bytesRead, &overlapped);
        
        if (!readRet) {
            DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {  // 等待读取完成或超时 
                DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 1000);
                if (waitResult == WAIT_TIMEOUT) {
                    // 超时处理 - 按间隔更新状态
                    updataConsoleTitle("COM: Timeout", GetCurrentThreadId());
                    CloseHandle(overlapped.hEvent);
                    continue;
                }
                else if (waitResult == WAIT_OBJECT_0) { // 读取完成 
                    if (!GetOverlappedResult(comPort.hCom, &overlapped, &bytesRead, FALSE)) {
                        error = GetLastError();
                        if (error != ERROR_OPERATION_ABORTED) {
                            printfSend(NULL, "%s error %ld\n", comPort.portName, error);
                            CloseComPort();
                            break;
                        }
                    }
                }
            }
            else if (error != ERROR_OPERATION_ABORTED) { 
              printfSend(NULL, "%s error %ld\n", comPort.portName, error);
              CloseComPort();
              break;
            }
        }

        // 处理接收到的数据如果是空读取就重新读
        if (bytesRead == 0) {
            CloseHandle(overlapped.hEvent);
            
            // 按间隔更新线程状态
            currentTime = GetTickCount();
            if (currentTime - lastUpdateTime >= updateInterval) {
                updataConsoleTitle( comPort.portName, GetCurrentThreadId());
                lastUpdateTime = currentTime;
            }
            Sleep(1); // 1ms也能使CPU占用很低
            continue;
        }

        trafficStats.com.totalBytesReceived += bytesRead; 
 
        // 从这里开始就是读到有效数据，处理接收到的数据
        int sendRet = SendDataToClients(runInfo.monopolizeSoclet, comRecvBuffer, bytesRead);
        // 指定客户端发送失败后，再发给其他所有客户端，并将指定的客户端设置为空，下次就是直接发给所有客户端
        if (sendRet <= 0) { 
          runInfo.monopolizeSoclet = NULL;
          SafePrintf("send monopolize clients failed ! code: %d\n", sendRet);
          sendRet = SendDataToClients(NULL, comRecvBuffer, bytesRead); 
        }
        
        // 打印日志
        char *Direct = getSendRecvDirectionStr("[COM --> TCP]", 0);
        char *timeStr = getCurrentTime();
        timeStr[ strlen(timeStr) ] = ' '; 
        
        SafePrintf("%s%6I64d [%s]  %-6d/%-6ld Byte (%s : %ld)%s\n", 
            timeStr, ++sendCount, Direct, sendRet, bytesRead, 
            sendRet == (int)bytesRead ? "OK":"Fail", bytesRead - sendRet,
            runInfo.serverPrintData != 0? " data:":" ");
            
        if (runInfo.serverPrintData != 0) { 
            if (runInfo.serverPrintData == 1)
                SafePrintf("%s", comRecvBuffer);
            if (runInfo.serverPrintData == 2)
                printf_hex8((uint8_t*)comRecvBuffer, bytesRead, 40, 2);
        }

        CloseHandle(overlapped.hEvent);
    }

    CloseHandle(overlapped.hEvent); 
    return 0;
}



static DWORD ComPortSendDataWait(char const *tcpRecvBuffer, int bytesReceived, DWORD *retError)
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
  
  // 错误处理，当串口拔掉后错误值是22
  if (error == ERROR_BAD_COMMAND || error == ERROR_OPERATION_ABORTED || error == ERROR_INVALID_HANDLE) {
      SafePrintf("Serial port error: %lu, closing port\n", error);
      CloseComPort();
  }
  
  if (retError) *retError = error;
  return bytesWritten;
}

// 修改后的串口发送函数
DWORD ComPortSendData(char const *tcpRecvBuffer, int bytesReceived, DWORD *retError) {
      // 检查串口是否打开
    if (!comPort.isOpen) {
        SafePrintf("COM not open, discarding data\n");
        return FALSE;
    }

    // 使用异步队列发送数据
    if ( AddDataToAsyncQueue(tcpRecvBuffer, bytesReceived) ) {
        if (retError) // 返回成功添加，实际发送由线程处理
          *retError = 0;
        return bytesReceived;
    }
    
    return ComPortSendDataWait(tcpRecvBuffer, bytesReceived, retError);
}


static AsyncSendQueue_t asyncSendQueue = {0};

// 初始化队列和启动线程
BOOL InitAsyncSendThread(int queueSize) {


    if (queueSize <= 10 || queueSize >= MAX_QUEUE_SIZE) {
        SafePrintf("Invalid queue size: %d\n", queueSize);
        return FALSE;
    }

    if( asyncSendQueue.running )
      FreeAsyncSendQueue();

    // 分配队列内存
    asyncSendQueue.queue = (sendCom_t*)malloc(queueSize * sizeof(sendCom_t));
    if (!asyncSendQueue.queue) {
        SafePrintf("Failed to allocate queue memory\n");
        return FALSE;
    }

    // 初始化队列属性
    asyncSendQueue.capacity = queueSize;
    asyncSendQueue.front = 0;
    asyncSendQueue.rear = 0;
    asyncSendQueue.running = TRUE;

    // 创建同步对象
    asyncSendQueue.hMutex = CreateMutex(NULL, FALSE, NULL);
    asyncSendQueue.hDataEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // 初始无数据
    asyncSendQueue.hSpaceEvent = CreateEvent(NULL, TRUE, TRUE, NULL); // 初始有空间
    
    // 创建发送线程
    asyncSendQueue.hThread = CreateThread(NULL, 0, AsyncSendThreadProc, NULL, 0, NULL);
    if (!asyncSendQueue.hThread) {
        SafePrintf("Failed to create async send thread\n");
        FreeAsyncSendQueue();
        return FALSE;
    }
    
    SafePrintf("Async send thread started with queue size: %d\n", queueSize);
 
    return TRUE;
}

// 释放队列资源
void FreeAsyncSendQueue(void) {
    // 设置停止标志
    asyncSendQueue.running = FALSE; 

    // 唤醒线程以便退出
    SetEvent(asyncSendQueue.hDataEvent);
    
    // 等待线程退出
    if (asyncSendQueue.hThread) {
        WaitForSingleObject(asyncSendQueue.hThread, 1000);
        CloseHandle(asyncSendQueue.hThread);
        asyncSendQueue.hThread = NULL;
    }
    
    // 关闭同步对象
    if (asyncSendQueue.hMutex) {
        CloseHandle(asyncSendQueue.hMutex);
        asyncSendQueue.hMutex = NULL;
    }
    if (asyncSendQueue.hDataEvent) {
        CloseHandle(asyncSendQueue.hDataEvent);
        asyncSendQueue.hDataEvent = NULL;
    }
    if (asyncSendQueue.hSpaceEvent) {
        CloseHandle(asyncSendQueue.hSpaceEvent);
        asyncSendQueue.hSpaceEvent = NULL;
    }
    
    // 释放队列内存
    if (asyncSendQueue.queue) {
        free(asyncSendQueue.queue);
        asyncSendQueue.queue = NULL;
    }
    
    asyncSendQueue.capacity = 0;
    SafePrintf("Async send queue freed\n");
}



// 异步发送线程主函数
static DWORD WINAPI AsyncSendThreadProc(LPVOID lpParam) {
  (void)lpParam; // 未使用参数

  SafePrintf("Async send thread started\n");
  
  while (asyncSendQueue.running) {
    // 等待数据可用或退出信号
    DWORD waitResult = WaitForSingleObject(asyncSendQueue.hDataEvent, INFINITE);
    
    // 检查是否退出
    if (!asyncSendQueue.running) 
      break;
    
    // 处理数据
    if (waitResult != WAIT_OBJECT_0) 
      continue;

    // 循环处理所有可用数据
    while (asyncSendQueue.running) {
      // 获取队列互斥锁
      WaitForSingleObject(asyncSendQueue.hMutex, INFINITE);
      
      // 检查队列是否为空
      if (asyncSendQueue.front == asyncSendQueue.rear) {
          ResetEvent(asyncSendQueue.hDataEvent);
          ReleaseMutex(asyncSendQueue.hMutex);
          break;
      }
      
      // 取出队列头的数据
      sendCom_t sendData = asyncSendQueue.queue[asyncSendQueue.front];
      asyncSendQueue.front = (asyncSendQueue.front + 1) % asyncSendQueue.capacity;
      
      // 如果有空间可用，设置空间事件
      if ((asyncSendQueue.rear + 1) % asyncSendQueue.capacity != asyncSendQueue.front)
          SetEvent(asyncSendQueue.hSpaceEvent);

      ReleaseMutex(asyncSendQueue.hMutex);
      updataConsoleTitle("COM Async Send", GetCurrentThreadId());

      // 发送数据到串口
      if (comPort.isOpen == FALSE) 
        continue;

      DWORD error = 0;
      DWORD bytesWritten = ComPortSendDataWait(sendData.buff, sendData.size, &error);
      
      if (bytesWritten != sendData.size) 
          SafePrintf("COM Async send error: written %lu/%u bytes, error: %lu\n", 
                    bytesWritten, sendData.size, error);
    }
  }
  
  SafePrintf("Async send thread exiting\n");
  return 0;
}


// 添加数据到发送队列
static BOOL AddDataToAsyncQueue(const char *data, uint32_t size) {

    if( asyncSendQueue.running == FALSE )
      return FALSE;
 
    // 检查数据大小
    if (size > RECV_BUFFER_SIZE) {
        SafePrintf("Data too large (%u > %d), discarding\n", size, RECV_BUFFER_SIZE);
        return FALSE;
    }
    
    // 等待队列空间可用
    DWORD waitResult = WaitForSingleObject(asyncSendQueue.hSpaceEvent, 100); // 100ms超时
    if (waitResult != WAIT_OBJECT_0) {
        SafePrintf("Async queue full, discarding data\n");
        return FALSE;
    }
    
    // 获取队列互斥锁
    WaitForSingleObject(asyncSendQueue.hMutex, INFINITE);
    
    // 检查队列是否已满
    int nextRear = (asyncSendQueue.rear + 1) % asyncSendQueue.capacity;
    if (nextRear == asyncSendQueue.front) {
        ReleaseMutex(asyncSendQueue.hMutex);
        SafePrintf("Queue full after space event, discarding data\n");
        return FALSE;
    }
    
    // 添加数据到队列
    asyncSendQueue.queue[asyncSendQueue.rear].size = size;
    memcpy(asyncSendQueue.queue[asyncSendQueue.rear].buff, data, size);
    asyncSendQueue.rear = nextRear;
    
    // 设置数据可用事件
    SetEvent(asyncSendQueue.hDataEvent);
    
    // 如果队列满，重置空间事件
    if ((asyncSendQueue.rear + 1) % asyncSendQueue.capacity == asyncSendQueue.front) {
        ResetEvent(asyncSendQueue.hSpaceEvent);
    }
    
    ReleaseMutex(asyncSendQueue.hMutex);
    return TRUE;
}

