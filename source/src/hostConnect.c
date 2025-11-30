/******************************************************************************
  * @file    文件 hostConnect.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 连接远端服务器，域名解析，获取本机IP
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "hostConnect.h"
#include "logPrint.h"
#include "public.h"


#include <string.h>

#ifdef _WIN32
// 必须在包含头文件之前定义 Windows 版本
#define _WIN32_WINNT 0x0600  // Windows Vista 或更高版本
#include <ws2tcpip.h>
#else
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <fcntl.h>
#endif

/*================== 本地数据类型   =========================================*/
/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量   =========================================*/
/*================== 本地常量声明   =========================================*/
/*================== 本地变量声明   =========================================*/
/*================== 本地函数声明   =========================================*/
/*================== 外部函数声明   =========================================*/
/*================== 外部变量声明   =========================================*/

/**
 * @brief 域名解析函数
 * @param hostname 域名或IP地址
 * @param ipBuffer 存储解析结果的缓冲区
 * @param bufferSize 缓冲区大小
 * @param retErr    具体错误值
 * @return   0 成功
 *          -1 域名解析失败
 *          -2 没有为主机名找到有效的IP地址
 */
int8_t resolveHostname(const char* hostname, char* ipBuffer, uint8_t bufferSize, int * retErr)
{ 
  struct addrinfo hints, *result = NULL, *ptr = NULL;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;     // 支持IPv4和IPv6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  
  // 解析域名
  int ret = getaddrinfo(hostname, NULL, &hints, &result);
  if (ret != 0){
    if( retErr ) *retErr = ret;
    return -1; 
  }

  // 遍历结果，优先选择IPv4
  for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
    void* addr;
    char ipstr[46] = {0};
    
    if (ptr->ai_family == AF_INET) { // IPv4
        struct sockaddr_in* ipv4 = (struct sockaddr_in*)ptr->ai_addr;
        addr = &(ipv4->sin_addr);
    } 
    else { // IPv6
        struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)ptr->ai_addr;
        addr = &(ipv6->sin6_addr);
    }
    
    // 转换IP地址为字符串
    inet_ntop(ptr->ai_family, addr, ipstr, sizeof(ipstr));
    
    // 优先选择IPv4地址
    if (ptr->ai_family == AF_INET) {
        strncpy(ipBuffer, ipstr, bufferSize - 1);
        ipBuffer[bufferSize - 1] = '\0';
        freeaddrinfo(result);
        return 0;
    }
  }
  
  // 如果没有IPv4，使用第一个找到的地址
  if (result != NULL) {
    void* addr;
    if (result->ai_family == AF_INET) {
        struct sockaddr_in* ipv4 = (struct sockaddr_in*)result->ai_addr;
        addr = &(ipv4->sin_addr);
    } 
    else {
        struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)result->ai_addr;
        addr = &(ipv6->sin6_addr);
    }
    
    inet_ntop(result->ai_family, addr, ipBuffer, bufferSize - 1);
    ipBuffer[bufferSize - 1] = '\0';
    freeaddrinfo(result);
    return 0;
  }
  
  freeaddrinfo(result);
  if( retErr ) *retErr = -2;
  return -2;
}

bool startConnectToServer(const char* host, uint16_t port, 
        uint16_t timeoutMs, socket_t *retSocket, char *retIP)
{
  char resolvedIP[46] = {0};

  if( retSocket == NULL || retIP == NULL )
    return false;
  
  // 解析域名或IP地址
  if ( 0 != resolveHostname(host, resolvedIP, sizeof resolvedIP, NULL ))  
    return false;
  
  // 创建socket
  *retSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (*retSocket == INVALID_SOCKET_VALUE) { 
      SafePrintf("Socket creation failed: %ld\n", GetLastError());
      return false;
  }
  
  // 设置非阻塞模式
#ifdef _WIN32
  u_long mode = 1;
  if (ioctlsocket(*retSocket, FIONBIO, &mode) != 0) {
#else
  int flags = fcntl(*retSocket, F_GETFL, 0);
  if (fcntl(*retSocket, F_SETFL, flags | O_NONBLOCK) == -1) {
#endif
      SafePrintf("Set non-blocking failed: %ld\n", GetLastError());
      closeSocket(*retSocket);
      *retSocket = INVALID_SOCKET_VALUE;
      return false;
  }
  
  // 设置服务器地址
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = inet_addr(resolvedIP);
  
  // 连接服务器
  int connectRet = connect(*retSocket, (struct sockaddr*)&server_addr, sizeof server_addr);
  if ( connectRet == SOCKET_ERROR ) {
      int error = GetLastError();
#ifdef _WIN32
      if (error != WSAEWOULDBLOCK) {
#else
      if (error != EINPROGRESS) {
#endif
        SafePrintf("Connect failed: %d\n", error);
        closeSocket(*retSocket);
        *retSocket = INVALID_SOCKET_VALUE;
        return false;
      }
  }

  // 处理非阻塞连接
  fd_set writefds;
  FD_ZERO(&writefds);
  FD_SET(*retSocket, &writefds);
  
  struct timeval timeout;
  timeout.tv_sec = timeoutMs / 1000;
  timeout.tv_usec = (timeoutMs % 1000) * 1000;
  
  int selectResult = select(*retSocket + 1, NULL, &writefds, NULL, &timeout);
  if (selectResult <= 0) {
      SafePrintf("\rConnection timeout or error: %-5d", selectResult);
      closeSocket(*retSocket);
      *retSocket = INVALID_SOCKET_VALUE;
      return false;
  }
  
  // 检查socket是否真的连接成功
  int error = 0;
  socklen_t errorLen = sizeof error;
  int retSockopt = getsockopt(*retSocket, SOL_SOCKET, SO_ERROR, (char*)&error, &errorLen);
  if (retSockopt == SOCKET_ERROR || error != 0) {
      SafePrintf("\rConnection failed: %-5d", error);
      closeSocket(*retSocket);
      *retSocket = INVALID_SOCKET_VALUE;
      return false;
  }
  
  strcpy(retIP, resolvedIP); 
  return true;
}

// 获取与客户端相同网段的IP地址
const char *GetMatchingSubnetIP(struct sockaddr_in* clientAddr)
{
  struct sockaddr_in tempAddr;
  socklen_t tempAddrLen = sizeof tempAddr;
  
  // 创建一个临时socket来获取本地接口信息
  socket_t tempSocket = socket(AF_INET, SOCK_DGRAM, 0);
  if (tempSocket == INVALID_SOCKET_VALUE)
      return "127.0.0.1";

  // 连接到客户端地址，系统会自动选择正确的本地接口
  if (connect(tempSocket, (struct sockaddr*)clientAddr, sizeof(*clientAddr)) == SOCKET_ERROR) {
      closeSocket(tempSocket);
      return "127.0.0.1";
  }
  
  // 获取socket的本地地址（这就是与客户端通信的接口地址）
  int ret = getsockname(tempSocket, (struct sockaddr*)&tempAddr, &tempAddrLen);
  
  static char retMyIP[20];
  memset(retMyIP, 0, sizeof retMyIP);
  strcpy(retMyIP, ret == 0? inet_ntoa(tempAddr.sin_addr): "127.0.0.1");

  closeSocket(tempSocket);
  return retMyIP; 
}

// 选择与客户端相同网段的IP
const char *SelectMatchingSubnetIP(const char *clientAddr)
{ 
  uint8_t ipCount = 0;
  char localIPs[25][20];
  static char retMyIP[20];
  memset(localIPs, 0, sizeof localIPs);
  memset(retMyIP, 0, sizeof retMyIP);

  GetAllLocalIPs(localIPs, &ipCount, 25);
  
  if (ipCount == 0) 
      return "127.0.0.1" ;  

  // 如果只有一个IP，直接使用
  if (ipCount == 1) {
      strcpy(retMyIP, localIPs[0]);
      return retMyIP;
  }
  
  // 获取客户端IP的网段
  char clientIP[16];
  strcpy(clientIP, clientAddr);
  
  // 提取客户端IP的前三段（网段）
  char clientSubnet[16] = {0};
  char* dot = strrchr(clientIP, '.');
  if (dot) 
    strncpy(clientSubnet, clientIP, dot - clientIP);
  
  // 寻找匹配网段的本地IP
  for (int i = 0; i < ipCount; i++) {
      char localSubnet[16] = {0};
      dot = strrchr(localIPs[i], '.');
      if (dot == 0) 
        continue;

      strncpy(localSubnet, localIPs[i], dot - localIPs[i]);
      if (strcmp(clientSubnet, localSubnet) == 0) {
          strcpy(retMyIP, localIPs[i]);
          return retMyIP;
      }
  }
  
  // 如果没有找到匹配网段的IP，使用第一个非回环IP
  strcpy(retMyIP, localIPs[0]);

  return retMyIP;
}

bool getSockfdPeerInfo(int sockfd, char *retIPstr, uint16_t *retPort) 
{
  struct sockaddr_in peer_addr;
  socklen_t addr_len = sizeof(peer_addr);
  int ret = getpeername(sockfd, (struct sockaddr*)&peer_addr, &addr_len);
 
  if( ret == 0 && retIPstr)
    inet_ntop(AF_INET, &peer_addr.sin_addr, retIPstr, INET6_ADDRSTRLEN);
  if( ret == 0 && retPort)
    *retPort = ntohs(peer_addr.sin_port);
  return ret == 0? true:false;
}