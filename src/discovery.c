/******************************************************************************
  * @file    文件 discovery.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 UDP服务发现功能
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
// 必须在包含头文件之前定义 Windows 版本
#define _WIN32_WINNT 0x0600  // Windows Vista 或更高版本

#include "discovery.h"
#include "main.h"
#include "logPrint.h"
#include "public.h"
#include "clients.h"
#include "Command.h"
#include "hostConnect.h"

#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <ws2tcpip.h>  // 添加用于域名解析的头文件

/*================== 本地数据类型   =========================================*/
/*================== 本地宏定义     =========================================*/
#define DISCOVERY_INTERVAL_MS  1000        // 发现请求检查间隔
#define RESPONSE_BUFFER_SIZE   256         // 响应缓冲区大小

/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static volatile BOOL discoveryRunning = FALSE;
static HANDLE hDiscoveryThread = NULL;
static SOCKET discoverySocket = INVALID_SOCKET;
static CRITICAL_SECTION csDiscovery;
static struct sockaddr_in newClientInfo;

 
/*================== 本地函数声明    ========================================*/
static void DiscoveryServiceStart(void);
static void DiscoveryServiceStop(void);

static DWORD WINAPI DiscoveryThread(LPVOID lpParam);
static BOOL InitializeDiscoverySocket(void);
static void SendDiscoveryResponse(struct sockaddr_in* clientAddr);


/*================== 外部函数和变量声明    ==================================*/

void DiscoveryService(bool start)
{
  if( start )
    DiscoveryServiceStart();
  else
    DiscoveryServiceStop();
}

SOCKET getDiscoverySocket(void)
{
  return discoverySocket;
}

const char *getDiscoveryNewClientIPAddr(void)
{
  static char IPaddr[50];
  memset(IPaddr, 0, sizeof IPaddr); 
  return inet_ntop(AF_INET, &newClientInfo.sin_addr, IPaddr, INET_ADDRSTRLEN) 
          == NULL? "unknown IP": IPaddr;
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
  
  InitializeCriticalSection(&csDiscovery);
  
  if (!InitializeDiscoverySocket()) {
    SafePrintf("Failed to initialize discovery socket\n");
    return;
  }

  hDiscoveryThread = CreateThread(NULL, 0, DiscoveryThread, NULL, 0, NULL);
  discoveryRunning = hDiscoveryThread? TRUE:FALSE;
  if (hDiscoveryThread == NULL) {
    closesocket(discoverySocket);
    discoverySocket = INVALID_SOCKET; 
    SafePrintf("Failed to create discovery thread\n");
  }
}

// 停止发现服务
static void DiscoveryServiceStop(void)
{
  if (!discoveryRunning)
    return;
  discoveryRunning = FALSE;

  // 关闭套接字促使线程退出
  if (discoverySocket != INVALID_SOCKET) {
    closesocket(discoverySocket);
    discoverySocket = INVALID_SOCKET;
  }
  
  if (hDiscoveryThread) {
    WaitForSingleObject(hDiscoveryThread, 1000);
    CloseHandle(hDiscoveryThread);
    hDiscoveryThread = NULL;
  }

  DeleteCriticalSection(&csDiscovery);
  SafePrintf("Discovery service stopped\n");
}


// 初始化发现Socket
static BOOL InitializeDiscoverySocket(void)
{ 
  discoverySocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (discoverySocket == INVALID_SOCKET) {
    SafePrintf("Discovery socket creation failed: %d\n", WSAGetLastError());
    return FALSE;
  }

  // 设置Socket选项：允许广播和地址重用
  BOOL broadcast = TRUE;
  if (setsockopt(discoverySocket, SOL_SOCKET, SO_BROADCAST, 
                (char*)&broadcast, sizeof(broadcast)) == SOCKET_ERROR) {
    SafePrintf("Set SO_BROADCAST failed: %d\n", WSAGetLastError());
    closesocket(discoverySocket);
    discoverySocket = INVALID_SOCKET;
    return FALSE;
  }

  BOOL reuseAddr = TRUE;
  if (setsockopt(discoverySocket, SOL_SOCKET, SO_REUSEADDR, 
                (char*)&reuseAddr, sizeof(reuseAddr)) == SOCKET_ERROR) {
    SafePrintf("Set SO_REUSEADDR failed: %d\n", WSAGetLastError());
  }

  // 绑定到发现端口
  struct sockaddr_in serverAddr;
  memset(&serverAddr, 0, sizeof(serverAddr));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serverAddr.sin_port = htons(DISCOVERY_PORT);

  if (bind(discoverySocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
    SafePrintf("Discovery bind failed: %d\n", WSAGetLastError());
    closesocket(discoverySocket);
    discoverySocket = INVALID_SOCKET;
    return FALSE;
  }

  // 设置非阻塞模式
  u_long nonBlocking = 1;
  if (ioctlsocket(discoverySocket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
    SafePrintf("Set non-blocking failed: %d\n", WSAGetLastError());
    closesocket(discoverySocket);
    discoverySocket = INVALID_SOCKET;
    return FALSE;
  }

  return TRUE;
}

// 发现服务线程
static DWORD WINAPI DiscoveryThread(LPVOID lpParam)
{
  (void)lpParam; 
  int clientAddrLen = sizeof newClientInfo;
  char recvBuffer[256];
  int bytesReceived, selectResult;
  fd_set readSet;
  struct timeval timeout; 
  
  char DiscoveryServerString[50];
  memset(DiscoveryServerString, 0, sizeof DiscoveryServerString);
  snprintf(DiscoveryServerString, sizeof DiscoveryServerString, 
    "Discovery PROT:%d", DISCOVERY_PORT);
  
  SafePrintf("Discovery service thread started on UDP port %d\n", DISCOVERY_PORT);
  
  while (discoveryRunning) {
    // 更新控制台标题显示发现服务状态
    updataConsoleTitle(DiscoveryServerString);

    FD_ZERO(&readSet);
    FD_SET(discoverySocket, &readSet);

    timeout.tv_sec = 0;
    timeout.tv_usec = DISCOVERY_INTERVAL_MS * 1000; // 转换为微秒

    selectResult = select(0, &readSet, NULL, NULL, &timeout);
    if (selectResult == SOCKET_ERROR) {
        SafePrintf("Discovery select error: %d\n", WSAGetLastError());
        Sleep(DISCOVERY_INTERVAL_MS);
        continue;
    }

    if (selectResult == 0 || FD_ISSET(discoverySocket, &readSet) == 0)
      continue;

    // 接收发现请求
    bytesReceived = recvfrom(discoverySocket, recvBuffer, sizeof(recvBuffer)
                    - 1, 0, (struct sockaddr*)&newClientInfo, &clientAddrLen);
    
    if (bytesReceived == 0) 
      continue;
    recvBuffer[bytesReceived] = '\0';
    
    // 检查是否是有效的发现请求
    if (strnicmp(recvBuffer, "discover_com2tcp_server", strlen("discover_com2tcp_server")) == 0){
      SendDiscoveryResponse(&newClientInfo); // 发送响应
    }
    else if (strnicmp(recvBuffer, CTRL_HEADER, strlen(CTRL_HEADER)) == 0){
      if (runInfo.serverPrintData == 3)
        SafePrintf("UDP [%s]:%d CMD: %-60s\n", inet_ntoa(newClientInfo.sin_addr), 
                ntohs(newClientInfo.sin_port), recvBuffer);
      
      // 连接UDP套接字到特定服务器，方便使用send发送数据
      connect(discoverySocket, (struct sockaddr*)&newClientInfo, sizeof newClientInfo); 
      HandleClientCommand(&discoverySocket, recvBuffer + strlen(CTRL_HEADER));

      // 这里是进行程序异常退出捕获测试的位置，用于程序自我错误定位
      #if 0
      if( strnicmp(recvBuffer, CTRL_HEADER"errorTest", strlen(CTRL_HEADER"errorTest")) == 0 )
        for( int8_t i = -2; i < 2; i++)
          SafePrintf("开始异常除法运算 8 / %d = %d\n", i, 8/i);
      #endif
    }
  }
  
  SafePrintf("Discovery thread exiting\n");
  return 0;
}



static void SendDiscoveryResponse(struct sockaddr_in* clientAddr)
{ 
  static uint16_t count = 0;
  EnterCriticalSection(&csDiscovery);
  
  static bool getIPmethod = true;   // 获取IP的方法
  const char *getServerIP = "NULL IP"; 
  if( getIPmethod == true )   // 方法1：使用socket连接方式获取正确IP（更可靠）
    getServerIP = GetMatchingSubnetIP(clientAddr);
  else                        // 方法2：或者使用网段匹配算法
    getServerIP = SelectMatchingSubnetIP(clientAddr);
  
  // 构建响应消息
  const char *ComputerFullName = getComputerFullName();
  char responseBuffer[RESPONSE_BUFFER_SIZE]; 
  snprintf(responseBuffer, sizeof responseBuffer, "%s|%-15s|%-15s|%d|%u|%u\n",
      DISCOVERY_MAGIC, ComputerFullName, getServerIP,
      getMainServerPort(), getClientNum(), getMaxClient());
  
  LeaveCriticalSection(&csDiscovery);

  // 发送响应到客户端
  int sendResult = sendto(discoverySocket, responseBuffer, strlen(responseBuffer), 
                0, (struct sockaddr*)clientAddr, sizeof *clientAddr);

  SafePrintf("Discovery response Sent to %s:%d -> Server IP: %s:%d  %s:%d  count:%-5d\r",
            inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port),
            getServerIP, getMainServerPort(), 
            sendResult == SOCKET_ERROR? "failed":"succeed", WSAGetLastError(), ++count);
}

