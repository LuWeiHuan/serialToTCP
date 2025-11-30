
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

#include <string.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

/*================== 本地数据类型   =========================================*/
typedef struct {
  ConnectState_t  state;
  uint64_t        startTimeMs;
  socket_t        socket;
  char            serverIP[46];    // 支持IPv6的最大长度
  uint16_t        serverPort;
  thread_t        thread;
  char            hsot[256];
  connectResultCallback Callback;
  void            *arg;
} connectServer_t;

/*================== 本地变量声明   =========================================*/
static connectServer_t client = {
  .state = CONNECT_STATE_FAILURE_DISCONNECTED,
  .startTimeMs = 0,
  .socket = INVALID_SOCKET_VALUE,
  .serverIP = {0},
  .thread = (thread_t)0,
  .hsot = {0},
  .Callback = NULL, 
  .arg = NULL, 
};

static mutex_type csClient;

/*================== 本地函数声明   =========================================*/
threadRet WINAPI ConnectServerThread(void * lpParam);

void ServerConnectInit(bool start)
{
  if( start )
    InitializeCriticalSection_Wrapper(&csClient);
  else
    DeleteCriticalSection_Wrapper(&csClient);
}

// 如果之前连结过服务器就断开之前的连接
static void DisconnectingServer(void)
{ 
  EnterCriticalSection_Wrapper(&csClient);

  bool ret = getClientIndex(&client.socket, NULL);
  const char* DisconnectServerInfo = getPrintf("断开之前连接的服务器，套接字：%s%s效", 
      ret? "存在":"没有", client.socket == INVALID_SOCKET_VALUE? "无":"有"); 
  if (ret == false || client.socket == INVALID_SOCKET_VALUE) {
    LeaveCriticalSection_Wrapper(&csClient);
    return;
  }

  printfSend(&client.socket, "Connect New Server, You are Disconnect!\n" ); 
  CloseClientSocket( &client.socket, DisconnectServerInfo);
  client.socket = INVALID_SOCKET_VALUE;
  LeaveCriticalSection_Wrapper(&csClient);
}

void ConnectToServer(const char* host, uint16_t port, 
          connectResultCallback ResultCallback, void *arg)
{ 
  EnterCriticalSection_Wrapper(&csClient);  

  // 防止重复调用连接服务器
  if (client.state == CONNECT_STATE_CONNECTING || client.thread ) {
      if( ResultCallback )
        ResultCallback(client.state, arg, client.hsot, client.serverPort, 
          CONNECT_TIMEOUT_MS - (getRuningTimeMs() - client.startTimeMs));
      LeaveCriticalSection_Wrapper(&csClient);
      return;
  }
  
  client.state = CONNECT_STATE_CONNECTING;  // 设置连接状态
  strcpy(client.hsot, host == NULL? "disconnect":host);
  client.serverPort = port;
  client.Callback = ResultCallback;
  client.arg = arg;

  // 启动连接其它服务器线程
  client.thread = threadCreate(NULL, ConnectServerThread, &client);
  if (client.thread == (thread_t)0) {
    SafePrintf("Failed to create Connect Server thread\n");
    client.state = CONNECT_STATE_FAILURE_DISCONNECTED;
    if( ResultCallback )
      ResultCallback(client.state, arg, client.hsot, client.serverPort, 0);
  }

  LeaveCriticalSection_Wrapper(&csClient);
}

threadRet WINAPI ConnectServerThread(void * lpParam)
{ 
  DisconnectingServer();  // 如果已经连接，先断开
  connectServer_t* clientInfo = (connectServer_t*)lpParam; 

  if( strnicmp(clientInfo->hsot, "disconnect", strlen("disconnect")) == 0 ||
     clientInfo->serverPort == 0 ){
    client.state = CONNECT_STATE_FAILURE_DISCONNECTED;
    clientInfo->thread = (thread_t)0;
    return (threadRet)0;
  }

  clientInfo->startTimeMs = getRuningTimeMs();
  bool ConnectRet = startConnectToServer(clientInfo->hsot, 
      clientInfo->serverPort, CONNECT_TIMEOUT_MS, 
      &clientInfo->socket, clientInfo->serverIP);

  if( ConnectRet ){  // 连接成功将连接交给clients.c管理
    ConnectRet = addNewClient(clientInfo->socket, clientInfo->serverIP);
    if (ConnectRet == false) {
      SafePrintf("Failed to add client to management\n");
      closeSocket(clientInfo->socket);
      clientInfo->socket = INVALID_SOCKET_VALUE; 
    }
  }

  EnterCriticalSection_Wrapper(&csClient); 
  clientInfo->state = ConnectRet? 
      CONNECT_STATE_CONNECTED : CONNECT_STATE_FAILURE_DISCONNECTED;
  
  if( clientInfo->Callback )
    clientInfo->Callback(clientInfo->state, clientInfo->arg, 
              clientInfo->hsot, clientInfo->serverPort, 0);
  
  LeaveCriticalSection_Wrapper(&csClient);
  clientInfo->thread = (thread_t)0;
  return (threadRet)0;
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