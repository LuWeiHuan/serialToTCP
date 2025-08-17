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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <winsock2.h>
#include <windows.h>
#include <tchar.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#include <ws2tcpip.h>
#include <inttypes.h>
#include <time.h>


/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
  CRITICAL_SECTION csComPort;
ComPortInfo_t comPort = { INVALID_HANDLE_VALUE, FALSE, "", {0}, NULL, 0 };

/*================== 本地函数声明    ========================================*/
static DWORD WINAPI ComRecvDataThread(LPVOID lpParam);

/*================== 外部函数和变量声明    ==================================*/

void ComPortResourceInit(bool start) 
{
  if( start )
    InitializeCriticalSection(&csComPort);
  else
    DeleteCriticalSection(&csComPort);
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

  printfSend(NULL, "%sclosed %s %s, thread exit %s.\n", CTRL_HEADER, comPort.portName, 
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
  static char response[1024] = "COM Ports: ";
  memset(response, 0, sizeof response);

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
      if (portName) {
          char* end = strchr(portName, ')');
          if (end) *end = '\0';
          if (!first) 
              strcat(response, ", ");
          strcat(response, portName);
          first = false;
      }
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
    if ( comPort.isOpen )
        CloseComPort();

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

    //strcpy(comPort.portName, portName, sizeof(comPort.portName) - 1);
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


static DWORD WINAPI ComRecvDataThread(LPVOID lpParam) {
    if(lpParam){}
    char comRecvBuffer[BUFFER_SIZE];
    DWORD bytesRead;
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    WINBOOL readRet;
    DWORD lastUpdateTime = 0, currentTime = 0;
    const DWORD updateInterval = 1500; // 1.5秒更新一次

    while ( comPort.isOpen ) {
        // 重置重叠结构
        memset(&overlapped, 0, sizeof(OVERLAPPED));
        overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        
        // 发起异步读取
        readRet = ReadFile(comPort.hCom, comRecvBuffer, (sizeof comRecvBuffer) - 1, &bytesRead, &overlapped);
        
        if (!readRet) {
            DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                // 等待读取完成或超时
                DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 1000);
                if (waitResult == WAIT_TIMEOUT) {
                    // 超时处理 - 按间隔更新状态
                    updataConsoleTitle("COM: Waiting", GetCurrentThreadId());
                    CloseHandle(overlapped.hEvent);
                    continue;
                }
                else if (waitResult == WAIT_OBJECT_0) {
                    // 读取完成
                    if (!GetOverlappedResult(comPort.hCom, &overlapped, &bytesRead, FALSE)) {
                        error = GetLastError();
                        if (error != ERROR_OPERATION_ABORTED) {
                            printfSend(NULL, "%s%s error %ld\n", CTRL_HEADER, comPort.portName, error);
                            CloseComPort();
                            break;
                        }
                    }
                }
            }
            else if (error != ERROR_OPERATION_ABORTED) { 
              printfSend(NULL, "%s%s error %ld\n", CTRL_HEADER, comPort.portName, error);
              CloseComPort();
              break;
            }
        }

        // 处理接收到的数据或空读取
        if (bytesRead == 0) {
            CloseHandle(overlapped.hEvent);
            
            // 按间隔更新状态
            currentTime = GetTickCount();
            if (currentTime - lastUpdateTime >= updateInterval) {
                updataConsoleTitle("COM", GetCurrentThreadId());
                lastUpdateTime = currentTime;
            }
            Sleep(1); // 5ms延迟可使CPU占用降至20%以下
            continue;
        }
 
        // 处理接收到的数据
        int sendRet = 0;
        if (runInfo.monopolizeSoclet != NULL) {
            // 发送给独立客户端
            sendRet = send(*runInfo.monopolizeSoclet, comRecvBuffer, bytesRead, 0);
            if (sendRet <= 0) {
                SafePrintf("send monopolize clients failed ! code: %d\n", sendRet);
                runInfo.monopolizeSoclet = NULL;
                sendRet = SendToClients(NULL, comRecvBuffer, bytesRead);
            }
        }
        else 
            sendRet = SendToClients(NULL, comRecvBuffer, bytesRead);
        

        // 打印日志
        char *Direct = getSendRecvDirectionStr("[COM --> TCP]", 0);
        char *timeStr = getCurrentTime();
        timeStr[strlen(timeStr)] = ' ';
        static uint64_t sendCount = 0;

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
