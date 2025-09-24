
/******************************************************************************
  * @file    文件 serverConnect.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 连接其他服务器
  * Win环境下用C语言编写一个TCP客户端连接远端服务端程序，使用独立线程完成接收数据，
  * 使用非阻塞接收，超时选定在1s，主循环用以做其他事情，编写一个函数，
  * 传递服务器IP端口号，如果之前已经就断开之前的连接，防止多进程调用造成频繁连接
  * 
  * 连接服务器      serverConnect,192.168.1.100,9000
  * 域名解析测试    serverConnect,google.com,80 
  * 断开连接        serverConnect,disconnect
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "ServerConnect.h"
#include "logPrint.h"
#include "public.h"
#include "clients.h"
#include "hostConnect.h"
#include "main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>

#include <ws2tcpip.h>  // 添加用于域名解析的头文件

/*================== 本地数据类型   =========================================*/
typedef struct {
  ConnectState_t  state;
  uint64_t        startTimeMs;
  SOCKET          socket;
  char            serverIP[46];    // 支持IPv6的最大长度
  uint16_t        serverPort;
  HANDLE          thread;
  char            hsot[256];
  connectResultCallback Callback;
  void            *arg;
}connectServer_t;

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量   =========================================*/
/*================== 本地常量声明   =========================================*/
/*================== 本地变量声明   =========================================*/

static connectServer_t client = {
  .state = CONNECT_STATE_FAILURE_DISCONNECTED,
  .startTimeMs = 0,
  .socket = INVALID_SOCKET,
  .serverIP = {0},
  .thread = NULL,
  .hsot = {0},
  .Callback = NULL, 
  .arg = NULL, 
};

static CRITICAL_SECTION csClient;

/*================== 本地函数声明   =========================================*/
static DWORD WINAPI ConnectServerThread(LPVOID lpParam);

/*================== 外部函数声明   =========================================*/
/*================== 外部变量声明   =========================================*/

void ServerConnectInit(bool start)
{
  if( start ) // 初始化临界区（在程序启动时调用）
    InitializeCriticalSection(&csClient);
  else        // 清理资源（在程序退出时调用）
    DeleteCriticalSection(&csClient);
}

// 如果之前连结过服务器就断开之前的连接
static void DisconnectingServer(void)
{ 
  EnterCriticalSection(&csClient);
  if (client.socket != INVALID_SOCKET) 
    printfSend(&client.socket, "Connect New Server, You are Disconnect!\n" );
  
  CloseClientSocket( client.socket, "断开之前连接的服务器");

  if (client.socket != INVALID_SOCKET) {
    closesocket(client.socket);
    client.socket = INVALID_SOCKET;
  }
  LeaveCriticalSection(&csClient);
}

/**
 * @brief 连接到服务器
 * @param host 主机名或IP地址，当主机名为 "disconnect" 或空 表示断开服务器连接
 * @param port 端口号          当端口号为 0 表示断开服务器连接
 * @param ResultCallback 连接结果通知回调
 * @param arg   连接结果通知回调 携带的参数
 * @return 无
 * @attention 
 */
void ConnectToServer(const char* host, uint16_t port, 
          connectResultCallback ResultCallback, void *arg)
{ 
  EnterCriticalSection(&csClient);  

  // 防止重复调用连接服务器
  if (client.state == CONNECT_STATE_CONNECTING || client.thread ) {
      if( ResultCallback )
        ResultCallback(client.state, arg, client.hsot, client.serverPort, 
          CONNECT_TIMEOUT_MS - (GetCurrentTimeMs() - client.startTimeMs));
      LeaveCriticalSection(&csClient);
      return;
  }

  client.state = CONNECT_STATE_CONNECTING;  // 设置连接状态
  strcpy(client.hsot, host);
  client.serverPort = port;
  client.Callback = ResultCallback;
  client.arg = arg;

  // 启动连接其它服务器线程
  client.thread = CreateThread(NULL, 0, ConnectServerThread, &client, 0, NULL);
  if (client.thread == NULL) {
    SafePrintf("Failed to create Connect Server thread\n");
    closesocket(client.socket);
    client.socket = INVALID_SOCKET;
    client.state = CONNECT_STATE_FAILURE_DISCONNECTED;
    if( ResultCallback )
      ResultCallback(client.state, arg, client.hsot, client.serverPort, 0);
  }

  LeaveCriticalSection(&csClient);
}

static DWORD WINAPI ConnectServerThread(LPVOID lpParam)
{ 
  DisconnectingServer();  // 如果已经连接，先断开
  connectServer_t* clientInfo = (connectServer_t*)lpParam; 

  if( clientInfo->hsot == NULL || clientInfo->serverPort == 0 ||
     strnicmp(clientInfo->hsot, "disconnect", strlen("disconnect")) == 0 ){
    client.state = CONNECT_STATE_FAILURE_DISCONNECTED;
    clientInfo->thread = NULL;
    return 0;
  }

  clientInfo->startTimeMs = GetCurrentTimeMs();
  bool ret = startConnectToServer(clientInfo->hsot, 
      clientInfo->serverPort, CONNECT_TIMEOUT_MS, 
      &clientInfo->socket, 
      clientInfo->serverIP);

 if( ret ){  // 连接成功将连接交给clients.c管理
    ret = addNewClient(clientInfo->socket, clientInfo->serverIP);
    if (ret == false) {
      SafePrintf("Failed to add client to management\n");
      closesocket(clientInfo->socket);
      clientInfo->socket = INVALID_SOCKET; 
    }
 }

  EnterCriticalSection(&csClient); 
  clientInfo->state = ret? 
      CONNECT_STATE_CONNECTED : CONNECT_STATE_FAILURE_DISCONNECTED;
  
  if( clientInfo->Callback )
    clientInfo->Callback(clientInfo->state, clientInfo->arg, 
              clientInfo->hsot, clientInfo->serverPort, 0);
  
  LeaveCriticalSection(&csClient);
  clientInfo->thread = NULL;
  return 0;
}



// 域名解析
bool ResolveDomainName(const char* domain, char* ipBuffer, uint8_t bufferSize)
{
  int getErr = 0;
  int8_t ret = resolveHostname(domain, ipBuffer, bufferSize, &getErr);
  if( ret == -1 )
    SafePrintf("Domain resolution failed: %s\n", gai_strerror(getErr));
  if( ret == -2 )
    SafePrintf("No valid IP address found for: %s, code:%d\n", domain, getErr);
  return ret==0? true:false;
}