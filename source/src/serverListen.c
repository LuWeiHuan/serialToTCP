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
#endif

#include "main.h"
#include "logPrint.h"
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

bool serverStart(serverInfo_t *server)
{
  if( server == NULL )
    return false;

  // 查找可用端口
  server->port = FindAvailablePort(server->port);
  if (server->port == 0) {
    SafePrintf("No available port found\n");
    return false;
  }

  // 创建服务器套接字
  server->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (server->socket == INVALID_SOCKET_VALUE) {
      SafePrintf("Error at socket(): %ld\n", GetLastError());
      return false;
  }

  // 关键修复：在绑定前设置SO_REUSEADDR
  int reuse = 1;
  if (setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, 
                (char*)&reuse, sizeof(reuse)) == SOCKET_ERROR) {
      SafePrintf("Set SO_REUSEADDR failed: %ld\n", GetLastError());
      // 不要立即返回，继续尝试
  }
  
  // 对于Linux，设置SO_LINGER确保快速释放端口
#ifndef _WIN32
  struct linger ling = {1, 0};  // 立即关闭，不等待
  if (setsockopt(server->socket, SOL_SOCKET, SO_LINGER, 
                &ling, sizeof(ling)) == SOCKET_ERROR) {
      SafePrintf("Set SO_LINGER failed: %ld\n", GetLastError());
  }
#endif

  int nagleStatus = true;   // 禁用Nagle算法
  int result = setsockopt(server->socket, IPPROTO_TCP, TCP_NODELAY,
                          (char*)&nagleStatus, sizeof nagleStatus);      
  if( result < 0 ) 
    SafePrintf("Disable Nagle Failed, result %d, error: %ld, nagleStatus %d\n", 
      result, GetLastError(), nagleStatus);

  // 绑定套接字
  struct sockaddr_in service;
  service.sin_family = AF_INET;
  service.sin_addr.s_addr = INADDR_ANY;
  service.sin_port = htons(server->port);

  if (bind(server->socket, (struct sockaddr*)&service, sizeof(service)) == SOCKET_ERROR) {
      SafePrintf("bind failed with error: %ld\n", GetLastError());
      closeSocket(server->socket);
      return false;
  }

  // 监听
  if (listen(server->socket, SOMAXCONN) == SOCKET_ERROR) {
      SafePrintf("listen failed with error: %ld\n", GetLastError());
      closeSocket(server->socket);
      return false;
  }

  return true;
}

int8_t listenNewClientConnect(serverInfo_t *server)
{    
  if( server == NULL ) 
    return -2;
 
  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(server->socket, &readSet);

  struct timeval timeout;
  timeout.tv_sec = 2;
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
  struct sockaddr_in clientAddr;
  socklen_t addrLen = sizeof clientAddr;
  socket_t clientSocket = accept(server->socket, (struct sockaddr*)&clientAddr, &addrLen);
  if (clientSocket == INVALID_SOCKET_VALUE) {
      SafePrintf("accept failed, error=%ld\n", GetLastError());
      return 3; // 继续监听
  }

  // 获取客户端IP地址
  char *clientIP = inet_ntoa( clientAddr.sin_addr );
  memset(server->newIP, 0, sizeof server->newIP);
  strcpy(server->newIP, clientIP? clientIP:"Unknown");
  server->newSocket = clientSocket;
  return 0; // 有新的客户端连接
}

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
    int bindRet;
    struct sockaddr_in service;
    uint16_t port = startPort;
    
    for (port = startPort; port < startPort + 100; port++) {
        socket_t testSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (testSocket == INVALID_SOCKET_VALUE)
            break;

        service.sin_family = AF_INET;
        service.sin_addr.s_addr = INADDR_ANY;
        service.sin_port = htons(port);

        bindRet = bind(testSocket, (struct sockaddr*)&service, sizeof(service));
        closeSocket(testSocket);
        
        if( bindRet != SOCKET_ERROR)
            return port;
    }

    return 0;
}
