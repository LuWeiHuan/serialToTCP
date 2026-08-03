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
  bool isIPv4; // 新客户端IP版本标记
  struct sockaddr_storage addr; 
  mutex_type mt;
}ClientInfo_t;

typedef struct {
  const uint8_t IPvNum;
  socket_t socket;
  thread_t thread;
  ClientInfo_t *const newClient;
}discoveryServerInfo_t;

/*================== 本地宏定义     =========================================*/
#define DISCOVERY_INTERVAL_MS  200        // 发现请求检查间隔

/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static char *broadcastTestMessage = "Broadcast Test Message";

static ClientInfo_t newClientInfo = {
  .isIPv4 = false, 
  .testV4 = false,
  .testV6 = false,
};

static discoveryServerInfo_t IPv4DS = {
  .IPvNum = 4,
  .socket = INVALID_SOCKET_VALUE, 
  .newClient = &newClientInfo,
  .thread = (thread_t)0,
};

static discoveryServerInfo_t IPv6DS = {
  .IPvNum = 6,
  .socket = INVALID_SOCKET_VALUE,
  .newClient = &newClientInfo,
  .thread = (thread_t)0,
};

/*================== 本地函数声明    ========================================*/
static void DiscoveryServiceStart(void);
static void DiscoveryServiceStop(void);
threadRet WINAPI discoveryServerThread(void*);
static socket_t createDiscoverySocket(uint16_t port, bool isIPv4); 
static void sendIPv4BroadcastTestMessage(void *arg);
static void sendIPv6MulticastTestMessage(void *arg);
static int getIPv6InterfaceIndexes(uint32_t *outIfs, int maxCount);
static void sendDiscoveryResponse(socket_t socket, struct sockaddr const * const addr, bool isIPv4);
static void handleUdpData(discoveryServerInfo_t const * const DSInfo, 
                          struct sockaddr const * const retAddrPtr, 
                          const char *recvBuffer, uint32_t bytesReceived);
static uint16_t getSocketBindingPort(socket_t socket, bool isIPv4);
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
  newClientInfo.testV4 = false;
  newClientInfo.testV6 = false;

  // 在 200 ms 后向广播组播发送一条测试消息
  static ThreadTask asyncSendBroadcast;
  threadTaskInit(&asyncSendBroadcast, sendIPv4BroadcastTestMessage, 
                  &IPv4DS.socket, 200, 0);
  threadTtaskStart(gThreadPool, &asyncSendBroadcast);

  static ThreadTask asyncSendMulticast;
  threadTaskInit(&asyncSendMulticast, sendIPv6MulticastTestMessage, 
                  &IPv6DS.socket, 210, 0);
  threadTtaskStart(gThreadPool, &asyncSendMulticast);
  SafePrintf("Broadcast Test Message...\r");
}

bool isDiscoveryServiceSocket(socket_t sock)
{
  return IPv4DS.socket == sock || IPv6DS.socket == sock;
}

uint8_t getDiscoveryServiceNewClientIPvNum(void)
{
  return newClientInfo.isIPv4? 4:6;
}

// 获取客户端端口（IPv4/IPv6通用）
uint16_t getDiscoveryServiceNewClientPort(void)
{
  struct sockaddr_in  *portV4 = (struct sockaddr_in  *)&newClientInfo.addr;
  struct sockaddr_in6 *portV6 = (struct sockaddr_in6 *)&newClientInfo.addr;
  return ntohs( newClientInfo.isIPv4? portV4->sin_port: portV6->sin6_port);
}

static uint16_t getSockAddrPort(const struct sockaddr *addr, bool isIPv4)
{
  if( addr == NULL )
    return 0;
  struct sockaddr_in  *clientAddr4 =  isIPv4? (struct sockaddr_in *)addr:NULL;
  struct sockaddr_in6 *clientAddr6 = !isIPv4? (struct sockaddr_in6*)addr:NULL; 
  return ntohs(isIPv4? clientAddr4->sin_port:clientAddr6->sin6_port);
}

// 获取新的客户端地址字符串（IPv4/IPv6通用）
const char *getDiscoveryServiceNewClientIPAddr(bool autoFormat)
{
  struct sockaddr *addr = (struct sockaddr *)&newClientInfo.addr;
  return getSockAddrIn(addr, false, newClientInfo.isIPv4, autoFormat, true);
}

// 启动发现服务
static void DiscoveryServiceStart(void)
{
  if( IPv4DS.socket != INVALID_SOCKET_VALUE || 
      IPv6DS.socket != INVALID_SOCKET_VALUE )
    return;
  InitializeCriticalSection_Wrapper(&newClientInfo.mt);

  // 创建IPv4和IPv6 搜索发现Socket 
  IPv4DS.socket = createDiscoverySocket(DISCOVERY_PORT, true); 
  IPv6DS.socket = createDiscoverySocket(DISCOVERY_PORT, false);
  // 实现IPv6 启动失败，则启动 IPv4 保底运行
  //if( IPv6DS.socket == INVALID_SOCKET_VALUE ) 
    // 如果IPv4 线程没有启动的话，会收不到 IPv4 UDP 单播数据包
  if ( IPv4DS.socket != INVALID_SOCKET_VALUE ) {
    IPv4DS.thread = threadCreate(NULL, discoveryServerThread, &IPv4DS);
    if (IPv4DS.thread == (thread_t)0) {
      closeSocket(IPv4DS.socket);
      IPv4DS.socket = INVALID_SOCKET_VALUE;
      SafePrintf("Create IPv4 Discovery Thread Failed\n");
    }
  }
  else
    SafePrintf("Create IPv4 Discovery Socket Failed\n");
  
  if ( IPv6DS.socket != INVALID_SOCKET_VALUE ) {
    IPv6DS.thread = threadCreate(NULL, discoveryServerThread, &IPv6DS);
    if (IPv6DS.thread == (thread_t)0) {
      closeSocket(IPv6DS.socket);
      IPv6DS.socket = INVALID_SOCKET_VALUE;
      SafePrintf("Create IPv6 Discovery Thread Failed\n");
    }
  }
  else 
    SafePrintf("Create IPv6 Discovery Socket Failed, System May Not Support IPv6.\n");
}

// 停止发现服务
static void DiscoveryServiceStop(void)
{
  if ( IPv4DS.socket == INVALID_SOCKET_VALUE && 
       IPv6DS.socket == INVALID_SOCKET_VALUE )
    return;

  // 关闭 IPv4
  if (IPv4DS.socket != INVALID_SOCKET_VALUE) {
    closeSocket(IPv4DS.socket);
    IPv4DS.socket = INVALID_SOCKET_VALUE;
  }
    // 关闭 IPv6
  if (IPv6DS.socket != INVALID_SOCKET_VALUE) {
    closeSocket(IPv6DS.socket);
    IPv6DS.socket = INVALID_SOCKET_VALUE;
  }

  if (IPv4DS.thread != (thread_t)0) {
    WaitForSingleObject_Wrapper(IPv4DS.thread, 1000);
    CloseHandle(IPv4DS.thread);
    IPv4DS.thread = (thread_t)0;
  }

  if (IPv6DS.thread != (thread_t)0) {
    WaitForSingleObject_Wrapper(IPv6DS.thread, 1000);
    CloseHandle(IPv6DS.thread);
    IPv6DS.thread = (thread_t)0;
  }

  DeleteCriticalSection_Wrapper(&newClientInfo.mt);
  SafePrintf("Discovery Service Stopped\n");
}

/*==============================================================================
 * @brief  创建发现服务 Socket（IPv4/IPv6 通用）
 * @param  port   绑定的 UDP 端口
 * @param  isIPv4 true 创建 IPv4 套接字；false 创建 IPv6 套接字
 * @return 成功返回 socket，失败返回 INVALID_SOCKET_VALUE
 * @note
 *   - IPv4: 开启 SO_BROADCAST / SO_REUSEADDR（Linux 下额外开启 SO_REUSEPORT），
 *           绑定 INADDR_ANY。
 *   - IPv6: 开启 SO_REUSEADDR / IPV6_V6ONLY=0，绑定 in6addr_any，
 *           并自动枚举接口加入组播组，失败时用 if=0 兜底。
 *============================================================================*/
static socket_t createDiscoverySocket(uint16_t port, bool isIPv4)
{
  const int family = isIPv4 ? AF_INET : AF_INET6;
  socket_t sock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == INVALID_SOCKET_VALUE) {
    SafePrintf("IPv%d Socket Create Failed\n", isIPv4 ? 4 : 6);
    return INVALID_SOCKET_VALUE;
  }

  /* ---------- 公共选项：SO_REUSEADDR ---------- */
  int ret, reuseAddr = 1;
  ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   (char*)&reuseAddr, sizeof reuseAddr);
  if (ret == SOCKET_ERROR)
    SafePrintf("IPv%d Set SO_REUSEADDR Failed: %ld\n",
               isIPv4 ? 4 : 6, GetLastError());

  /* ---------- IPv4 专属选项 ---------- */
  if (isIPv4) {
    int broadcast = 1;
    ret = setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
                     (char*)&broadcast, sizeof broadcast);
    if (ret == SOCKET_ERROR)
      SafePrintf("IPv4 Set SO_BROADCAST Failed: %ld\n", GetLastError());

#ifdef __linux
    int reusePort = 1;
    ret = setsockopt(sock, SOL_SOCKET, SO_REUSEPORT,
                     (char*)&reusePort, sizeof reusePort);
    if (ret == SOCKET_ERROR)
      SafePrintf("IPv4 Set SO_REUSEPORT Failed: %ld\n", GetLastError());
#endif
  }

  /* ---------- IPv6 专属选项 + 组播加入 ---------- */
  if (!isIPv4) {
    int v6only = 0;
    ret = setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY,
                     (char*)&v6only, sizeof v6only);
    if (ret == SOCKET_ERROR)
      SafePrintf("IPv6 Set IPV6_V6ONLY Failed: %ld\n", GetLastError());
  }

  /* ---------- 绑定 ---------- */
  struct sockaddr_storage serverAddr;
  memset(&serverAddr, 0, sizeof serverAddr);
  socklen_t addrLen;
  if (isIPv4) {
    struct sockaddr_in *a4 = (struct sockaddr_in *)&serverAddr;
    a4->sin_family      = AF_INET;
    a4->sin_addr.s_addr = htonl(INADDR_ANY);
    a4->sin_port        = htons(port);
    addrLen             = sizeof(struct sockaddr_in);
  }
  else {
    struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)&serverAddr;
    a6->sin6_family = AF_INET6;
    a6->sin6_addr   = in6addr_any;
    a6->sin6_port   = htons(port);
    addrLen         = sizeof(struct sockaddr_in6);
  }

  ret = bind(sock, (struct sockaddr*)&serverAddr, addrLen);
  if (ret != 0) {
    int err = errno;
    SafePrintf("IPv%d Bind Failed: %s (Errno Code:%d, Bind Ret:%d)\n",
               isIPv4 ? 4 : 6, strerror(err), err, ret);
    closeSocket(sock);
    return INVALID_SOCKET_VALUE;
  }

  /* ---------- IPv6 需要加入组播 ---------- */
  if (!isIPv4) {
    struct in6_addr groupAddr;
    inet_pton(AF_INET6, DISCOVERY_IPV6_MULTICAST, &groupAddr);

    uint32_t ifs[128];
    int joined  = 0;
    int ifCount = getIPv6InterfaceIndexes(ifs, (int)(sizeof ifs / sizeof ifs[0]));
    if (ifCount > 0) {
#ifdef _WIN32
      bool isJoined = false;
      for (int i = 0; i < ifCount; i++) {
        struct ipv6_mreq mreq6;
        memset(&mreq6, 0, sizeof mreq6);
        mreq6.ipv6mr_multiaddr = groupAddr;
        mreq6.ipv6mr_interface = ifs[i];
        int jret = setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                              (char*)&mreq6, sizeof mreq6);
        if (isJoined == false && jret == SOCKET_ERROR) {
          SafePrintf("IPv6 JOIN");
          isJoined = true;
        }
        if (isJoined)
          SafePrintf(" [if=%u %s:%ld]",
                     ifs[i], jret == SOCKET_ERROR ? "Fail" : "OK",
                     GetLastError());
        if (jret != SOCKET_ERROR) joined++;
      }
      if (isJoined)
        SafePrintf("\n");
#endif
    }

    /* 兜底：接口枚举失败或全部 JOIN 失败时，用索引 0 让内核自选接口 */
    if (joined == 0) {
      struct ipv6_mreq mreq6;
      memset(&mreq6, 0, sizeof mreq6);
      mreq6.ipv6mr_multiaddr = groupAddr;
      mreq6.ipv6mr_interface = 0;
      int jret = setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                            (char*)&mreq6, sizeof mreq6);
      if (jret == SOCKET_ERROR) {
        SafePrintf("IPv6 JOIN if=0(auto) Fail: %ld\n", GetLastError());
        closeSocket(sock);
        SafePrintf("IPv6 JOIN Error:%d\n", jret);
        return INVALID_SOCKET_VALUE;
      }
    }
  }

  /* ---------- 设置非阻塞 ---------- */
#ifdef _WIN32
  u_long nonBlocking = 1;
  ioctlsocket(sock, FIONBIO, &nonBlocking);
#else
  int flags = fcntl(sock, F_GETFL, 0);
  fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

  return sock;
}

static int sendUdpMessage(socket_t sock, uint16_t port, char const * const addr, bool isPv4,
                          const char *message, uint32_t len)
{
  if (sock == INVALID_SOCKET_VALUE || addr == NULL || message == NULL)
    return SOCKET_ERROR;

  struct sockaddr_storage ss;   // 用通用的 storage，足够容纳 v4/v6
  memset(&ss, 0, sizeof ss); 
  socklen_t sendAddrlen = 0;

  if (isPv4) {
    struct sockaddr_in *a4 = (struct sockaddr_in *)&ss;
    a4->sin_family = AF_INET;
    inet_pton(AF_INET, addr, &a4->sin_addr);
    a4->sin_port = htons(port);
    sendAddrlen = sizeof( struct sockaddr_in );
  } else {
    struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)&ss;
    a6->sin6_family = AF_INET6;
    inet_pton(AF_INET6, addr, &a6->sin6_addr);
    a6->sin6_port = htons(port);
    a6->sin6_scope_id = 0; // 由系统选择接口
    sendAddrlen = sizeof(struct sockaddr_in6);
  }

  struct sockaddr *sendAddr = (struct sockaddr *)&ss;
  return sendto(sock, message, len, 0, sendAddr, sendAddrlen);
}





// 发现服务线程
threadRet WINAPI discoveryServerThread(void* lpParam)
{ 
  if( lpParam == NULL ){
    SafePrintf("Discovery Server Thread: Invalid Args\n");
    return (threadRet)0;
  }

  discoveryServerInfo_t *DSInfo = (discoveryServerInfo_t*)lpParam;

  uint8_t titleCount = 0;
  char recvBuffer[256]; 

  fd_set readSet;
  struct timeval timeout;
  struct sockaddr_storage retAddr;
  struct sockaddr * const retAddrPtr = (struct sockaddr*)&retAddr;
  bool isIPv4;

  if (DSInfo->socket == INVALID_SOCKET_VALUE) {
    SafePrintf("IPv%d Discovery Thread: invalid socket, exiting\n", DSInfo->IPvNum);
    return (threadRet)0;
  }

  uint16_t port = getSocketBindingPort(DSInfo->socket, DSInfo->IPvNum == 4);

  char DiscoveryServerString[50];
  memset(DiscoveryServerString, 0, sizeof DiscoveryServerString);
  snprintf(DiscoveryServerString, sizeof DiscoveryServerString, 
    "IPv%d Discovery PROT:%d", DSInfo->IPvNum, port);

  SafePrintf("Discovery Service On IPv%d UDP Port: %d\n", DSInfo->IPvNum, port);

  while ( DSInfo->socket != INVALID_SOCKET_VALUE ) {
    if( ++titleCount % 5 == 0 )
      updataConsoleTitle(DiscoveryServerString);
    
    FD_ZERO(&readSet);
    FD_SET(DSInfo->socket, &readSet);
    timeout.tv_sec = 0;
    timeout.tv_usec = DISCOVERY_INTERVAL_MS * 1000;

    int selectResult = select((int)(DSInfo->socket) + 1, &readSet, NULL, NULL, &timeout);
    if (selectResult == SOCKET_ERROR) {
      SafePrintf("IPv%d Discovery Select Error: %ld\n", DSInfo->IPvNum, GetLastError());
      Sleep(DISCOVERY_INTERVAL_MS);
      continue;
    }
    if (selectResult == 0 || FD_ISSET(DSInfo->socket, &readSet) == 0)
      continue;

    memset(&retAddr, 0, sizeof retAddr);
    socklen_t socklen = DSInfo->IPvNum == 4? sizeof(struct sockaddr_in):sizeof(struct sockaddr_in6);
    int bytesReceived = recvfrom(DSInfo->socket, recvBuffer, sizeof recvBuffer - 2, 0, retAddrPtr, &socklen);
    if (bytesReceived <= 0) {
      int err = GetLastError();
      if (err != WSAEWOULDBLOCK) 
        SafePrintf("IPv%d Discovery Recvfrom Error=%d\n", DSInfo->IPvNum, err);
      Sleep( err == WSAEWOULDBLOCK ? 10:100);
      continue;
    }
    recvBuffer[bytesReceived < (int)sizeof recvBuffer? bytesReceived:(int)sizeof recvBuffer - 1] = '\0';
    
    if ( DSInfo->IPvNum == 4 )
      isIPv4 = true;
    else {
      struct sockaddr_in6 *client6 = (struct sockaddr_in6 *)&retAddr;
      isIPv4 = IN6_IS_ADDR_V4MAPPED(&client6->sin6_addr);
      if ( isIPv4 )
        continue; // IPv6 不处理IPv4数据包了，因为收不到IPv4 UDP 单播数据包
    }

    DSInfo->newClient->isIPv4 = isIPv4;
    DSInfo->newClient->addr = retAddr;

    handleUdpData(DSInfo, retAddrPtr, recvBuffer, bytesReceived);
  }

  SafePrintf("IPv%d Discovery Thread Exiting, socket:%d\n", DSInfo->IPvNum, (uint32_t)DSInfo->socket);
  return (threadRet)0;
}


/*==============================================================================
 * @brief  处理UDP 数据
 * @param  DSInfo           搜索服务信息
 * @param  addr             新客户端地址
 * @param  recvBuffer       接收到的数据
 * @param  bytesReceived    接收到的数据长度
 * @return 
 * @note
 *============================================================================*/
static void handleUdpData(discoveryServerInfo_t const * const DSInfo,
                          struct sockaddr const * const addr, 
                          const char *recvBuffer, uint32_t bytesReceived)
{
  if( DSInfo == NULL || addr == NULL || recvBuffer == NULL || bytesReceived <= 0 )
    return;
  
  const char *IPAddr = getSockAddrIn(addr, false, DSInfo->newClient->isIPv4, true, true);
  const uint16_t port = getSockAddrPort(addr, DSInfo->newClient->isIPv4);
  if (saveInfo.serverPrintData == 3)  // 显示信息 
    SafePrintf("UDP Recv IPv%d %s:%d Len:%d Data: %s\n", 
        DSInfo->newClient->isIPv4? 4:6, IPAddr, port, bytesReceived, recvBuffer);
  
  if (strnicmp(recvBuffer, "discover_com2tcp_server", strlen("discover_com2tcp_server")) == 0) {
    EnterCriticalSection_Wrapper(&DSInfo->newClient->mt);
    sendDiscoveryResponse(DSInfo->socket, addr, DSInfo->newClient->isIPv4);
    LeaveCriticalSection_Wrapper(&DSInfo->newClient->mt);
  }
  else if (strnicmp(recvBuffer, CONTROL_HEADER"errorTest", strlen(CONTROL_HEADER"errorTest")) == 0) 
    ErrorCodeTest();  // 异常测试钩子
  else if (strnicmp(recvBuffer, CONTROL_HEADER, strlen(CONTROL_HEADER)) == 0){
    HandleClientCommand(&DSInfo->socket, recvBuffer + strlen(CONTROL_HEADER));
  }
  else if( strnicmp(recvBuffer, broadcastTestMessage, strlen(broadcastTestMessage)) == 0 ){ 
    bool *isTest = DSInfo->newClient->isIPv4? &DSInfo->newClient->testV4: &DSInfo->newClient->testV6;
    if( *isTest == false )
      *isTest = true;
    static uint16_t testCount = 0;
    static bool allTest = false;
    testCount++;
    SafePrintf("%s IPv%d Thread, New IPv%d Test OK! MsgLen:%d, [IPv4:%s IPv6:%s] Number:%-5d %c", 
        broadcastTestMessage, DSInfo->IPvNum, 
        DSInfo->newClient->isIPv4? 4:6, bytesReceived, 
        DSInfo->newClient->testV4? "Yes":"No ",
        DSInfo->newClient->testV6? "Yes":"No ", 
        testCount, allTest? '\r':'\n');
    allTest = DSInfo->newClient->testV4 && DSInfo->newClient->testV6;
  }
}

static void sendDiscoveryResponse(socket_t socket, struct sockaddr const * const addr, bool isIPv4)
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
  
#if 0
  // 什么网卡接收就什么网卡回复
  socklen_t sockLen = isIPv4? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
//int sendResult = sendto(socket, responseString, strlen(responseString), 0, addr, sockLen);
#endif
  const char *IPaddr = getSockAddrIn(addr, false, isIPv4, false, false);
  int sendResult = sendUdpMessage(socket, port, IPaddr, isIPv4, responseString, strlen(responseString));

  IPaddr = getSockAddrIn(addr, false, isIPv4, true, true);
  SafePrintf("Discovery Response Send To IPv%d %s:%d %s:%ld Count:%-5d%c", isIPv4? 4:6,
            IPaddr, port, sendResult == SOCKET_ERROR? "Failed" : "Succeed",
            GetLastError(), ++count, isRunningAsService()? '\n' : '\r');
}

int DiscoveryServiceSend(socket_t sock, const char *buf, uint32_t len)
{
  if( sock == INVALID_SOCKET_VALUE )
    return -1;
  if( isDiscoveryServiceSocket( sock ) == false )
    return -2;
    
  socklen_t socklen = 0;

  if( IPv4DS.socket == sock )
    socklen = sizeof(struct sockaddr_in); 
 
  if( IPv6DS.socket == sock )
    socklen = sizeof(struct sockaddr_in6); 
  
  if( socklen == 0 )
    return -3;

  struct sockaddr const * addr = (struct sockaddr*)&newClientInfo.addr;
  // SafePrintf("UDP Send %d Bytes To %s:%d\n", len, 
  //   getSockAddrIn(addr, true, IPDS->IPvNum == 4, true, true), 
  //   getSockAddrPort(addr, newClientInfo.isIPv4) );
  return sendto(sock, buf, len, 0, addr, socklen);
}



static void sendIPv4BroadcastTestMessage(void *arg)
{
  socket_t sock = *((socket_t*)(((ThreadPoolArgWrapper*)arg)->arg));
  uint16_t port = getSocketBindingPort(sock, true);
  sendUdpMessage(sock, port, "255.255.255.255", true,
    broadcastTestMessage, strlen(broadcastTestMessage) ); 
}

static void sendIPv6MulticastTestMessage(void *arg)
{
  socket_t sock = *((socket_t*)(((ThreadPoolArgWrapper*)arg)->arg));
  uint16_t port = getSocketBindingPort(sock, true); 
  sendUdpMessage(sock, port, DISCOVERY_IPV6_MULTICAST, false,
    broadcastTestMessage, strlen(broadcastTestMessage) ); 
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
 * @brief  获取套接字绑定的端口号
 * @param  socket             套接字套接字
 * @param  isIPv4             是否为 IPv4 地址 
 * @return 端口号，返回0代表无法获取正确的端口号
 * @note  本函数用于获取套接字绑定的端口号，用于判断是否为正确的端口号。
 *============================================================================*/
static uint16_t getSocketBindingPort(socket_t socket, bool isIPv4)
{
  if( socket == INVALID_SOCKET_VALUE )
    return 0;
  struct sockaddr_storage localAddr;
  socklen_t addrLen = sizeof localAddr;
  int ret = getsockname(socket, (struct sockaddr*)&localAddr, &addrLen);
  if (ret != 0)
    return 0;

  if (isIPv4) 
      return ntohs(((struct sockaddr_in*)&localAddr)->sin_port);
  else 
      return ntohs(((struct sockaddr_in6*)&localAddr)->sin6_port); 
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
