/******************************************************************************
  * @file    文件 serverListen.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 服务器监听，接受其他客户端连接
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <netinet/tcp.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#include "main.h"
#include "log.h"
#include "serverListen.h"

/*================== 本地宏定义     =========================================*/
#ifdef _WIN32
typedef int socklen_t;
#endif

/*================== 本地宏定义     =========================================*/
#define MIN_USER_PORT   1024
#define MAX_PORT        65535

/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/
static uint16_t FindAvailablePort(uint16_t startPort);

/*=============================================================================
 功   能：服务器资源清理函数
 参   数：server  服务器信息结构体指针
 返   回：无
 注   意：只关闭监听socket，不关闭newSocket
 说   明：专门解决Linux端口占用问题
=============================================================================*/
void serverCleanup(serverInfo_t *server)
{
    if (server == NULL || server->socket == INVALID_SOCKET_VALUE || server->socket == 0)
      return;

    // 只处理服务器监听socket，不管newSocket（由其他地方管理） 
    SafePrintf("Closing server Port %d ", server->port);
        
#ifndef _WIN32
    // Linux下确保TCP连接完全终止
    shutdown(server->socket, SHUT_RDWR);
    
    // 设置非阻塞模式，确保close立即返回
    int flags = fcntl(server->socket, F_GETFL, 0);
    fcntl(server->socket, F_SETFL, flags | O_NONBLOCK);
#endif
    // 关闭socket
    closeSocket(server->socket);
    server->socket = INVALID_SOCKET_VALUE;
    server->port = 0;   // 重置服务器端口信息
    SafePrintf(" has been released\n");
}

// 修改 serverStart 函数支持双栈
bool serverStart(serverInfo_t *server)
{
  if (server == NULL)
    return false;

  // 查找可用端口
  server->port = FindAvailablePort(server->port);
  if (server->port == 0) {
    SafePrintf("No available port found\n");
    return false;
  }

  // 创建服务器套接字 - 使用 IPv6 双栈
  // 注意：AF_INET6 默认支持 IPv4 映射（需要设置 IPV6_V6ONLY=0）
  server->socket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  if (server->socket == INVALID_SOCKET_VALUE) {
    // 如果 IPv6 不可用，回退到 IPv4
    server->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->socket == INVALID_SOCKET_VALUE) {
      SafePrintf("Error at socket(): %ld\n", GetLastError());
      return false;
    }
    server->isIPv6 = false;// 标记为 IPv4 only
  } 
  else {
    // 关闭 IPv6 V6ONLY，允许双栈
    int v6only = 0;
    if (setsockopt(server->socket, IPPROTO_IPV6, IPV6_V6ONLY, 
                    (char*)&v6only, sizeof(v6only)) == SOCKET_ERROR) {
      SafePrintf("Set IPV6_V6ONLY failed: %ld\n", GetLastError());
    }
    server->isIPv6 = true;
  }

  // 设置 SO_REUSEADDR
  int reuse = 1;
  if (setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, 
                  (char*)&reuse, sizeof(reuse)) == SOCKET_ERROR) {
    SafePrintf("Set SO_REUSEADDR failed: %ld\n", GetLastError());
  }
  
#ifndef _WIN32
  struct linger ling = {1, 0};
  if (setsockopt(server->socket, SOL_SOCKET, SO_LINGER, 
                  &ling, sizeof(ling)) == SOCKET_ERROR) {
    SafePrintf("Set SO_LINGER failed: %ld\n", GetLastError());
  }
#endif

  // 禁用 Nagle
  int nagleStatus = true;
  int result = setsockopt(server->socket, IPPROTO_TCP, TCP_NODELAY,
                          (char*)&nagleStatus, sizeof nagleStatus);
  if (result < 0) 
    SafePrintf("Disable Nagle Failed, result %d, error: %ld\n", result, GetLastError());

  // 绑定套接字 - 使用 sockaddr_storage
  struct sockaddr_storage service;
  socklen_t addr_len;
  
  memset(&service, 0, sizeof service);
  if (server->isIPv6) {
    struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&service;
    addr6->sin6_family = AF_INET6;
    addr6->sin6_addr = in6addr_any;
    addr6->sin6_port = htons(server->port);
    addr_len = sizeof(struct sockaddr_in6);
  } else {
    struct sockaddr_in* addr4 = (struct sockaddr_in*)&service;
    addr4->sin_family = AF_INET;
    addr4->sin_addr.s_addr = INADDR_ANY;
    addr4->sin_port = htons(server->port);
    addr_len = sizeof(struct sockaddr_in);
  }

  if (bind(server->socket, (struct sockaddr*)&service, addr_len) == SOCKET_ERROR) {
    SafePrintf("bind failed with error: %ld\n", GetLastError());
    closeSocket(server->socket);
    return false;
  }

  if (listen(server->socket, SOMAXCONN) == SOCKET_ERROR) {
    SafePrintf("listen failed with error: %ld\n", GetLastError());
    closeSocket(server->socket);
    return false;
  }

  SafePrintf("Server listening on port %d, %s\n", server->port, 
              server->isIPv6 ? "IPv4/IPv6 dual-stack" : "IPv4 only");
  return true;
}


/*=============================================================================
 功   能：监听新客户端连接
 参   数：server  --> 服务端信息
					timeout --> 监听超时时间，单位秒
 返   回：-2  请传递有效的服务端结构体
          -1  这个服务端套接字是无效的，建议重新创建服务端套接字
           0  则是有新的客户端连接
      大于 0  的话请重新监听
 描   述：无
=============================================================================*/
int8_t listenNewClientConnect(serverInfo_t *server, uint8_t timeOut)
{    
  if( server == NULL ) 
    return -2;
 
  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(server->socket, &readSet);

  struct timeval timeout;
  timeout.tv_sec = timeOut;
  timeout.tv_usec = 0;

  int selRet = select(server->socket + 1, &readSet, NULL, NULL, &timeout);
  if (selRet == 0) 
      return 1; // 继续监听
  else if (selRet == SOCKET_ERROR) {
    SafePrintf("select failed, error=%ld\n", GetLastError());
    return -1;  // 无效的服务器套接字
  }

  if (!FD_ISSET(server->socket, &readSet)) 
    return 2; // 继续监听

  // 接受客户端连接
  struct sockaddr_storage clientAddr;  // 改用通用结构
  socklen_t addrLen = sizeof clientAddr ; 
  socket_t clientSocket = accept(server->socket, (struct sockaddr*)&clientAddr, &addrLen); 
  if (clientSocket == INVALID_SOCKET_VALUE) {
      SafePrintf("accept failed, Error=%ld\n", GetLastError() ); 
      return 3; // 继续监听
  }

  // 获取客户端IP地址
  char clientIP[INET6_ADDRSTRLEN];  // Windows 下这个宏值为 65
  memset(clientIP, 0, sizeof clientIP);

  if (clientAddr.ss_family == AF_INET) {
      struct sockaddr_in* addr4 = (struct sockaddr_in*)&clientAddr;
      inet_ntop(AF_INET, &addr4->sin_addr, clientIP, sizeof clientIP);
  } 
  else if (clientAddr.ss_family == AF_INET6) {
      struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&clientAddr;
      // 检查是否为 IPv4 映射地址
      if (IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr)) {
          struct in_addr addr4;
          memcpy(&addr4, &addr6->sin6_addr.s6_addr[12], 4);
          inet_ntop(AF_INET, &addr4, clientIP, sizeof clientIP);
      }
      else 
          inet_ntop(AF_INET6, &addr6->sin6_addr, clientIP, sizeof clientIP);
      
  } else {
    SafePrintf("Get IP Addr Failed! , Error:%ld\n", GetLastError());
    strcpy(clientIP, "Unknown");
    closeSocket(clientSocket);
    return 4; // 没有IP地址的不要，继续监听
  }
  
  memset(server->newIP, 0, sizeof server->newIP);
  strcpy(server->newIP,   clientIP );
  
  server->newSocket = clientSocket;
  return 0; // 有新的客户端连接
}

// 解析命令行参数获取端口号
// 参数: argc - 参数个数, argv - 参数数组, defaultPort - 默认端口号
// 返回值: 解析成功的端口号，如果无效则返回0
uint16_t ParsePortParameter(int argc, char const* argv[]) 
{
  for (int i = 1; i < argc; i++) {
      // 检查参数是否以-p或-P开头
      if ((argv[i][0] == '-' || argv[i][0] == '/') && 
          tolower(argv[i][1]) == 'p' && 
          argv[i][2] != '\0') {
          
          // 获取端口号部分
          char const * portStr = &argv[i][2];
          char* endPtr;
          long port = strtol(portStr, &endPtr, 10);
          
          // 验证转换是否成功
          if (*endPtr != '\0') {
              fprintf(stderr, "错误: 端口号 '%s' 包含非数字字符\n", portStr);
              return 0;
          }
          
          // 检查端口范围
          if (port <= MIN_USER_PORT) {
              fprintf(stderr, "错误: 端口号必须大于 %d (当前: %ld)\n", MIN_USER_PORT, port);
              return 0;
          }
          
          if (port > MAX_PORT) {
              fprintf(stderr, "错误: 端口号不能超过 %d (当前: %ld)\n", MAX_PORT, port);
              return 0;
          }
          
          return (uint16_t)port;
      }
      // 支持格式: -p 5000 (带空格)
      else if ((argv[i][0] == '-' || argv[i][0] == '/') && 
                tolower(argv[i][1]) == 'p' && 
                argv[i][2] == '\0' && 
                i + 1 < argc) {
          
          char const* portStr = argv[i+1];
          char* endPtr;
          long port = strtol(portStr, &endPtr, 10);
          
          if (*endPtr != '\0') {
              fprintf(stderr, "错误: 端口号 '%s' 包含非数字字符\n", portStr);
              return 0;
          }
          
          if (port <= MIN_USER_PORT) {
              fprintf(stderr, "错误: 端口号必须大于 %d (当前: %ld)\n", MIN_USER_PORT, port);
              return 0;
          }
          
          if (port > MAX_PORT) {
              fprintf(stderr, "错误: 端口号不能超过 %d (当前: %ld)\n", MAX_PORT, port);
              return 0;
          }
          
          return (uint16_t)port;
      }
  }
  
  // 没有指定-p参数，返回默认端口
  return DEFAULT_PORT;
}

// 从指定端口开始查找100个可用端口，返回0是无效端口
static uint16_t FindAvailablePort(uint16_t startPort) 
{
  for (uint16_t port = startPort; port < startPort + 100; port++) {
    bool portAvailable = false;
    
    // 优先尝试IPv6（能同时检测IPv4和IPv6占用）
    socket_t testSocket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (testSocket != INVALID_SOCKET_VALUE) {
      struct sockaddr_in6 service6;
      memset(&service6, 0, sizeof service6);
      service6.sin6_family = AF_INET6;
      service6.sin6_addr = in6addr_any;
      service6.sin6_port = htons(port);
      
      // 关键：让IPv6套接字也处理IPv4
      int v6only = 0;
      setsockopt(testSocket, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&v6only, sizeof v6only);
      
      if (bind(testSocket, (struct sockaddr*)&service6, sizeof service6) == 0)
          portAvailable = true;
      
      closeSocket(testSocket);
      
      if (portAvailable)
          return port;
      
      continue;  // IPv6绑定失败，直接下一个端口
    }
    
    // IPv6不可用，回退到IPv4
    testSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (testSocket == INVALID_SOCKET_VALUE) 
      break;
    
    struct sockaddr_in service;
    memset(&service, 0, sizeof service);
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = htonl(INADDR_ANY);
    service.sin_port = htons(port);
    
    if (bind(testSocket, (struct sockaddr*)&service, sizeof service) == 0)
      portAvailable = true;
    
    closeSocket(testSocket);
    
    if (portAvailable) 
        return port;
  }
  
  return 0;
}
