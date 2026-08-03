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
#include <iphlpapi.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

/*================== 本地数据类型   =========================================*/
typedef struct {
  bool testV4;
  bool testV6;
  socket_t socketV4;
  socket_t socketV6;
  struct sockaddr_in  addrV4; // 保留 IPv4
  struct sockaddr_in6 addrV6; // 保留 IPv6

  bool isIPv4;            // 新客户端IP版本标记
  thread_t thread;
  volatile bool run;
  mutex_type mt;
}ClientInfo_t;

typedef struct {
  const uint8_t IPvNum;
  socket_t *const socket;
  socklen_t socklen;
  struct sockaddr *const addr;
  ClientInfo_t *const newClient;
}discoveryServerInfo_t;

/*================== 本地宏定义     =========================================*/
#define DISCOVERY_INTERVAL_MS  200        // 发现请求检查间隔

/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static char *broadcastTestMsg = "Broadcast Test Message";

static ClientInfo_t newClientInfo = {
  .isIPv4 = false,
  .thread = (thread_t)0,
  .run = false,
  .testV4 = false,
  .testV6 = false,
  .socketV4 = INVALID_SOCKET_VALUE,
  .socketV6 = INVALID_SOCKET_VALUE,
};

static discoveryServerInfo_t IPv4DS = {
  .IPvNum = 4,
  .socket = &newClientInfo.socketV4,
  .addr = (struct sockaddr*)&newClientInfo.addrV4,
  .socklen = sizeof(struct sockaddr_in),
  .newClient = &newClientInfo
};

static discoveryServerInfo_t IPv6DS = {
  .IPvNum = 6,
  .socket = &newClientInfo.socketV6,
  .addr = (struct sockaddr*)&newClientInfo.addrV6,
  .socklen = sizeof(struct sockaddr_in6),
  .newClient = &newClientInfo
};

static discoveryServerInfo_t *IPDS = &IPv6DS;

/*================== 本地函数声明    ========================================*/
static void DiscoveryServiceStart(void);
static void DiscoveryServiceStop(void);
threadRet WINAPI DiscoveryThread(void*);
static socket_t createIPv4DiscoverySocket(void);
static socket_t createIPv6DiscoverySocket(void); 
static void sendIPv4BroadcastTestMessage(void *arg);
static void sendIPv6MulticastTestMessage(void *arg);
static int getIPv6InterfaceIndexes(uint32_t *outIfs, int maxCount);
static void sendDiscoveryResponse(socket_t socket, struct sockaddr *addr, bool isIPv4);
static const char* getSockAddrIn(const struct sockaddr *addr, bool addrIsSockAddrIn6,
                          bool isIPv4, bool autoFormat, bool IPv6AddScopeId);
static uint16_t getSockAddrPort(const struct sockaddr *addr, bool isIPv4);

void DiscoveryService(bool start)
{
  if( start )
    DiscoveryServiceStart();
  else
    DiscoveryServiceStop();
}

// 测试广播是否能正常使用
void DiscoveryServiceTestIsNormal(void)
{
  IPDS->newClient->testV4 = false;
  IPDS->newClient->testV6 = false;

  // 在 200 ms 后向广播组播发送一条测试消息
  static ThreadTask asyncSendBroadcast;
  threadTaskInit(&asyncSendBroadcast, sendIPv4BroadcastTestMessage, 
                  &IPDS->newClient->socketV4, 200, 0);
  threadTtaskStart(gThreadPool, &asyncSendBroadcast);

  static ThreadTask asyncSendMulticast;
  threadTaskInit(&asyncSendMulticast, sendIPv6MulticastTestMessage, 
                  &IPDS->newClient->socketV6, 210, 0);
  threadTtaskStart(gThreadPool, &asyncSendMulticast);
  SafePrintf("Broadcast Test Message...\r");
}

bool isDiscoveryServiceSocket(socket_t sock)
{
  return IPDS->newClient->socketV4 == sock || IPDS->newClient->socketV6 == sock;
}

int DiscoveryServiceSend(socket_t sock, const char *buf, int len)
{  
  if( sock == INVALID_SOCKET_VALUE )
    return -1;
  if( isDiscoveryServiceSocket( sock ) == false )
    return -2;

  struct sockaddr const * addr = NULL;
  socklen_t socklen;

  if( IPDS->newClient->socketV4 == sock ){
    socklen = sizeof(struct sockaddr_in);
    addr = (struct sockaddr*)&IPDS->newClient->addrV4;
  }
 
  if( IPDS->newClient->socketV6 == sock ){
    socklen = sizeof(struct sockaddr_in6);
    addr = (struct sockaddr*)&IPDS->newClient->addrV6;
  }
  
  if( addr == NULL )
    return -3;

  // SafePrintf("UDP Send %d Bytes To %s:%d\n", len, 
  //   getSockAddrIn(addr, true, IPDS->IPvNum == 4, true, true), 
  //   getSockAddrPort(addr, IPDS->newClient->isIPv4) );
  return sendto(sock, buf, len, 0, addr, socklen);
}

uint8_t getDiscoveryServiceNewClientIPvNum(void)
{
  return IPDS->newClient->isIPv4? 4:6;
}

// 获取客户端端口（IPv4/IPv6通用）
uint16_t getDiscoveryServiceNewClientPort(void)
{
  return ntohs( IPDS->newClient->isIPv4? 
                IPDS->newClient->addrV4.sin_port:
                IPDS->newClient->addrV6.sin6_port);
}

static uint16_t getSockAddrPort(const struct sockaddr *addr, bool isIPv4)
{
  if( addr == NULL )
    return 0;
  struct sockaddr_in  *clientAddr4 =  isIPv4? (struct sockaddr_in *)addr:NULL;
  struct sockaddr_in6 *clientAddr6 = !isIPv4? (struct sockaddr_in6*)addr:NULL; 
  return ntohs(isIPv4? clientAddr4->sin_port:clientAddr6->sin6_port);
}

// 获取（IPv4/IPv6通用）客户端地址字符串
const char *getDiscoveryServiceNewClientIPAddr(bool autoFormat)
{
  struct sockaddr *addr = IPDS->newClient->isIPv4?
        (struct sockaddr *)&IPDS->newClient->addrV4:
        (struct sockaddr *)&IPDS->newClient->addrV6;
  return getSockAddrIn(addr, false, IPDS->newClient->isIPv4, autoFormat, true);
}

// 启动发现服务
static void DiscoveryServiceStart(void)
{
  if (IPDS->newClient->run)
    return;
  
  IPDS->newClient->socketV4 = createIPv4DiscoverySocket(); // 创建IPv4 搜索发现Socket 
  IPDS->newClient->socketV6 = createIPv6DiscoverySocket(); // 创建IPv6 搜索发现Socket
  //SafePrintf("Create Socket IPv4 %d IPv6 %d\n", IPv4DS, IPv6DS);

  if ( IPDS->newClient->socketV6 != INVALID_SOCKET_VALUE ) {
    IPDS->newClient->thread = threadCreate(NULL, DiscoveryThread, &IPv6DS);
    if (IPDS->newClient->thread == (thread_t)0) {
      closeSocket(IPDS->newClient->socketV6);
      IPDS->newClient->socketV6 = INVALID_SOCKET_VALUE;
      SafePrintf("Create IPv6 Discovery Thread Failed\n");
    }
    else
      IPDS = &IPv6DS;
  }
  else 
    SafePrintf("Create IPv6 Discovery Socket Failed, System May Not Support IPv6.\n");
    
  // IPv6 启动失败，则启动 IPv4 保底运行
  if( IPDS->newClient->socketV6 == INVALID_SOCKET_VALUE ){ 
    if ( IPDS->newClient->socketV4 != INVALID_SOCKET_VALUE ) {
      IPDS->newClient->thread = threadCreate(NULL, DiscoveryThread, &IPv4DS);
      if (IPDS->newClient->thread == (thread_t)0) {
        closeSocket(IPDS->newClient->socketV4);
        IPDS->newClient->socketV4 = INVALID_SOCKET_VALUE;
        SafePrintf("Create IPv4 Discovery Thread Failed\n");
      }
      else
        IPDS = &IPv4DS;
    }
    else
      SafePrintf("Create IPv4 Discovery Socket Failed\n");
  }
  IPDS->newClient->run = IPDS->newClient->thread != (thread_t)0;
  if (IPDS->newClient->run)
    InitializeCriticalSection_Wrapper(&IPDS->newClient->mt);
}

// 停止发现服务
static void DiscoveryServiceStop(void)
{
  if (!IPDS->newClient->run)
    return;
  IPDS->newClient->run = false;

  // 关闭 IPv4
  if (IPDS->newClient->socketV4 != INVALID_SOCKET_VALUE) {
    closeSocket(IPDS->newClient->socketV4);
    IPDS->newClient->socketV4 = INVALID_SOCKET_VALUE;
  }
    // 关闭 IPv6
  if (IPDS->newClient->socketV6 != INVALID_SOCKET_VALUE) {
    closeSocket(IPDS->newClient->socketV6);
    IPDS->newClient->socketV6 = INVALID_SOCKET_VALUE;
  }

  if (IPDS->newClient->thread) {
    WaitForSingleObject_Wrapper(IPDS->newClient->thread, 1000);
    CloseHandle(IPDS->newClient->thread);
    IPDS->newClient->thread = (thread_t)0;
  }

  DeleteCriticalSection_Wrapper(&IPDS->newClient->mt);
  SafePrintf("Discovery Service Stopped\n");
}

// 创建 IPv4 发现 Socket
static socket_t createIPv4DiscoverySocket(void)
{
	int socketRet;
  uint8_t retryCount = 0;
  socket_t sock = INVALID_SOCKET_VALUE;
  
  while (sock == INVALID_SOCKET_VALUE && retryCount < 5) {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET_VALUE) {
      SafePrintf("Socket create failed, retry %d\n", retryCount);
      Sleep(1);
      retryCount++;
      continue;
    }
    
    int broadcast = 1;
    socketRet = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof broadcast);
    if ( socketRet== SOCKET_ERROR) {
      SafePrintf("Set SO_BROADCAST failed: %ld\n", GetLastError());
      //closeSocket(sock); 
      //return INVALID_SOCKET_VALUE;
    }
  
    int reuseAddr = 1;
    socketRet = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuseAddr, sizeof reuseAddr);
    if (socketRet == SOCKET_ERROR) 
      SafePrintf("Set SO_REUSEADDR failed: %ld\n", GetLastError());
    
#ifdef __linux // Linux下使用 SO_REUSEPORT 允许多个 socket 绑定同一端口
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
    return INVALID_SOCKET_VALUE;
  
  // 设置非阻塞
#ifdef _WIN32
  u_long nonBlocking = 1;
  ioctlsocket(sock, FIONBIO, &nonBlocking);
#else
  int flags = fcntl(sock, F_GETFL, 0);
  fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
  return sock;
}

// 创建 IPv6 发现 Socket
static socket_t createIPv6DiscoverySocket(void)
{
  socket_t sock = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sock == INVALID_SOCKET_VALUE) {
    SafePrintf("IPv6 socket create failed\n");
    return INVALID_SOCKET_VALUE;
  }

  int reuseAddr = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuseAddr, sizeof reuseAddr);

  int v6only = 0;
  setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&v6only, sizeof v6only);

  struct sockaddr_in6 serverAddr;
  memset(&serverAddr, 0, sizeof serverAddr);
  serverAddr.sin6_family = AF_INET6;
  serverAddr.sin6_addr   = in6addr_any;
  serverAddr.sin6_port   = htons(DISCOVERY_PORT);

  if (bind(sock, (struct sockaddr*)&serverAddr, sizeof serverAddr) == SOCKET_ERROR) {
    SafePrintf("IPv6 bind failed: %ld\n", GetLastError());
    closeSocket(sock);
    return INVALID_SOCKET_VALUE;
  }

  /* ★ 自动枚举接口 */
  struct in6_addr groupAddr;
  inet_pton(AF_INET6, DISCOVERY_IPV6_MULTICAST, &groupAddr);

  /* ★ 栈上数组，无 malloc */
  uint32_t ifs[128];
  int joined = 0, ifCount = getIPv6InterfaceIndexes(ifs, (int)(sizeof ifs / sizeof ifs[0]));
  if (ifCount > 0) {
    #ifdef _WIN32
    bool isJoined = false;
    for (int i = 0; i < ifCount; i++) {
      struct ipv6_mreq mreq6;
      memset(&mreq6, 0, sizeof mreq6);
      mreq6.ipv6mr_multiaddr = groupAddr;
      mreq6.ipv6mr_interface = ifs[i];
      int ret = setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                           (char*)&mreq6, sizeof mreq6);
      if(isJoined == false && ret == SOCKET_ERROR){
        SafePrintf("IPv6 JOIN");
        isJoined = true;
      }
      if( isJoined ) 
        SafePrintf(" [if=%u %s:%ld]",
                 ifs[i], ret == SOCKET_ERROR ? "Fail" : "OK", GetLastError());
      if (ret != SOCKET_ERROR) joined++;
    }
    if( isJoined ) 
      SafePrintf("\n");
    #endif
  }

  /* ★ 兜底：接口枚举失败或全部 JOIN 失败时，用索引 0 让内核自选接口 */
  if (joined == 0) {
    struct ipv6_mreq mreq6;
    memset(&mreq6, 0, sizeof mreq6);
    mreq6.ipv6mr_multiaddr = groupAddr;
    mreq6.ipv6mr_interface = 0;
    int ret = setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                         (char*)&mreq6, sizeof mreq6);
    if( ret == SOCKET_ERROR )
      SafePrintf("IPv6 JOIN if=0(auto) %s: %ld\n",
               ret == SOCKET_ERROR ? "Fail" : "OK", GetLastError());
    if (ret == SOCKET_ERROR) {
      closeSocket(sock);
      SafePrintf("IPv6 JOIN Error:%d\n", ret);
      return INVALID_SOCKET_VALUE;
    }
  }
  
  return sock;
}

static void sendUDPTestMessage(socket_t *IPv4Sock, socket_t *IPv6Sock)
{ 
  bool hasV4 = IPv4Sock != NULL;
  bool hasV6 = IPv6Sock != NULL; 
  if (hasV4 == hasV6){  // 全空或全有 
      SafePrintf("Don't args: must provide exactly one socket (IPv4:%s, IPv6:%s)\n",
          hasV4 ? "Yes" : "No", hasV6 ? "Yes" : "No");
      return;
  }

  socket_t sock = INVALID_SOCKET_VALUE;
  struct sockaddr *testAddr = NULL;
  socklen_t testAddrlen = 0;
  if( IPv4Sock != NULL && *IPv4Sock != INVALID_SOCKET_VALUE ){
    struct sockaddr_in testAddr4;
    memset(&testAddr4, 0, sizeof testAddr4);
    testAddr4.sin_family = AF_INET;
    testAddr4.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    testAddr4.sin_port = htons(DISCOVERY_PORT);
    sock = *IPv4Sock;
    testAddr = (struct sockaddr*)&testAddr4;
    testAddrlen = sizeof testAddr4;
  }

  if( IPv6Sock != NULL && *IPv6Sock != INVALID_SOCKET_VALUE ){
    struct sockaddr_in6 testAddr6;
    memset(&testAddr6, 0, sizeof testAddr6);
    testAddr6.sin6_family = AF_INET6;
    inet_pton(AF_INET6, DISCOVERY_IPV6_MULTICAST, &testAddr6.sin6_addr);
    testAddr6.sin6_port = htons(DISCOVERY_PORT);
    testAddr6.sin6_scope_id = 0;  // 由系统选择接口
    sock = *IPv6Sock;
    testAddr = (struct sockaddr*)&testAddr6;
    testAddrlen = sizeof testAddr6; 
  }

  if( testAddr == NULL || sock == INVALID_SOCKET_VALUE )
    return;

  // 发送测试广播组播包
  int sendRet = sendto(sock, broadcastTestMsg, strlen(broadcastTestMsg), 0, testAddr, testAddrlen);
  if (sendRet == SOCKET_ERROR) {
    int err = GetLastError();
    logPrint("Warning: IPv%c Broadcast Test Failed (err=%d), may cause issues\n", hasV4? '4':'6', err);
    SafePrintf("Warning: IPv%c Broadcast Test Failed (err=%d), may cause issues\n", hasV4? '4':'6', err);
  }
}

static void sendIPv4BroadcastTestMessage(void *arg)
{
  socket_t sock = *((socket_t*)(((ThreadPoolArgWrapper*)arg)->arg));
  sendUDPTestMessage(&sock, NULL); 
}

static void sendIPv6MulticastTestMessage(void *arg)
{
  socket_t sock = *((socket_t*)(((ThreadPoolArgWrapper*)arg)->arg));
  sendUDPTestMessage(NULL, &sock);
}

// 发现服务线程
threadRet WINAPI DiscoveryThread(void* lpParam)
{ 
  if( lpParam == NULL ){
    SafePrintf("Discovery Server Thread: Invalid Args\n");
    return (threadRet)0;
  }

  discoveryServerInfo_t *DSInfo = (discoveryServerInfo_t*)lpParam;

  uint8_t titleCount = 0;
  char recvBuffer[256];
  int bytesReceived, selectResult;

  fd_set readSet;
  struct timeval timeout;

  if (*DSInfo->socket == INVALID_SOCKET_VALUE) {
    SafePrintf("IPv%d Discovery Thread: invalid socket, exiting\n", DSInfo->IPvNum);
    return (threadRet)0;
  }

  char DiscoveryServerString[50];
  memset(DiscoveryServerString, 0, sizeof DiscoveryServerString);
  snprintf(DiscoveryServerString, sizeof DiscoveryServerString, 
    "IPv%d Discovery PROT:%d", DSInfo->IPvNum, DISCOVERY_PORT);

  SafePrintf("Discovery Service On IPv%d UDP Port: %d\n", DSInfo->IPvNum, DISCOVERY_PORT);

  while ( DSInfo->newClient->run ) {
    if( ++titleCount % 5 == 0 )
      updataConsoleTitle(DiscoveryServerString);

    FD_ZERO(&readSet);
    FD_SET(*DSInfo->socket, &readSet);
    timeout.tv_sec = 0;
    timeout.tv_usec = DISCOVERY_INTERVAL_MS * 1000;

    selectResult = select((int)(*DSInfo->socket) + 1, &readSet, NULL, NULL, &timeout);
    if (selectResult == SOCKET_ERROR) {
      SafePrintf("IPv%d Discovery Select Error: %ld\n", DSInfo->IPvNum, GetLastError());
      Sleep(DISCOVERY_INTERVAL_MS);
      continue;
    }
    if (selectResult == 0 || FD_ISSET(*DSInfo->socket, &readSet) == 0)
      continue;

    DSInfo->socklen = DSInfo->IPvNum == 4? sizeof(struct sockaddr_in):sizeof(struct sockaddr_in6);
    memset(DSInfo->addr, 0, DSInfo->socklen);
    bytesReceived = recvfrom(*DSInfo->socket, recvBuffer,
                              sizeof recvBuffer - 2, 0,
                              DSInfo->addr, &DSInfo->socklen);
    if (bytesReceived <= 0) {
      int err = GetLastError();
      if (err != WSAEWOULDBLOCK) 
        SafePrintf("IPv%d Discovery Recvfrom Error=%d\n", DSInfo->IPvNum, err);
      Sleep( err == WSAEWOULDBLOCK ? 10:100);
      continue;
    }
    recvBuffer[bytesReceived < (int)sizeof recvBuffer? bytesReceived:(int)sizeof recvBuffer - 1] = '\0';

    if ( DSInfo->IPvNum == 4 ) 
      DSInfo->newClient->isIPv4 = true;
    else {
      struct sockaddr_in6 *client6 = (struct sockaddr_in6 *)DSInfo->addr;
      DSInfo->newClient->isIPv4 = IN6_IS_ADDR_V4MAPPED( &client6->sin6_addr);
      if ( DSInfo->newClient->isIPv4 ) {
          /* 极少数情况：V6ONLY=0 时 IPv6 socket 收到 mapped 包 */ 
          memset(&DSInfo->newClient->addrV4, 0, sizeof DSInfo->newClient->addrV4);
          DSInfo->newClient->addrV4.sin_family = AF_INET;
          DSInfo->newClient->addrV4.sin_port   = client6->sin6_port;
          memcpy(&DSInfo->newClient->addrV4.sin_addr, &client6->sin6_addr.s6_addr[12], 4);
      }
    }

    socket_t replySocket = DSInfo->newClient->isIPv4? 
                            DSInfo->newClient->socketV4:
                            DSInfo->newClient->socketV6;
    struct sockaddr* replyAddr = DSInfo->newClient->isIPv4?
                                (struct sockaddr*)&DSInfo->newClient->addrV4:
                                (struct sockaddr*)&DSInfo->newClient->addrV6;
    const char *IPAddr = getSockAddrIn(replyAddr, false, DSInfo->newClient->isIPv4, true, true);
    const uint16_t port = getSockAddrPort(replyAddr, DSInfo->newClient->isIPv4);
    uint8_t IPvNum = DSInfo->newClient->isIPv4? 4:6;
    
    if (saveInfo.serverPrintData == 3)  // 显示信息 
      SafePrintf("UDP Recv IPv%d %s:%d Len:%d Data: %s\n", 
          IPvNum, IPAddr, port, bytesReceived, recvBuffer);
    
    if (strnicmp(recvBuffer, "discover_com2tcp_server", strlen("discover_com2tcp_server")) == 0) {
      EnterCriticalSection_Wrapper(&DSInfo->newClient->mt);
      sendDiscoveryResponse(replySocket, replyAddr, DSInfo->newClient->isIPv4);
      LeaveCriticalSection_Wrapper(&DSInfo->newClient->mt);
    }
    else if (strnicmp(recvBuffer, CONTROL_HEADER"errorTest", strlen(CONTROL_HEADER"errorTest")) == 0) 
      ErrorCodeTest();  // 异常测试钩子
    else if (strnicmp(recvBuffer, CONTROL_HEADER, strlen(CONTROL_HEADER)) == 0){
      HandleClientCommand(&replySocket, recvBuffer + strlen(CONTROL_HEADER));
    }
    else if( strnicmp(recvBuffer, broadcastTestMsg, strlen(broadcastTestMsg)) == 0 ){ 
      bool *isTest = DSInfo->newClient->isIPv4? 
                      &DSInfo->newClient->testV4: 
                      &DSInfo->newClient->testV6;
      if( *isTest == false )
        *isTest = true;
      static uint16_t count = 0;
      SafePrintf("%s IPv%d...OK! MsgLen:%d, [IPv4:%s IPv6:%s] Number:%-5d\r", 
          broadcastTestMsg, IPvNum, bytesReceived, 
          DSInfo->newClient->testV4? "Yes":"No ",
          DSInfo->newClient->testV6? "Yes":"No ", ++count);
    } 

  }

  SafePrintf("IPv%d Discovery Thread Exiting\n", DSInfo->IPvNum);
  return (threadRet)0;
}



static void sendDiscoveryResponse(socket_t socket, struct sockaddr *addr, bool isIPv4)
{
  static uint16_t count = 0;

  /* ---------- 构造响应内容（只做一次） ---------- */
  static char *getVersions = NULL;
  if (getVersions == NULL) {
    static char getVersionsBuff[512];
    getVersions = getVersionsBuff;
    memset(getVersionsBuff, 0, sizeof getVersionsBuff);
#ifdef _WIN32
    char versionsBuff[20];
    getWindowsVersionSimple(versionsBuff);
    snprintf(getVersionsBuff, sizeof getVersionsBuff, "Win%s", versionsBuff);
#else
    struct utsname systemInfo;
    if (uname(&systemInfo) != 0)
      snprintf(getVersionsBuff, sizeof getVersionsBuff, "Linux Null Versions");
    else
      snprintf(getVersionsBuff, sizeof getVersionsBuff, "%s-%s",
               systemInfo.sysname, systemInfo.machine);
#endif
  }

  /* ---------- 构建响应消息 ---------- */
  const char *ComputerFullName = getComputerFullName();
  static char responseString[256];
  memset(responseString, 0, sizeof responseString);
  snprintf(responseString, sizeof responseString, "%s|%-15s|%-15s|%d|%u|%u|%s|\n",
           DISCOVERY_MAGIC, ComputerFullName,
           getVersions ? getVersions : "NULL",
           getMainServerPort(), getClientNum(), getMaxClient(),
           GetSystemUniqueIdentifier());

  #if 0
  const char *getServerIP = GetMatchingSubnetIPv6(clientAddr6);
  bool getIPmethod = true;    // 获取IPv4的方法
  const char *getServerIP = "NULL IP";
  if( getIPmethod == true )   // 方法1：使用socket连接方式获取正确IP（更可靠）
    getServerIP = GetMatchingSubnetIP(clientAddr);
  else                        // 方法2：或者使用网段匹配算法
    getServerIP = SelectMatchingSubnetIP( inet_ntoa(clientAddr->sin_addr) );
  #endif

  uint16_t port = getSockAddrPort(addr, isIPv4); 
  const char *IPaddr = getSockAddrIn(addr, false, isIPv4, true, true);
  socklen_t sockLen = isIPv4? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
  //int sendResult = sendto(socket, responseString, strlen(responseString), 0, addr, sockLen);

  struct sockaddr_storage sendAddr;
  memcpy(&sendAddr, addr, sockLen);

  if (!isIPv4) {
      struct sockaddr_in6 *src6 = (struct sockaddr_in6 *)&sendAddr;
      // char dbg[INET6_ADDRSTRLEN];
      // inet_ntop(AF_INET6, &src6->sin6_addr, dbg, sizeof dbg);
      // SafePrintf("v6 dst=%s scope=%-3ld ll=%-10u\n", dbg, src6->sin6_scope_id,
      //           IN6_IS_ADDR_LINKLOCAL(&src6->sin6_addr));
      if (/*!IN6_IS_ADDR_LINKLOCAL(&src6->sin6_addr) &&*/ src6->sin6_scope_id != 0)
          src6->sin6_scope_id = 0; // 让内核自己选最优的适配器去发送
  }

  int sendResult = sendto(socket, responseString, strlen(responseString), 
                                0, (struct sockaddr *)&sendAddr, sockLen);
  SafePrintf("Discovery Response Send To IPv%d %s:%d %s:%ld Count:%-5d%c", isIPv4? 4:6,
            IPaddr, port, sendResult == SOCKET_ERROR ? "Failed" : "Succeed",
            GetLastError(), ++count, isRunningAsService()? '\n' : '\r');
}

/* 枚举应 JOIN 的 IPv6 接口索引。
 * ifs     : 调用方提供的数组
 * maxCount: 数组容量
 * 返回    : 实际写入的接口数量（0 表示未找到或失败）
 */
static int getIPv6InterfaceIndexes(uint32_t *ifs, int maxCount)
{
  /* 适配器列表缓冲区：静态复用，避免每次 malloc。
   * 一般 16KB 足够容纳常见机器的适配器信息；
   * 若 ERROR_BUFFER_OVERFLOW 且需要的更大，再退回堆分配一次。 */
#ifdef _WIN32
  static IP_ADAPTER_ADDRESSES s_addrsBuf[128];   /* 按实际结构大小算，见下注 */
  ULONG bufLen = sizeof s_addrsBuf;
  IP_ADAPTER_ADDRESSES *addrs = s_addrsBuf;
  IP_ADAPTER_ADDRESSES *heapAddrs = NULL;

  DWORD ret = GetAdaptersAddresses(AF_INET6, 0, NULL, addrs, &bufLen);
  if (ret == ERROR_BUFFER_OVERFLOW) {
    /* 静态缓冲不够，兜底用堆分配一次（只在极少数机器上发生） */
    heapAddrs = (IP_ADAPTER_ADDRESSES*)malloc(bufLen * 2);
    if (!heapAddrs) return 0;
    addrs = heapAddrs;
    ret = GetAdaptersAddresses(AF_INET6, 0, NULL, addrs, &bufLen);
  }
  if (ret != NO_ERROR) {
    free(heapAddrs);
    return 0;
  }

  int count = 0;
  for (IP_ADAPTER_ADDRESSES *p = addrs; p && count < maxCount; p = p->Next) {
    if (//p->OperStatus != IfOperStatusUp ||      // 跳过当前未处于“已连接/已启用”状态的适配器
        p->IfType == IF_TYPE_SOFTWARE_LOOPBACK || // 跳过软件回环适配器
        p->Ipv6IfIndex == 0 ||                    // 没有有效 IPv6 接口索引的适配器
        p->Flags & IP_ADAPTER_NO_MULTICAST )      // 跳过被标记为“不支持组播”的适配器
      continue; 
    ifs[count++] = p->Ipv6IfIndex;                // 可用的 Ipv6IfIndex
  }

  free(heapAddrs);
  return count;
#else
  struct ifaddrs *ifa = NULL;
  if (getifaddrs(&ifa) != 0) 
    return 0;
  int count = 0;
  for (struct ifaddrs *p = ifa; p && count < maxCount; p = p->ifa_next) {
    if (!p->ifa_addr  )                     continue; // 跳过没有地址的条目
    if (p->ifa_addr->sa_family != AF_INET6) continue; // 跳过地址不是 IPv6 的条目
    if (!(p->ifa_flags & IFF_UP))           continue; // 跳过未启用的接口
    if (!(p->ifa_flags & IFF_MULTICAST))    continue; // 跳过不支持组播的接口
    if (p->ifa_flags & IFF_LOOPBACK)        continue; // 跳过回环接口

    uint32_t idx = if_nametoindex(p->ifa_name);
    if (idx == 0) continue;

    /* 去重：同一接口可能有多个 IPv6 地址 */
    bool dup = false;
    for (int i = 0; i < count; i++)
      if (ifs[i] == idx) { dup = true; break; }
    if (dup) continue;

    ifs[count++] = idx; // 可用的 Ipv6IfIndex
  }
  freeifaddrs(ifa);
  return count;
#endif
}


/*==============================================================================
 * @brief  将 sockaddr 转为 IP 字符串（IPv4/IPv6 通用）
 * @param  addr               指向 sockaddr_in 或 sockaddr_in6
 * @param  addrIsSockAddrIn6  参数 addr 实际指向 sockaddr_in6（用于判断 IPv4-mapped）
 * @param  isIPv4             是否为 IPv4 地址
 * @param  autoFormat         true 时 IPv6 加方括号 [::1]
 * @param  IPv6AddScopeId     是否添加 IPv6 域索引
 * @return 静态缓冲区中的 IP 字符串（非线程安全，调用方需立即使用/拷贝）
 * @note
 *   - isIPv6Sockaddr == true && isIPv4 == true 时，表示 addr 是 sockaddr_in6
 *     但里面存的是 IPv4-mapped 地址（::ffff:a.b.c.d），需要取后 4 字节当 IPv4 用。
 *   - 其他情况：isIPv4 决定用 AF_INET 还是 AF_INET6 解析。
 *============================================================================*/
static const char* getSockAddrIn(const struct sockaddr *addr, bool addrIsSockAddrIn6,
                          bool isIPv4, bool autoFormat, bool IPv6AddScopeId)
{
    static char ipBuf[INET6_ADDRSTRLEN + 20];   /* 足够容纳 "[" + IPv6 + "]" + '\0' */

    if (addr == NULL)
        return "Unknown IP";
    memset(ipBuf, 0, sizeof ipBuf);

    const void *sinAddr;
    bool needBracket = false;
    uint32_t scopeId = 0;

    if (addrIsSockAddrIn6 && isIPv4) {
      /* IPv6 socket 收到 IPv4-mapped 地址：从 sockaddr_in6 中提取后 4 字节 */
      const struct sockaddr_in6 *client6 = (const struct sockaddr_in6 *)addr;
      sinAddr = &client6->sin6_addr.s6_addr[12];   /* 直接指向后 4 字节，无需临时结构体 */
    }
    else if (isIPv4) {
      const struct sockaddr_in *client4 = (const struct sockaddr_in *)addr;
      sinAddr = &client4->sin_addr;
    }
    else {
      const struct sockaddr_in6 *client6 = (const struct sockaddr_in6 *)addr;
      sinAddr = &client6->sin6_addr;
      needBracket = autoFormat;
      scopeId = client6->sin6_scope_id;
    }

    /* 真正用于解析的地址族：isIPv4==true 一律用 AF_INET */
    int family = isIPv4 ? AF_INET : AF_INET6;

    /* 预留 '[' 的位置 */
    char *writePos = ipBuf + (needBracket ? 1 : 0);
    size_t writeSize = sizeof(ipBuf) - (needBracket ? 2 : 0);  /* 预留 [] 和 '\0' */

    const char *result = inet_ntop(family, sinAddr, writePos, (socklen_t)writeSize);
    if (result == NULL)
        return "Unknown IP";

    /* 如果需要，附加 scope_id（仅 IPv6 且非 IPv4-mapped 情况） */
    if (!isIPv4 && IPv6AddScopeId && scopeId != 0) {
        size_t len = strlen(writePos);
        /* 剩余空间检查：需要至少 "%u" + '\0' 的空间（num 最大 10 位 + '%' + '\0'） */
        size_t remaining = sizeof(ipBuf) - (needBracket ? 2 : 0) - len;
        if (remaining > 12) 
            snprintf(writePos + len, remaining, "%%%u", (unsigned)scopeId);
    }

    if (needBracket) {
        size_t len = strlen(ipBuf + 1);
        ipBuf[0] = '[';
        ipBuf[len + 1] = ']';
        ipBuf[len + 2] = '\0';
    }

    return ipBuf;
}
