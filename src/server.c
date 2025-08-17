
/*
实现将串口数据转到TCP收发的能力
环境Win平台，使用C语言编写一个服务端程序，接受任何网段连接该服务器

主要实现功能如下：
1. 服务端监听的串口号默认为9000，如果被占用自动加1，服务端使用一个宏来控制能接受多少个客户端连接。
2. 限制最大客户端连接数量，新的客户端连接后，踢掉最早连接的客户端，使用系统时间精确到ms的方法作为判断依据哪个是最早的，并告诉被踢下线的客户端他被踢下线了
3. 使用setsockopt函数 禁用Nagle算法。使用非阻塞式监听客户端连接，超时时间为1秒。
4. 用“crtlInfo:”字符串用于客户端控制服务器动作的头标识，结尾加“\n”，这些约定的内容用来作为控制信息，不发送给串口。
5. 客户端连接服务端后，服务端创建一个独立的线程接收客户端的数据，完成后向客户端发送“crtlInfo:OK! your indes x\n”
6. 获取Win系统下有效可用的串口列表，让客户端通过发送“crtlInfo:comliset”字符串后，服务端返回可用串口列表。
7. 比如串口列表里有 COM2和COM5，客户端通过发送 “crtlInfo:open,串口号,波特率,数据位,停止位,校验位\n” 来打开串口并设置相关参数，其中，串口号必填项，后面可以在不填入情况下，使用默认参数，默认波特率115200，数据位8位，停止位1位，无校验位。不管打开串口成功或失败，都将结果代码发送给客户端，格式为“crtlInfo: open [串口号,波特率,数据位,停止位,校验位] 结果 异常代码\n”
8. 可以的话用独立线程接收串口数据，接收到串口数据发给所有客户端（数据内容有不光有字符串，还有一般数据），任何客户端发来的数据直接发给串口。
9. 一个服务端只能打开一个串口。相应的，任何客户端也可以发送打开新的串口，但是要关闭之前打开的串口。
10. 串口可能出现热插拔或者异常关闭的问题，将这些信息发给所有客户端，格式为“crtlInfo:串口号异常关闭，结果代码\n”
11. 串口通信异步方式可能无法正常使用，暂且用同步方式。
12. 让程序支持在运行时通过传入参数，来修改指定端口号，比如输入 -p5000 就指定监听5000端口号，-p参数不区分大小写，规避前1024，返回参数为端口号。
如果该端口号被占用就自动加1，尝试10次。可能是启用IPv6和IPv4问题，第二次运行还是同一个端口号切不支持IPv6，只有第三次运行才会是新的端口号
13. USB设备插入或拔出通知所有客户端
最后给出使用MakeFile管理编译。
*/

#include <stdio.h>

#include <stdint.h>
#include <stdbool.h>

#include <winsock2.h>
#include <windows.h>
#include <time.h>

#include "main.h"
#include "DCM.h"
#include "logPrint.h"
#include "public.h"
#include "client.h"
#include "COM.h"

static SOCKET serverSocket = INVALID_SOCKET;
int ParsePortParameter(int argc, char const* argv[], int defaultPort);
 
void HandleClientCommand( SOCKET clientSocket, uint8_t clientIndex, const char* command) 
{
    char *token, temp[100];
    strcpy(temp, command);

    if (strncmp(command, "comlist", strlen("comlist")) == 0) {
      sendListComPorts(&clientSocket);
    }
    else if (strncmp(command, "setRecvCOMdataTo", strlen("setRecvCOMdataTo")) == 0) {
      token = strtok(temp, DECOLLATOR);
      token = strtok(NULL, DECOLLATOR);
      
      char clientStr[5];
      memset(clientStr, 0, sizeof clientStr);
      if( strncmp(token, "my", strlen("my") ) == 0 ){
        runInfo.monopolizeSoclet = &clients[ clientIndex ].socket;
        runInfo.monopolizeIndex = clientIndex;
        sprintf(clientStr, "%d", clientIndex);
      }
      else{
        strcpy(clientStr, "All");
        runInfo.monopolizeSoclet = NULL;
      }
      printfSend(NULL, "%sset COM --> TCP %s client\n", CTRL_HEADER, clientStr); 
    }
    else if (strncmp(command, "exit", strlen("exit")) == 0) {
      printfSend(&clientSocket, "%sserver ready exit\n", CTRL_HEADER );
      exit(0);
    }
    else if (strncmp(command, "serverPrintData", strlen("serverPrintData") ) == 0) {
        token = strtok(temp, DECOLLATOR);
        token = strtok(NULL, DECOLLATOR); // 显示模式
        runInfo.serverPrintData = 0;
        if( strncmp(token, "NULL", strlen("NULL") ) == 0 )
          runInfo.serverPrintData = 0;
        if( strncmp(token, "ASCII", strlen("ASCII")) == 0 )
          runInfo.serverPrintData = 1;
        if( strncmp(token, "HEX", strlen("HEX")) == 0 )
          runInfo.serverPrintData = 2;
        printfSend(NULL, "%sserver Print Data: %d %s \n", 
          CTRL_HEADER, runInfo.serverPrintData, token);
    }
    else if (strncmp(command, "system", strlen("system") ) == 0) {
        // 解析命令
        token = strtok(temp, DECOLLATOR);
        token = strtok(NULL, DECOLLATOR); // 指令 
        int ret = system(token);
        printfSend(&clientSocket, "%sexecute system %s :%d\n", 
            CTRL_HEADER, ret == 0? "success": "failed", ret);
    }
    else if (strncmp(command, "open", strlen("open")) == 0) {
        // 解析命令
        token = strtok(temp, DECOLLATOR);
        token = strtok(NULL, DECOLLATOR); // 串口号

        if( token == NULL || strncasecmp(token, "COM", strlen("COM") ) != 0 ) {
          printfSend(&clientSocket, "%sThe input is not COM\n", CTRL_HEADER);
          return;
        }

        char portName[10];
        memset(portName, 0, sizeof portName);
        strcpy(portName, "COM");  // 串口号
        for(uint8_t i = strlen(portName); i < strlen(token); i++)
          portName[i] = ( '0' <= token[i] && token[i] <= '9' ) ? token[i] : 0;

        if( strcmp(portName, comPort.portName) == 0 ){ // 防止重复打开同一个串口浪费资源
          printfSend(&clientSocket, "%sthe %s has been turned on\n", CTRL_HEADER, portName);
          return;
        }

        uint32_t baudRate = 115200;
        uint8_t dataBits = 8, stopBits = 1, parity = 0;
        token = strtok(NULL, DECOLLATOR); // 波特率
        if (token) baudRate = atoi(token);
        
        token = strtok(NULL, DECOLLATOR); // 数据位
        if (token) dataBits = atoi(token);
        
        token = strtok(NULL, DECOLLATOR); // 停止位
        if (token) stopBits = atoi(token);
        
        token = strtok(NULL, DECOLLATOR); // 校验位
        if (token) parity = atoi(token);

        CloseComPort();
        DWORD error = 0;
        int8_t ret = OpenComPort(portName, baudRate, dataBits, stopBits, parity);
        if( ret != 0 )
          error = GetLastError();

        printfSend(NULL, "%sopen [%s,%d,%d,%d,%d] %s %d %ld\n", CTRL_HEADER, 
          portName,baudRate,dataBits,stopBits,parity,
          ret == 0 ? "success":"failed", ret, error );
        SafePrintf("open [%s,%d,%d,%d,%d] %s %d %ld\n", 
          portName,baudRate,dataBits,stopBits,parity,
            ret == 0 ? "success":"failed", ret, error);
    }
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









int main(int argc, char const *argv[])
{
    print_build_info();

    if( argc || argv){} 
    time(&runInfo.startTime);  // 获取当前时间（从 1970-01-01 00:00:00 开始的秒数）
 
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        SafePrintf("WSAStartup failed: %d\n", iResult);
        return 1;
    }

    logPrintResourceInit(true); 
    ClientResourceInit(true);
    ComPortResourceInit(true);
    

    // 初始化客户端数组
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket = INVALID_SOCKET;
        clients[i].hThread = NULL;
    }

    // 解析来自程序传递的端口号
    int port = ParsePortParameter(argc, argv, DEFAULT_PORT);

    // 查找可用端口
    port = FindAvailablePort(port);
    if (port == -1) {
        SafePrintf("No available port found\n");
        WSACleanup();
        return 1;
    }

    // 创建服务器套接字
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        SafePrintf("Error at socket(): %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // 禁用Nagle算法
    char nagleStatus = 1;
    int result = setsockopt(serverSocket, //socket的文件描述符
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

    if (bind(serverSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
        SafePrintf("bind failed with error: %d\n", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // 监听
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        SafePrintf("listen failed with error: %d\n", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // 启动设备变化监听
    StartDeviceChangeMonitor();
 
    SafePrintf("Server started on port %d\n", port);

    fd_set readSet;
    struct timeval timeout;
    int selRet;
    SOCKET clientSocket;
    
    while (true) {
        FD_ZERO(&readSet);
        FD_SET(serverSocket, &readSet);

        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        selRet = select(0, &readSet, NULL, NULL, &timeout);
        if (selRet == 0) { 
            updataConsoleTitle("Main", GetCurrentThreadId());
            continue;
        }
        else if (selRet == SOCKET_ERROR) {
            SafePrintf("select failed, error=%d\n", WSAGetLastError());
            break;
        } 

        if (!FD_ISSET(serverSocket, &readSet)) 
            continue;

        clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            SafePrintf("accept failed, error=%d\n", WSAGetLastError());
            continue;
        }
        
        extern CRITICAL_SECTION csClient;
        EnterCriticalSection(&csClient);

        // 查找空闲位置或最早的客户端
        int8_t newCountIndex = -1;
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (clients[i].socket == INVALID_SOCKET) {
                newCountIndex = i;
                break;
            }
        
        // 如果没有空闲位置，踢掉最早的客户端
        if (newCountIndex == -1) {
            uint8_t oldestIndex = 0;
            __int64 oldestTime = clients[0].connectTime;

            // 找出最早连接的客户端
            for (uint8_t i = 1; i < MAX_CLIENTS; i++)
                if (clients[i].connectTime < oldestTime) {
                    oldestTime = clients[i].connectTime;
                    oldestIndex = i;
                }
            
            // 通知被踢的客户端
            printfSend(&clients[oldestIndex].socket, 
                "%sYou are kicked due to server full! Your index %d, connected at %I64d ms\n", 
                CTRL_HEADER, oldestIndex, oldestTime);
            
            SafePrintf("Kicked oldest client index %d (connected at %I64d ms)\n", 
                oldestIndex, oldestTime);
            
            // 关闭最早的客户端
            CloseClient(oldestIndex, "Server full, kicking oldest client");
            newCountIndex = oldestIndex;
        }

        // 添加新客户端
        addClient(newCountIndex, clientSocket);
        LeaveCriticalSection(&csClient);
    }

  // 清理
  for (uint8_t i = 0; i < MAX_CLIENTS; i++)
    CloseClient( i, "清理");
  CloseComPort();
  closesocket(serverSocket);
  ClientResourceInit(false);
  ComPortResourceInit(true);
  logPrintResourceInit(false);
  WSACleanup();
  StopDeviceChangeMonitor();  
  return 0;
}


#define MIN_USER_PORT   1024
#define MAX_PORT        65535

// 解析命令行参数获取端口号
// 参数: argc - 参数个数, argv - 参数数组, defaultPort - 默认端口号
// 返回值: 解析成功的端口号，如果无效则返回-1
int ParsePortParameter(int argc, char const* argv[], int defaultPort) 
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
    return defaultPort;
}







