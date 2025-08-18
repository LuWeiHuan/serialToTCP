 /******************************************************************************
  * @file    文件 server.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 服务器相关准备
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
#include "server.h"

/*================== 本地宏定义     =========================================*/


/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/
static int FindAvailablePort(int startPort);

/*================== 外部函数和变量声明    ==================================*/



int serverInit(int port, SOCKET *ServerSocket)
{
  if( ServerSocket == NULL )
    return 0;
  WSADATA wsaData;
  int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0) {
      SafePrintf("WSAStartup failed: %d\n", iResult);
      return 0;
  }

  // 查找可用端口
  port = FindAvailablePort(port);
  if (port == -1) {
      SafePrintf("No available port found\n");
      WSACleanup();
      return 0;
  }

  // 创建服务器套接字
  *ServerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (*ServerSocket == INVALID_SOCKET) {
      SafePrintf("Error at socket(): %d\n", WSAGetLastError());
      WSACleanup();
      return 0;
  }

  // 禁用Nagle算法
  char nagleStatus = 0;
  int result = setsockopt(*ServerSocket, //socket的文件描述符
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
  service.sin_port = htons(port);
  runInfo.port = port;

  if (bind(*ServerSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
      SafePrintf("bind failed with error: %d\n", WSAGetLastError());
      closesocket(*ServerSocket);
      WSACleanup();
      return 0;
  }

  // 监听
  if (listen(*ServerSocket, SOMAXCONN) == SOCKET_ERROR) {
      SafePrintf("listen failed with error: %d\n", WSAGetLastError());
      closesocket(*ServerSocket);
      WSACleanup();
      return 0;
  }

  return port;
}

/*=============================================================================
 功   能：监听新客户端连接
 参   数：ServerSocket  --> 服务端套接字
					retSocket		  --> 有新的客户端连接这里会返回客户端套接字
					retIP 	      --> 有新的客户端连接这里会返回客户端IP 
 返   回：-2  请传递有效的服务端套接字
          -1  这个服务端套接字是无效的，建议重新创建服务端套接字
           0  则是有新的客户端连接
      大于 0  的话请重新监听
 描   述：无
=============================================================================*/
int8_t listenNewClientLink(SOCKET *ServerSocket, SOCKET *retSocket, char *retIP)
{    
  if( ServerSocket == NULL ) 
    return -2;
 
  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(*ServerSocket, &readSet);

  struct timeval timeout;
  timeout.tv_sec = 2;
  timeout.tv_usec = 0;

  int selRet = select(0, &readSet, NULL, NULL, &timeout);
  if (selRet == 0) {
      return 1;
  }
  else if (selRet == SOCKET_ERROR) {
    SafePrintf("select failed, error=%d\n", WSAGetLastError());
    return -1;
  } 

  if (!FD_ISSET(*ServerSocket, &readSet)) 
    return 2;

    // 接受客户端连接
  struct sockaddr_in clientAddr;
  int addrLen = sizeof clientAddr;
  SOCKET clientSocket = accept(*ServerSocket, (struct sockaddr*)&clientAddr, &addrLen);
  if (clientSocket == INVALID_SOCKET) {
      SafePrintf("accept failed, error=%d\n", WSAGetLastError());
      return 3;
  }

  // 获取客户端IP地址
  char *clientIP = inet_ntoa( clientAddr.sin_addr );
  if( retIP != NULL ) 
    strcpy(retIP, clientIP != NULL ? clientIP:"Unknown");

  if( retSocket != NULL)
    *retSocket = clientSocket;
  return 0;
}





#define MIN_USER_PORT   1024
#define MAX_PORT        65535

// 解析命令行参数获取端口号
// 参数: argc - 参数个数, argv - 参数数组, defaultPort - 默认端口号
// 返回值: 解析成功的端口号，如果无效则返回-1
int ParsePortParameter(int argc, char const* argv[]) 
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
              return -1;
          }
          
          // 检查端口范围
          if (port <= MIN_USER_PORT) {
              fprintf(stderr, "错误: 端口号必须大于 %d (当前: %ld)\n", MIN_USER_PORT, port);
              return -1;
          }
          
          if (port > MAX_PORT) {
              fprintf(stderr, "错误: 端口号不能超过 %d (当前: %ld)\n", MAX_PORT, port);
              return -1;
          }
          
          return (int)port;
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
              return -1;
          }
          
          if (port <= MIN_USER_PORT) {
              fprintf(stderr, "错误: 端口号必须大于 %d (当前: %ld)\n", MIN_USER_PORT, port);
              return -1;
          }
          
          if (port > MAX_PORT) {
              fprintf(stderr, "错误: 端口号不能超过 %d (当前: %ld)\n", MAX_PORT, port);
              return -1;
          }
          
          return (int)port;
      }
  }
  
  // 没有指定-p参数，返回默认端口
  return DEFAULT_PORT;
}


static int FindAvailablePort(int startPort) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return -1;
    }

    int port = startPort;
    while (port < startPort + 100) {
        SOCKET testSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (testSocket == INVALID_SOCKET) {
            WSACleanup();
            return -1;
        }

        struct sockaddr_in service;
        service.sin_family = AF_INET;
        service.sin_addr.s_addr = INADDR_ANY;
        service.sin_port = htons(port);

        if (bind(testSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
            closesocket(testSocket);
            port++;
        } else {
            closesocket(testSocket);
            WSACleanup();
            return port;
        }
    }

    WSACleanup();
    return -1;
}
