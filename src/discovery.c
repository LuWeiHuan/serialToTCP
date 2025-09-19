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
#include "discovery.h"
#include "main.h"
#include "logPrint.h"
#include "public.h"
#include "clients.h"
#include "Command.h"

#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

/*================== 本地数据类型   =========================================*/
typedef struct {
    uint16_t serverPort;      // 服务器TCP端口
    char *serverName;         // 服务器名称
    char serverIP[20];        // 服务器名称
    uint32_t clientCount;     // 当前客户端数量
} DiscoveryInfo_t;

/*================== 本地宏定义     =========================================*/
#define DISCOVERY_INTERVAL_MS  1000        // 发现请求检查间隔
#define RESPONSE_BUFFER_SIZE   256         // 响应缓冲区大小

/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static bool getIPmethod = true;              // 获取IP的方法
static volatile BOOL discoveryRunning = FALSE;
static HANDLE hDiscoveryThread = NULL;
static SOCKET discoverySocket = INVALID_SOCKET;
static CRITICAL_SECTION csDiscovery;

static DiscoveryInfo_t discoveryInfo = {
    .serverPort = 0,
    .serverName = DISCOVERY_MAGIC,
    .serverIP = "NULL",
    .clientCount = 0,
};
/*================== 本地函数声明    ========================================*/
static void DiscoveryServiceStart(void);
static void DiscoveryServiceStop(void);

static DWORD WINAPI DiscoveryThread(LPVOID lpParam);
static BOOL InitializeDiscoverySocket(void);
static void SendDiscoveryResponse(struct sockaddr_in* clientAddr);
static void GetMatchingSubnetIP(struct sockaddr_in* clientAddr, char* ipBuffer);
static void SelectMatchingSubnetIP(struct sockaddr_in* clientAddr, char* selectedIP);

/*================== 外部函数和变量声明    ==================================*/

void DiscoveryService(bool start)
{
  if( start )
    DiscoveryServiceStart( );
  else
    DiscoveryServiceStop( );
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

    discoveryRunning = TRUE;
    hDiscoveryThread = CreateThread(NULL, 0, DiscoveryThread, NULL, 0, NULL);
    
    if (hDiscoveryThread == NULL) {
        closesocket(discoverySocket);
        discoverySocket = INVALID_SOCKET;
        discoveryRunning = FALSE;
        SafePrintf("Failed to create discovery thread\n");
    }
}

// 停止发现服务
static void DiscoveryServiceStop(void)
{
    if (!discoveryRunning)
        return;
    discoveryRunning = FALSE;
    
    if (hDiscoveryThread) {
        WaitForSingleObject(hDiscoveryThread, 1000);
        CloseHandle(hDiscoveryThread);
        hDiscoveryThread = NULL;
    }

    if (discoverySocket != INVALID_SOCKET) {
        closesocket(discoverySocket);
        discoverySocket = INVALID_SOCKET;
    }

    DeleteCriticalSection(&csDiscovery);
    SafePrintf("Discovery service stopped\n");
}

// 更新发现信息
void UpdateDiscoveryInfo(uint16_t port, uint32_t clientCount)
{
    EnterCriticalSection(&csDiscovery);
    discoveryInfo.serverPort = port;
    discoveryInfo.clientCount = clientCount;
    LeaveCriticalSection(&csDiscovery);
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
    struct sockaddr_in clientAddr;
    int clientAddrLen = sizeof clientAddr;
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
        updataConsoleTitle(DiscoveryServerString, GetCurrentThreadId());

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
                        - 1, 0, (struct sockaddr*)&clientAddr, &clientAddrLen);
        
        if (bytesReceived == 0) 
          continue;
        recvBuffer[bytesReceived] = '\0';
        
        // 检查是否是有效的发现请求
        if (strnicmp(recvBuffer, "discover_com2tcp_server", strlen("discover_com2tcp_server")) == 0){
          SendDiscoveryResponse(&clientAddr); // 发送响应
        }
        else if (strnicmp(recvBuffer, CTRL_HEADER, strlen(CTRL_HEADER)) == 0){ 
          SafePrintf("UDP命令内容：%s\n", recvBuffer);
          HandleClientCommand(NULL, recvBuffer + strlen(CTRL_HEADER));
        }
        
        // SafePrintf("Discovery request from %s:%d\n", 
        //           inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
    }

    SafePrintf("Discovery thread exiting\n");
    return 0;
}

static void SendDiscoveryResponse(struct sockaddr_in* clientAddr)
{
    char responseBuffer[RESPONSE_BUFFER_SIZE];
    static uint16_t count = 0;
    memset(discoveryInfo.serverIP, 0, sizeof discoveryInfo.serverIP);

    EnterCriticalSection(&csDiscovery);
    char *ComputerFullName = GetComputerFullName();

    if( getIPmethod == true )   // 方法1：使用socket连接方式获取正确IP（更可靠）
      GetMatchingSubnetIP(clientAddr, discoveryInfo.serverIP);
    else                        // 方法2：或者使用网段匹配算法
      SelectMatchingSubnetIP(clientAddr, discoveryInfo.serverIP);
    
    // 构建响应消息
    snprintf(responseBuffer, sizeof responseBuffer,
            "%s|%-15s|%-15s|%d|%u|%u\n",
            discoveryInfo.serverName != NULL ? 
              discoveryInfo.serverName:"not server name",
            ComputerFullName != NULL ? ComputerFullName:"not host name",
            discoveryInfo.serverIP,
            discoveryInfo.serverPort,
            getClientNum(),
            getMaxClient());
    
    LeaveCriticalSection(&csDiscovery);

    // 发送响应到客户端
    int sendResult = sendto(discoverySocket, responseBuffer, strlen(responseBuffer), 
                  0, (struct sockaddr*)clientAddr, sizeof(*clientAddr));

    SafePrintf("Discovery response Sent to %s:%d -> Server IP: %s:%d  %s:%d  count:%-5d\r",
              inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port),
              discoveryInfo.serverIP, discoveryInfo.serverPort, 
              sendResult == SOCKET_ERROR? "failed":"succeed", WSAGetLastError(), ++count);
}

// 获取与客户端相同网段的IP地址
static void GetMatchingSubnetIP(struct sockaddr_in* clientAddr, char* ipBuffer)
{
    struct sockaddr_in tempAddr;
    int tempAddrLen = sizeof tempAddr;
    
    // 创建一个临时socket来获取本地接口信息
    SOCKET tempSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (tempSocket == INVALID_SOCKET) {
        strcpy(ipBuffer, "127.0.0.1");
        return;
    }
    
    // 连接到客户端地址，系统会自动选择正确的本地接口
    if (connect(tempSocket, (struct sockaddr*)clientAddr, 
              sizeof(*clientAddr)) == SOCKET_ERROR) {
        closesocket(tempSocket);
        strcpy(ipBuffer, "127.0.0.1");
        return;
    }
    
    // 获取socket的本地地址（这就是与客户端通信的接口地址）
    int ret = getsockname(tempSocket, (struct sockaddr*)&tempAddr, &tempAddrLen); 
    strcpy(ipBuffer, ret == 0? inet_ntoa(tempAddr.sin_addr): "127.0.0.1");

    closesocket(tempSocket);
}






// 获取所有本地IP地址
static void GetAllLocalIPs(char ips[][20], int *count)
{
    if( count == NULL )
      return;

    char hostname[256];
    if (gethostname(hostname, sizeof hostname) == SOCKET_ERROR)
        return;

    struct hostent* hostinfo = gethostbyname(hostname);
    if (hostinfo == NULL) 
        return;

    *count = 0;
     struct in_addr addr;
    for (int i = 0; hostinfo->h_addr_list[i] != NULL && *count < 10; i++) {
        memcpy(&addr, hostinfo->h_addr_list[i], sizeof(struct in_addr));
        if (strcmp(inet_ntoa(addr), "127.0.0.1") == 0) 
          continue;
        strncpy(ips[*count], inet_ntoa(addr), 16);
        (*count)++;
    }
}

// 选择与客户端相同网段的IP
static void SelectMatchingSubnetIP(struct sockaddr_in* clientAddr, char* selectedIP)
{
    char localIPs[10][20] = {0};
    int ipCount = 0;
    
    GetAllLocalIPs(localIPs, &ipCount);
    
    if (ipCount == 0) {
        strcpy(selectedIP, "127.0.0.1");
        return;
    }
    
    // 如果只有一个IP，直接使用
    if (ipCount == 1) {
        strcpy(selectedIP, localIPs[0]);
        return;
    }
    
    // 获取客户端IP的网段
    char clientIP[16];
    strcpy(clientIP, inet_ntoa(clientAddr->sin_addr));
    
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
            strcpy(selectedIP, localIPs[i]);
            return;
        }
    }
    
    // 如果没有找到匹配网段的IP，使用第一个非回环IP
    strcpy(selectedIP, localIPs[0]);
}

