/******************************************************************************
  * @file    文件 clients.c 
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
#include "client.h"
#include "main.h"
#include "public.h"
#include "logPrint.h"
#include "COM.h"

#include <stdio.h>

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/
static CRITICAL_SECTION csClient;
ClientInfo_t clients[MAX_CLIENTS];

/*================== 外部函数和变量声明    ==================================*/
void HandleClientCommand( SOCKET clientSocket, uint8_t clientIndex, const char* command);
DWORD ComPortSendData(SOCKET Socket,  char const *tcpRecvBuffer, int bytesReceived, uint32_t *retError);

void ClientResourceInit(bool start) 
{
  if( start ){
    // 初始化客户端数组
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket = INVALID_SOCKET;
        clients[i].hThread = NULL;
    }
    InitializeCriticalSection(&csClient);
  }
  else{
    // 清理
    for (uint8_t i = 0; i < MAX_CLIENTS; i++)
      CloseClient( i, "清理");
    DeleteCriticalSection(&csClient);
  }
    
}

DWORD WINAPI ClientRecvDataThread(LPVOID lpParam) 
{
    if (lpParam == NULL) { 
        SafePrintf("client thread not Client info introduction\n");
        return -1;
    }

    ClientInfo_t *clientInfo = (ClientInfo_t*)lpParam;
    char tcpRecvBuffer[RECV_BUFFER_SIZE];
    int bytesReceived = 0, ret;
    fd_set readSet;
    struct timeval timeout;
    uint64_t sendCount = 0;
    

    // 设置socket为非阻塞模式
    u_long mode = 1; // 1表示非阻塞，0表示阻塞
    if (ioctlsocket(clientInfo->socket, FIONBIO, &mode) != 0) {
        SafePrintf("Set non-blocking failed for client %d, error: %d\n", 
               clientInfo->index, WSAGetLastError());
        CloseClient(clientInfo->index, "设置非阻塞失败");
        return -1;
    }

    char threadNameStr[20];

    // 发送连接成功消息  
    printfSend(&clientInfo->socket, "%sOK! your index %d\n", CTRL_HEADER, clientInfo->index);

    while (true) {
        // 检查客户端socket是否仍然有效
        if (clientInfo->socket == INVALID_SOCKET)
            break;

        FD_ZERO(&readSet);
        FD_SET(clientInfo->socket, &readSet);

        // 设置超时时间为100毫秒
        timeout.tv_sec = 1;
        timeout.tv_usec = 0; // 100毫秒

        ret = select(0, &readSet, NULL, NULL, &timeout);
        if (ret == SOCKET_ERROR) {
            SafePrintf("select error for client %d, error: %d\n",
                   clientInfo->index, WSAGetLastError());
            break;
        }
        else if (ret == 0) {
            // 超时，没有数据可读，继续循环
            memset(threadNameStr, 0, sizeof threadNameStr );
            sprintf(threadNameStr, "client %d ", clientInfo->index);
            updataConsoleTitle(threadNameStr, GetCurrentThreadId());
            continue;
        }

        // 有数据可读
        bytesReceived = recv(clientInfo->socket, tcpRecvBuffer, sizeof(tcpRecvBuffer) - 1, 0);
        
        if (bytesReceived == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                // 非阻塞模式下没有数据是正常情况
                continue;
            }
            else {
                // 其他错误，断开连接
                SafePrintf("recv error for client %d, error: %d\n", 
                       clientInfo->index, error);
                break;
            }
        }
        else if (bytesReceived == 0) {
            // 客户端正常关闭连接
            SafePrintf("client %d gracefully disconnected\n", clientInfo->index);
            break;
        }
        
        // 正常接收到数据
        tcpRecvBuffer[bytesReceived] = '\0';

        // 检查是否是控制命令
        if (strncmp(tcpRecvBuffer, CTRL_HEADER, strlen(CTRL_HEADER)) == 0) {
            HandleClientCommand(clientInfo->socket, clientInfo->index, 
                              tcpRecvBuffer + strlen(CTRL_HEADER));
            continue;
        }
        
        // 判断串口是否已经打开
        if (comPort.isOpen == FALSE) {
            printfSend(&clientInfo->socket, "%sCOM not open !\n", CTRL_HEADER);
            continue;
        }

        // 普通数据，发送到串口 
        DWORD bytesWritten, error = 0;
        extern CRITICAL_SECTION csComPort;
        EnterCriticalSection(&csComPort);

        OVERLAPPED writeOverlapped = {0};
        writeOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL); 
        WINBOOL WriteRet = WriteFile(comPort.hCom, tcpRecvBuffer, bytesReceived, &bytesWritten, &writeOverlapped);
        if (!WriteRet) {
            error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                // 等待写入完成
                if (!GetOverlappedResult(comPort.hCom, &writeOverlapped, &bytesWritten, TRUE)) {
                    // 这里还是真正的错误
                    error = GetLastError();
                    printfSend(&clientInfo->socket, "%sCOM write failed: %d\n", CTRL_HEADER, error);
                }
                else{
                  error = 0;
                  WriteRet = TRUE;
                }
            }
            else 
                printfSend(&clientInfo->socket, "%sCOM write error: %d\n", CTRL_HEADER, error);
        }
        CloseHandle(writeOverlapped.hEvent);
        
        LeaveCriticalSection(&csComPort);
        
        if( error == 22 ) // 设备可能已经拔出
          CloseComPort(); 
        
        char *Direct = getSendRecvDirectionStr("[TCP --> COM]", clientInfo->index);
        char *timeStr = getCurrentTime();
        timeStr[strlen(timeStr)] = ' ';
 
        SafePrintf("%s%6I64d [%s]  %-6ld/%-6d Byte (%s : %ld)%s\n",  timeStr, ++sendCount, Direct,
               bytesWritten, bytesReceived, WriteRet ? "OK" : "Fail", error,
               runInfo.serverPrintData != 0 ? " data:" : " ");
               
        if (runInfo.serverPrintData != 0) {
            if (runInfo.serverPrintData == 1) {
                SafePrintf("%s", tcpRecvBuffer);
            }
            if (runInfo.serverPrintData == 2) {
                printf_hex8((uint8_t*)tcpRecvBuffer, bytesReceived, 40, 2);
            }
        }
    }

    SafePrintf("client %d disconnected, last recv code: %d\n", 
           clientInfo->index, bytesReceived);
 
    CloseClient(clientInfo->index, "线程关闭");
    return 0;
}

void addNewClient(uint8_t index, SOCKET socket)
{       
  EnterCriticalSection(&csClient); 
  // 添加新客户端并记录精确连接时间
  clients[index].index = index;
  clients[index].socket = socket;
  clients[index].connectTime = GetCurrentTimeMillis();  // 记录精确到毫秒的连接时间
  clients[index].hThread = CreateThread(NULL, 0, ClientRecvDataThread, 
    &clients[index], 0, &clients[index].threadId);
  
  if( clients[index].hThread != NULL ){
    runInfo.clientCount++;
    runInfo.linkCount++;
    SafePrintf("The %d(index %d) Client connected at %I64d ms, Total clients: %d\n", 
      runInfo.clientCount, clients[index].index, clients[index].connectTime, runInfo.linkCount);
  }
  LeaveCriticalSection(&csClient);
}


void CloseClient(uint8_t index, char *reason) 
{
    if (index >= MAX_CLIENTS || clients[index].socket == INVALID_SOCKET) {
        SafePrintf("Invalid client index %d or socket already closed\n", index);
        return;
    }

    EnterCriticalSection(&csClient); 

    // 关闭套接字
    closesocket(clients[index].socket);
    clients[index].socket = INVALID_SOCKET;

    // 关闭线程
    if (clients[index].hThread) {
        WaitForSingleObject(clients[index].hThread, 1000);
        CloseHandle(clients[index].hThread);
        clients[index].hThread = NULL;
    }

    if (runInfo.clientCount > 0)
        runInfo.clientCount--;

    SafePrintf("Closed client %d, reason: %s\n", index, reason);
    LeaveCriticalSection(&csClient);
}


// Socket 如果为就会发送给所有客户端，不为空且有效的话就会只发送给指定的客户端
int SendToClients(SOCKET *socket, const char* buff, int len) 
{
    int ret = 0;
    EnterCriticalSection(&csClient);

    if( socket != NULL && *socket != INVALID_SOCKET )
      ret = send(*socket, buff, len, 0);
    else  // 无效的 套接字会发给所有客户端
      for (int i = 0; i < MAX_CLIENTS; i++)
          if (clients[i].socket != INVALID_SOCKET) 
            ret = send(clients[i].socket, buff, len, 0);
    
    LeaveCriticalSection(&csClient);
    return ret;
}

void sendListComPorts( SOCKET *socket)
{ 
  //SafePrintf("获取串口列表\n");
  char *ComList = getComPortList();
  printfSend(socket, "%s%s\n", CTRL_HEADER, 
    ComList == NULL? "Failed to get COM port list":ComList); 
}

/**
 * @brief  套接字发送字符串，使用类似于printf函数
 * @param 
 *		@arg Socket：指定发给客户端套接字指针，如果为孔就不指定客户端发送给所有客户端
 *		@arg fmt: printf 格式
 * @retval 
 */
int printfSend(SOCKET *Socket, const char *fmt, ...)
{
  //EnterCriticalSection(&g_log_cs);

	static char char_buff[1024]; // 字符串缓冲区
	memset(char_buff, 0, sizeof char_buff);
 
  // args为定义的一个指向可变参数的变量，va_list以及下边要用到的
  // va_start,va_end都是是在定义可变参数函数中必须要用到宏，在stdarg.h头文件中定义
	va_list args; 
  va_start(args, fmt);
  int retLen = vsnprintf(char_buff, sizeof char_buff, fmt, args);
  va_end(args); // 初始化args的函数，使其指向可变参数的第一个参数，fmt是可变参数的前一个参数

  //LeaveCriticalSection(&g_log_cs);
 
  return SendToClients(Socket, char_buff, retLen); 
}

// 查找可用的客户端槽位
int8_t findClientSlot(void)
{
    // 寻找新空位
    for (int i = 0; i < MAX_CLIENTS; i++) 
        if (clients[i].socket == INVALID_SOCKET) 
            return i;

    // 没有空位就找到最早连接的客户端
    uint8_t oldestIndex = 0;
    for (uint8_t i = 1; i < MAX_CLIENTS; i++) 
        if (clients[i].connectTime < clients[oldestIndex].connectTime) 
            oldestIndex = i;

    // 通知告诉最早客户端下线
    printfSend(&clients[oldestIndex].socket, 
        "%sYou are kicked due to server full! Your index %d\n", 
        CTRL_HEADER, oldestIndex);
    
    SafePrintf("Kicked oldest client index %d\n", oldestIndex);

    // 踢出最早的客户端
    CloseClient(oldestIndex, "Server full");
    
    return oldestIndex;
}
