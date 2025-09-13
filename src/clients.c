/******************************************************************************
  * @file    文件 clients.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 连接的客户端
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
#include "TrafficStats.h"
#include "discovery.h"
#include "Command.h"

#include "serverListen.h"

#include <stdio.h>

/*================== 本地数据类型   =========================================*/
typedef struct {
    SOCKET socket;
    HANDLE hThread;
    DWORD threadId;
    uint8_t index;
    uint64_t connectTime;
    char ipAddress[16];
} ClientInfo_t;

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/
static CRITICAL_SECTION csClient;
static ClientInfo_t clients[MAX_CLIENTS];

/*================== 外部函数和变量声明    ==================================*/

const char *getClientIP(uint8_t index)
{
  return index < MAX_CLIENTS? clients[index].ipAddress:"NULL";
}

void ClientResourceInit(bool start) 
{
  if( start ){ // 初始化客户端数组
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket = INVALID_SOCKET;
        clients[i].hThread = NULL;
    }
    InitializeCriticalSection(&csClient);
  }
  else{ // 清理
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
    char *const tcpRecvBuffer = malloc( RECV_BUFFER_SIZE );
    int bytesReceived = 0, retSelect;
    fd_set readSet;
    struct timeval timeout;
    uint64_t sendCount = 0;
    char threadNameStr[20];

    // 设置socket为非阻塞模式
    u_long mode = 1; // 1表示非阻塞，0表示阻塞
    if (ioctlsocket(clientInfo->socket, FIONBIO, &mode) != 0) {
        SafePrintf("Set non-blocking failed for client %d, error: %d\n", 
               clientInfo->index, WSAGetLastError());
        CloseClient(clientInfo->index, "设置非阻塞失败");
      if( tcpRecvBuffer != NULL)
        free( tcpRecvBuffer );
      return -1;
    }

    // 发送连接成功消息  
    printfSend(&clientInfo->socket, "OK! your index %d\n", clientInfo->index);
    sendComPortsListToClient( &clientInfo->socket, true );  // 像该客户端发送可用端口号

    while ( tcpRecvBuffer != NULL ) {
        // 检查客户端socket是否仍然有效
        if (clientInfo->socket == INVALID_SOCKET)
            break;

        FD_ZERO(&readSet);
        FD_SET(clientInfo->socket, &readSet);

        // 设置超时时间为100毫秒
        timeout.tv_sec = 1;
        timeout.tv_usec = 0; // 100毫秒

        retSelect = select(0, &readSet, NULL, NULL, &timeout);
        if (retSelect == SOCKET_ERROR) {
            SafePrintf("select error for client %d, error: %d\n",
                   clientInfo->index, WSAGetLastError());
            break;
        }
        else if (retSelect == 0) {
            // 超时，没有数据可读，继续循环
            memset(threadNameStr, 0, sizeof threadNameStr );
            sprintf(threadNameStr, "client %d ", clientInfo->index);
            updataConsoleTitle(threadNameStr, GetCurrentThreadId());
            continue;
        }

        // 有数据可读
        bytesReceived = recv(clientInfo->socket, tcpRecvBuffer, RECV_BUFFER_SIZE - 1, 0);
        if (bytesReceived == SOCKET_ERROR) {
            int WSAerror = WSAGetLastError();
            if (WSAerror == WSAEWOULDBLOCK) { 
                continue; // 非阻塞模式下没有数据是正常情况
            }
            else {        // 其他错误，断开连接 
                SafePrintf("client %d recv error: %d\n", 
                       clientInfo->index, WSAerror);
                break;
            }
        }
        else if (bytesReceived == 0) { // 客户端正常关闭连接
            SafePrintf("client index:%d IP:%s gracefully disconnected\n", 
              clientInfo->index, clientInfo->ipAddress);
            break;
        }
        
        // 正常接收到数据
        tcpRecvBuffer[bytesReceived] = '\0';  // 防止命令解析异常

        // 检查是否是控制命令
        if (strncmp(tcpRecvBuffer, CTRL_HEADER, strlen(CTRL_HEADER)) == 0) {
            if (runInfo.serverPrintData == 3) 
              SafePrintf("%s\n", tcpRecvBuffer);
            HandleClientCommand(&clientInfo->socket, clientInfo->index, 
                              tcpRecvBuffer + strlen(CTRL_HEADER));
            continue;
        }
        
        // 判断串口是否已经打开
        if (comPort.isOpen == FALSE) {
          printfSend(&clientInfo->socket, "COM not open !\n");
          continue;
        }

        // 普通数据，发送到串口  
        DWORD getError = 0;
        DWORD bytesWritten = ComPortSendData(tcpRecvBuffer, bytesReceived, &getError);
        if( bytesWritten != (DWORD)bytesReceived )
          printfSend(&clientInfo->socket, "COM write error: %ld\n", getError);

        char *Direct = getSendRecvDirectionStr("[TCP --> COM]", clientInfo->index);
        char *timeStr = getCurrentTime();
        strcat(timeStr, " " );
 
        SafePrintf("%s%6I64d [%s]  %-6ld/%-6d Byte (%s : %ld)%s\n", timeStr, ++sendCount, Direct,
               bytesWritten, bytesReceived, bytesWritten == (DWORD)bytesReceived? "OK" : "Fail", getError,
               runInfo.serverPrintData != 0 ? " data:" : " ");
               
        if (runInfo.serverPrintData != 0) {
            if (runInfo.serverPrintData == 1) 
              SafePrintf("%s", tcpRecvBuffer);
              
            if (runInfo.serverPrintData == 2) 
              printf_hex8((uint8_t*)tcpRecvBuffer, bytesReceived, 40, 2);
        }

        // 收到客户端数据时（发送到串口）
        trafficStats.net.totalBytesReceived += bytesReceived;
        trafficStats.com.totalBytesSent += bytesWritten;
    }

    SafePrintf("Client index:%d IP:%s disconnected, last recv code: %d%s\n", 
      clientInfo->index, clientInfo->ipAddress, bytesReceived, 
      tcpRecvBuffer != NULL? ", free memory":" ");

    if( tcpRecvBuffer != NULL)
      free( tcpRecvBuffer );

    CloseClient(clientInfo->index, "客户端线程关闭");
    return 0;
}

void addNewClient(uint8_t index, SOCKET socket, const char *ip)
{       
  EnterCriticalSection(&csClient); 
  // 添加新客户端并记录精确连接时间
  clients[index].index = index;
  clients[index].socket = socket; 
  strcpy(clients[index].ipAddress, ip != NULL? ip : "Unknown");
  clients[index].connectTime = GetCurrentTimeMillis();  // 记录精确到毫秒的连接时间
  clients[index].hThread = CreateThread(NULL, 0, ClientRecvDataThread, 
    &clients[index], 0, &clients[index].threadId);
  
  if( clients[index].hThread != NULL ){
    runInfo.clientCount++;
    runInfo.connectCount++;
    SafePrintf("Client connected IP:%s, Count:%d, index %d, connectTime %I64d ms, Total clients: %d\n", 
      clients[index].ipAddress, runInfo.clientCount, clients[index].index, clients[index].connectTime, runInfo.connectCount);
  }
  LeaveCriticalSection(&csClient);
 
  UpdateDiscoveryInfo(g_server.port, runInfo.clientCount);
}


void getAllclientIPandIndexInfo(char *retStr, uint16_t len)
{
  uint16_t strLen = 0;
  memset(retStr, 0, len);
  char clientInfo[40];
  for (uint8_t i = 0; i < MAX_CLIENTS; i++)
    if (clients[i].socket != INVALID_SOCKET) {
      memset(clientInfo, 0, sizeof clientInfo);
      sprintf(clientInfo, "client index %d, IP:%s\n", i, clients[i].ipAddress );
      strLen += strlen(clientInfo);
      if( strLen > len  )
        break;
      strcat(retStr, clientInfo);
    }
}


void CloseClient(uint8_t index, const char *reason) 
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
    
    SafePrintf("Closed client %d, reason: %s\n", index, reason? reason:"NULL");
    memset(clients[index].ipAddress, 0, sizeof clients[index].ipAddress);
    LeaveCriticalSection(&csClient);

    UpdateDiscoveryInfo(g_server.port, runInfo.clientCount);// 更新发现信息 
}


// Socket 如果为就会发送给所有客户端，不为空且有效的话就会只发送给指定的客户端
int SendDataToClients(SOCKET *socket, const char* buff, int len) 
{
  int sendRet = 0;
  EnterCriticalSection(&csClient);

  if( socket != NULL && *socket != INVALID_SOCKET ){
    sendRet = send(*socket, buff, len, 0);
    trafficStats.net.totalBytesSent += sendRet;
  }
  // 无效的 套接字会发给所有客户端
  else for (uint8_t i = 0; i < MAX_CLIENTS; i++)
    if (clients[i].socket != INVALID_SOCKET) {
      sendRet = send(clients[i].socket, buff, len, 0); 
      trafficStats.net.totalBytesSent += sendRet; // 发送数据到客户端时 
    }

  LeaveCriticalSection(&csClient);
  return sendRet;
}

void sendComPortsListToClient(SOCKET *socket, bool VPID)
{
  const char *comList = getComPortList(VPID);
  printfSend(socket, "%s\n", comList? comList: "Failed to get COM port list");
  
  if( comPort.isOpen && strstr(comList, comPort.portName) == NULL ){
    SafePrintf("%s disconnection!\n", comPort.portName);
    printfSend(socket, "%s disconnection!\n", comPort.portName);
    CloseComPort();
  }
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
  strcpy(char_buff, CTRL_HEADER);
  uint8_t ctrlHeaderLen = strlen( CTRL_HEADER);

  // args为定义的一个指向可变参数的变量，va_list以及下边要用到的
  // va_start,va_end都是是在定义可变参数函数中必须要用到宏，在stdarg.h头文件中定义
	va_list args; 
  va_start(args, fmt);
  int retLen = vsnprintf(char_buff + ctrlHeaderLen, 
    sizeof char_buff - ctrlHeaderLen, fmt, args);
  va_end(args); // 初始化args的函数，使其指向可变参数的第一个参数，fmt是可变参数的前一个参数

  //LeaveCriticalSection(&g_log_cs);

  return SendDataToClients(Socket, char_buff, retLen + ctrlHeaderLen); 
}

// 查找可用的客户端槽位
uint8_t findClientSlot(void)
{
  uint8_t oldestIndex = 0;
  for (uint8_t i = 0; i < MAX_CLIENTS; i++) {

    // 寻找新空位
    if (clients[i].socket == INVALID_SOCKET)  
      return i;
    
    // 没有空位就找到最早连接的客户端
    if (clients[i].connectTime < clients[oldestIndex].connectTime) 
      oldestIndex = i;
  }

  // 通知告诉最早客户端下线
  printfSend(&clients[oldestIndex].socket, 
      "You are kicked due to server full! Your index %d\n", 
        oldestIndex);
  
  SafePrintf("Kicked oldest client index %d, IP:%s\n", 
    oldestIndex, clients[oldestIndex].ipAddress);

  // 踢出最早的客户端
  CloseClient(oldestIndex, "Server full");
  
  return oldestIndex;
}

