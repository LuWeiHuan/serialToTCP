 /******************************************************************************
  * @file    文件 serverListen.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 服务器监听，接收其他客户端连接
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
#include <winsock2.h>
#include <windows.h>

#include "main.h"
#include "logPrint.h"
#include "public.h"
#include "client.h"
#include "serverListen.h"

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/
static uint16_t FindAvailablePort(uint16_t startPort);

/*================== 外部函数和变量声明    ==================================*/

bool serverInit(serverInfo_t *server)
{
  if( server == NULL )
    return 0;


  // 查找可用端口
  server->port = FindAvailablePort(server->port);
  if (server->port == 0) {
    SafePrintf("No available port found\n");
    return false;
  }

  // 创建服务器套接字
  server->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (server->socket == INVALID_SOCKET) {
      SafePrintf("Error at socket(): %d\n", WSAGetLastError());
      return false;
  }

  // 禁用Nagle算法
  char nagleStatus = 0;
  int result = setsockopt(server->socket, //socket的文件描述符
                          IPPROTO_TCP,
                          TCP_NODELAY,
                          &nagleStatus, 
                          sizeof(int));    // 1 - on, 0 - off
  if (result < 0)
    SafePrintf("disable Nagle failed : %d\n", result);

  // 绑定套接字
  struct sockaddr_in service;
  service.sin_family = AF_INET;
  service.sin_addr.s_addr = INADDR_ANY;
  service.sin_port = htons(server->port);

  if (bind(server->socket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
      SafePrintf("bind failed with error: %d\n", WSAGetLastError());
      closesocket(server->socket);
      return false;
  }

  // 监听
  if (listen(server->socket, SOMAXCONN) == SOCKET_ERROR) {
      SafePrintf("listen failed with error: %d\n", WSAGetLastError());
      closesocket(server->socket);
      return false;
  }

  return true;
}

/*=============================================================================
 功   能：监听新客户端连接
 参   数：ServerSocket  --> 服务端套接字
					retSocket		  --> 有新的客户端连接这里会返回客户端套接字
					retIP 	      --> 有新的客户端连接这里会返回客户端IP 
 返   回：-2  请传递有效的服务端结构体
          -1  这个服务端套接字是无效的，建议重新创建服务端套接字
           0  则是有新的客户端连接
      大于 0  的话请重新监听
 描   述：无
=============================================================================*/
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

  int selRet = select(0, &readSet, NULL, NULL, &timeout);
  if (selRet == 0) 
      return 1; // 继续监听
  else if (selRet == SOCKET_ERROR) {
    SafePrintf("select failed, error=%d\n", WSAGetLastError());
    return -1;  // 无效的服务器套接字
  }

  if (!FD_ISSET(server->socket, &readSet)) 
    return 2; // 继续监听

  // 接受客户端连接
  struct sockaddr_in clientAddr;
  int addrLen = sizeof clientAddr;
  SOCKET clientSocket = accept(server->socket, (struct sockaddr*)&clientAddr, &addrLen);
  if (clientSocket == INVALID_SOCKET) {
      SafePrintf("accept failed, error=%d\n", WSAGetLastError());
      return 3; // 继续监听
  }

  // 获取客户端IP地址
  char *clientIP = inet_ntoa( clientAddr.sin_addr );
  memset(server->newIP, 0, sizeof server->newIP);
  strcpy(server->newIP, clientIP != NULL ? clientIP:"Unknown");
  server->newSocket = clientSocket;
  return 0; // 有新的客户端连接
}





#define MIN_USER_PORT   1024
#define MAX_PORT        65535

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
  int bindRet;
  struct sockaddr_in service;
  uint16_t port = startPort;
  for (port = startPort; port < startPort + 100; port++) {
    SOCKET testSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (testSocket == INVALID_SOCKET)
      break;

    service.sin_family = AF_INET;
    service.sin_addr.s_addr = INADDR_ANY;
    service.sin_port = htons(port);

    bindRet = bind(testSocket, (SOCKADDR*)&service, sizeof(service));
    closesocket(testSocket);
    if( bindRet != SOCKET_ERROR)
      return port;
    
  }

  return 0;
}
