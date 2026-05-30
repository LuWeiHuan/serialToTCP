/******************************************************************************
  * @file    文件 discovery.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 UDP服务发现功能
  ******************************************************************************
  * @attention 注意
  * 搜索服务在回复搜索请求的时候，就已经有本机IP地址了，
  * 如果本机提供的不对，可以使客户端可以直接用 socket 中获取IP即可。
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
// 必须在包含头文件之前定义 Windows 版本
#define _WIN32_WINNT 0x0600  // Windows Vista 或更高版本

#include "discovery.h"
#include "main.h"
#include "log.h"
#include "commonUtils.h"
#include "clients.h"
#include "Command.h"
#include "hostConnect.h"
#include "configSave.h"
#include "threadPool.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#endif

/*================== 本地数据类型   =========================================*/
#ifdef _WIN32
typedef int socklen_t;
#endif

/*================== 本地宏定义     =========================================*/
#define DISCOVERY_INTERVAL_MS  1000        // 发现请求检查间隔
#define RESPONSE_BUFFER_SIZE   256         // 响应缓冲区大小

/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static volatile bool discoveryRunning = false;
static thread_t hDiscoveryThread = (thread_t)0;
static socket_t discoverySocket = INVALID_SOCKET_VALUE;
static mutex_type csDiscovery;
static struct sockaddr_in newClientInfo;

static char *broadcastMsg = "Broadcast Test Message";
static bool broadcastTestOK = false;

/*================== 本地函数声明    ========================================*/
static void DiscoveryServiceStart(void);
static void DiscoveryServiceStop(void);
threadRet WINAPI DiscoveryThread(void*);
static bool InitializeDiscoverySocket(void);
static void SendDiscoveryResponse(struct sockaddr_in* clientAddr);

static void readyExamineBroadcastTask(void *arg);
static void startSendBroadcast(void *arg);


void DiscoveryService(bool start)
{
  if( start )
    DiscoveryServiceStart();
  else
    DiscoveryServiceStop();
}

// 测试广播是否能正常使用
void broadcastTestIsNormal(void)
{ 
  // 在 100 ms 后向广播发送一条测试消息
  static ThreadTask asyncSendBroadcast;
  #ifdef _WIN32
  threadTaskInit(&asyncSendBroadcast, startSendBroadcast, (void*)discoverySocket, 200, 0);
  #else
  threadTaskInit(&asyncSendBroadcast, startSendBroadcast, &discoverySocket, 200, 0);
  #endif
  threadTtaskStart(gThreadPool, &asyncSendBroadcast);

  static ThreadTask readyExamineBroadcast;
  threadTaskInit(&readyExamineBroadcast, readyExamineBroadcastTask, NULL, 1000, 0);
  threadTtaskStart(gThreadPool, &readyExamineBroadcast);

  SafePrintf("Broadcast Test Message...");
}

socket_t getDiscoverySocket(void)
{
  return discoverySocket;
}

const char *getDiscoveryNewClientIPAddr(void)
{
  static char IPaddr[50];
  memset(IPaddr, 0, sizeof IPaddr); 
  
#ifdef _WIN32
  // Windows 使用 inet_ntoa
  const char* result = inet_ntoa(newClientInfo.sin_addr);
  if (result != NULL) {
    strncpy(IPaddr, result, sizeof(IPaddr) - 1);
    return IPaddr;
  } 
  else {
    return "unknown IP";
  }
#else
  // Linux 使用 inet_ntop
  return inet_ntop(AF_INET, &newClientInfo.sin_addr, IPaddr, INET_ADDRSTRLEN) 
          == NULL ? "unknown IP" : IPaddr;
#endif
}

uint16_t getDiscoveryNewClientPort(void)
{ 
  return  ntohs(newClientInfo.sin_port);
}

// 启动发现服务
static void DiscoveryServiceStart(void)
{
  if (discoveryRunning) 
      return;
  
  InitializeCriticalSection_Wrapper(&csDiscovery);
  
  if (!InitializeDiscoverySocket()) {
    SafePrintf("Failed to initialize discovery socket\n");
    return;
  }

  hDiscoveryThread = threadCreate(NULL, DiscoveryThread, NULL);
  discoveryRunning = (hDiscoveryThread != (thread_t)0);
  
  if (hDiscoveryThread == (thread_t)0) {
    closeSocket(discoverySocket);
    discoverySocket = INVALID_SOCKET_VALUE; 
    SafePrintf("Failed to create discovery thread\n");
  }
}

 // 在Linux下必须需要断开连接以恢复广播能力，Win7也要
static inline void UdpDisconnect(socket_t sock) 
{
  struct sockaddr_in unconnectAddr;
  memset(&unconnectAddr, 0, sizeof(unconnectAddr));
  unconnectAddr.sin_family = AF_UNSPEC;
  int ret = connect(sock, (struct sockaddr*)&unconnectAddr, sizeof(unconnectAddr));
  if( ret == SOCKET_ERROR)
    SafePrintf("UDP Disconnect ERROR %ld, ret %d\n", GetLastError(), ret);
}

// 停止发现服务
static void DiscoveryServiceStop(void)
{
  if (!discoveryRunning)
    return;
  discoveryRunning = false;

  // 关闭套接字促使线程退出
  if (discoverySocket != INVALID_SOCKET_VALUE) {
    closeSocket(discoverySocket);
    discoverySocket = INVALID_SOCKET_VALUE;
  }
  
  if (hDiscoveryThread) {
    WaitForSingleObject_Wrapper(hDiscoveryThread, 1000);
    CloseHandle(hDiscoveryThread);
    hDiscoveryThread = (thread_t)0;
  }

  DeleteCriticalSection_Wrapper(&csDiscovery);
  SafePrintf("Discovery service stopped\n");
}

// 初始化发现Socket
static bool InitializeDiscoverySocket(void)
{
	int socketRet;
  uint8_t retryCount = 0;
  socket_t sock = INVALID_SOCKET_VALUE;
  
  while (sock == INVALID_SOCKET_VALUE && retryCount < 5) {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET_VALUE) {
      SafePrintf("Socket creation failed, retry %d\n", retryCount);
      Sleep(1);
      retryCount++;
      continue;
    }
    
    int broadcast = 1;
    socketRet = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof broadcast);
    if ( socketRet== SOCKET_ERROR) {
      SafePrintf("Set SO_BROADCAST failed: %ld\n", GetLastError());
      //closeSocket(discoverySocket);
      //discoverySocket = INVALID_SOCKET_VALUE;
      //return false;
    }
  
    int reuseAddr = 1;
    socketRet = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuseAddr, sizeof reuseAddr);
    if (socketRet == SOCKET_ERROR) 
      SafePrintf("Set SO_REUSEADDR failed: %ld\n", GetLastError());
    
#ifdef __linux // Linux下使用 SO_REUSEPORT 允许多个socket绑定同一端口
    int reusePort = 1;
    socketRet = setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reusePort, sizeof reusePort);
    if (socketRet == SOCKET_ERROR) 
      SafePrintf("Set SO_REUSEPORT failed: %ld\n", GetLastError());
#endif
    
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(DISCOVERY_PORT);
    
    if (bind(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == 0) {
      break;// bind成功，保持这个socket
    }
    else {
      int err = errno;
      SafePrintf("Bind failed: %s (errno=%d), retry %d\n", strerror(err), err, retryCount);
      closeSocket(sock);
      sock = INVALID_SOCKET_VALUE;
      Sleep(2);
      retryCount++;
    }
  }
  
  if (sock == INVALID_SOCKET_VALUE) 
    return false;
  
  // 设置非阻塞
#ifdef _WIN32
  u_long nonBlocking = 1;
  ioctlsocket(sock, FIONBIO, &nonBlocking);
#else
  int flags = fcntl(sock, F_GETFL, 0);
  fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
  
  discoverySocket = sock;
  return true;
}

static void startSendBroadcast(void *arg)
{
  //ThreadTask *task = ((ThreadPoolArgWrapper*)arg)->threadTask;
  #ifdef _WIN32
  socket_t sock = (socket_t)((ThreadPoolArgWrapper*)arg)->arg;
  #else
  socket_t sock = *((socket_t*)(((ThreadPoolArgWrapper*)arg)->arg));
  #endif

  struct sockaddr_in testAddr;
  memset(&testAddr, 0, sizeof(testAddr));
  testAddr.sin_family = AF_INET;
  testAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  testAddr.sin_port = htons(DISCOVERY_PORT);

  // 发送测试广播包
  int sendRet = sendto(sock, broadcastMsg, strlen(broadcastMsg), 0,
                        (struct sockaddr*)&testAddr, sizeof(testAddr));

  if (sendRet == SOCKET_ERROR) {
      int err = GetLastError();
      logPrint("Warning: Broadcast test failed (err=%d), may cause issues\n", err);
      SafePrintf("Warning: Broadcast test failed (err=%d), may cause issues\n", err);
  }
}

// 检测是否收到广播包
static void readyExamineBroadcastTask(void *arg)
{
  (void)arg;
  if( broadcastTestOK )
    return;
 
  SafePrintf("Examine Broadcast Test Message %s!System Uptime:%"PRIu64" s\n", 
      broadcastTestOK?"OK":"FAIL", getSystemUptimeSeconds());
  int err = GetLastError();
  SafePrintf("Broadcast test failed, discovery may not work properly!!!!\n");
  logPrint("Warning: Broadcast test failed (err=%d), may cause issues!!!!\n", err);
  Sleep(2000);

  // 系统开机5分钟内的广播测试失败则直接退出程序
  // 有的Linux开机启动这个程序，网络可能还没有准备好，广播功能会失效，建议重启
  if( getSystemUptimeSeconds() < 3000) 
    exit(1);  // 搜索服务不能正常工作直接退出程序让服务保活重启
}

// 发现服务线程
threadRet WINAPI DiscoveryThread(void* lpParam)
{
  (void)lpParam; 
  socklen_t clientAddrLen = sizeof newClientInfo;
  char recvBuffer[256];
  int bytesReceived, selectResult;
  fd_set readSet;
  struct timeval timeout; 
  
  char DiscoveryServerString[50];
  memset(DiscoveryServerString, 0, sizeof DiscoveryServerString);
  snprintf(DiscoveryServerString, sizeof DiscoveryServerString, 
    "Discovery PROT:%d", DISCOVERY_PORT);
  
  SafePrintf("Discovery Service On UDP Port: %d\n", DISCOVERY_PORT);
  
  while (discoveryRunning) {
    updataConsoleTitle(DiscoveryServerString);

    FD_ZERO(&readSet);
    FD_SET(discoverySocket, &readSet);

    timeout.tv_sec = 0;
    timeout.tv_usec = DISCOVERY_INTERVAL_MS * 1000; // 转换为微秒

    selectResult = select(discoverySocket + 1, &readSet, NULL, NULL, &timeout);
    if (selectResult == SOCKET_ERROR) {
        SafePrintf("Discovery select error: %ld\n", GetLastError());
        Sleep(DISCOVERY_INTERVAL_MS);
        continue;
    }

    if (selectResult == 0 || FD_ISSET(discoverySocket, &readSet) == 0)
      continue;

    // 接收发现请求
    bytesReceived = recvfrom(discoverySocket, recvBuffer, sizeof(recvBuffer) - 1, 0, 
                            (struct sockaddr*)&newClientInfo, &clientAddrLen);
    if (bytesReceived <= 0)
      continue;
    recvBuffer[bytesReceived] = '\0';

    #if 0
    SafePrintf("UDP [%s]:%d %s\n", inet_ntoa(newClientInfo.sin_addr), 
            ntohs(newClientInfo.sin_port), recvBuffer);
    #endif

    // 检查是否是有效的发现请求
    if (strnicmp(recvBuffer, "discover_com2tcp_server", strlen("discover_com2tcp_server")) == 0){
      SendDiscoveryResponse(&newClientInfo); // 发送响应
    }
    else if (strnicmp(recvBuffer, CONTROL_HEADER, strlen(CONTROL_HEADER)) == 0){
      if (saveInfo.serverPrintData == 3)
        SafePrintf("UDP [%s]:%d CMD: %-60s\n", inet_ntoa(newClientInfo.sin_addr), 
                ntohs(newClientInfo.sin_port), recvBuffer);
      
      // 连接UDP套接字到特定服务器，方便使用send发送数据
      int connectRet = connect(discoverySocket, (struct sockaddr*)&newClientInfo, sizeof newClientInfo);
      if( connectRet != 0 )
        SafePrintf("Discovery UDP connect Error! code :%d\n", connectRet);
      HandleClientCommand(&discoverySocket, recvBuffer + strlen(CONTROL_HEADER));
      UdpDisconnect(discoverySocket);   // 需要断开连接以恢复广播能力

      // 这里是进行程序异常退出捕获的测试位置，用于测试程序自我错误定位能力
      if( strnicmp(recvBuffer, CONTROL_HEADER"errorTest", strlen(CONTROL_HEADER"errorTest")) == 0 )
        ErrorCodeTest();
    }
    else if( strnicmp(recvBuffer, broadcastMsg, strlen(broadcastMsg)) == 0 ){
      static uint16_t count = 0;
      if( broadcastTestOK == false ){
        broadcastTestOK = true;
        SafePrintf("\rBroadcast Test Message...OK! (MsgLen:%d)\n", bytesReceived);
      }
      else
        SafePrintf("Don't Broadcast Test Message, Number:%-5d\r", ++count);
    }
  }
  
  SafePrintf("Discovery thread exiting\n");
  return (threadRet)0;
}



static void SendDiscoveryResponse(struct sockaddr_in* clientAddr)
{ 
  static uint16_t count = 0;
  EnterCriticalSection_Wrapper(&csDiscovery);
  
  bool getIPmethod = true;    // 获取IP的方法
  const char *getServerIP = "NULL IP";
  if( getIPmethod == true )   // 方法1：使用socket连接方式获取正确IP（更可靠）
    getServerIP = GetMatchingSubnetIP(clientAddr);
  else                        // 方法2：或者使用网段匹配算法
    getServerIP = SelectMatchingSubnetIP( inet_ntoa(clientAddr->sin_addr) );
  
  // 构建响应消息
  const char *ComputerFullName = getComputerFullName();
  char responseBuffer[RESPONSE_BUFFER_SIZE]; 
  snprintf(responseBuffer, sizeof responseBuffer, "%s|%-15s|%-15s|%d|%u|%u|%s|\n",
      DISCOVERY_MAGIC, ComputerFullName, getServerIP,
      getMainServerPort(), getClientNum(), getMaxClient(), GetSystemUniqueIdentifier());
  
  LeaveCriticalSection_Wrapper(&csDiscovery);

  // 发送响应到客户端
  int sendResult = sendto(discoverySocket, responseBuffer, strlen(responseBuffer), 
                0, (struct sockaddr*)clientAddr, sizeof *clientAddr);

  SafePrintf("Discovery response Sent to %s:%d -> Server IP: %s:%d  %s:%ld  count:%-5d%c",
            inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port),
            getServerIP, getMainServerPort(), 
            sendResult == SOCKET_ERROR? "failed":"succeed", 
            GetLastError(), ++count, is_running_as_service()? '\n':'\r');
}
