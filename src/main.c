 /******************************************************************************
  * @file    文件 main.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 本代码绝大部分都由AI完成，部分经过人工修改
  * 

 AI 平台：DeepSeek
运行系统：Windows
编译工具：MinGW
工程管理：make

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
14. 串口异步发送能力。使用独立线程使用队列，主要任务是异步发送数据到串口。
15. 请让服务端实现被发现的能力，这个功能在子线程用UDP实现，使用19000端口
  ******************************************************************************
  * @attention 注意
  * 可能要要打开设备管理器才能实现插入拔出串口检测功能
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include <stdio.h>
#include <time.h>

#include "main.h"
#include "logPrint.h"
#include "public.h"
#include "server.h"
#include "client.h"
#include "COM.h"
#include "DCM.h"
#include "TrafficStats.h"
#include "discovery.h"

/*================== 本地宏定义     =========================================*/
#define DECOLLATOR    ",\n"


/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/
static void microFuncCodeTest(void);
/*================== 外部函数和变量声明    ==================================*/

/*=============================================================================
 功   能：主函数
 参   数：argc  传递数量
          argv  传递内容
 返   回：无
 描   述：无
=============================================================================*/
int main(int argc, char const *argv[])
{
  GetCurrentTimeMillis();
  printBuildInfo();
  microFuncCodeTest();
  
  logPrintResourceInit(true); 
  ClientResourceInit(true);
  ComPortResourceInit(true);
  StartTrafficMonitor(); //流量统计

  // 解析来自程序传递的端口号
  int port = ParsePortParameter(argc, argv);
  if( port <= 0 )
    return 1;
  
  // 初始化服务器资源
  static SOCKET serverSocket = INVALID_SOCKET;
  port = serverInit(port, &serverSocket);
  if( port == 0 ){
    SafePrintf("Server init fail %d\n", port);
    return 1;
  }

  SafePrintf("Server started on port %d\n", port);
   
  DeviceChangeMonitor( true ); // 启动设备插拔变化监听

  DiscoveryServiceStart();      // 启动发现服务
  UpdateDiscoveryInfo(port, 0); // 初始客户端数量为0

  SOCKET newClientSocket;
  int8_t listenStartRet, getClientIndex;
  char clientIP[100];
  while( true ) {
    memset(clientIP, 0, sizeof clientIP);

    // 看看是否有新的客户端连接
    listenStartRet = listenNewClientLink(&serverSocket, &newClientSocket, clientIP);
    if( listenStartRet == -1 ) 
      break;
    if( listenStartRet && listenStartRet != 0 ){
      updataConsoleTitle("Main", GetCurrentThreadId());
      continue;
    }
      
    getClientIndex = findClientSlot();  // 获取新的客户端索引空位
    addNewClient(getClientIndex, newClientSocket, clientIP); // 添加新客户端 
  }

  closesocket(serverSocket); 
  DiscoveryServiceStop();  // 在退出前停止发现服务 
  ClientResourceInit(false);
  ComPortResourceInit(false);
  logPrintResourceInit(false);
  DeviceChangeMonitor(false);
  WSACleanup();
  return 0;
}

// 处理客户端发过来的指令
void HandleClientCommand(SOCKET *clientSocket, uint8_t clientIndex, const char* command) 
{
    char *token;
    static char handleString[256];
    memset(handleString, 0, sizeof handleString);
    strcpy(handleString, command);

    if (strnicmp(command, "comlist", strlen("comlist")) == 0) { 
      sendComPortsListToClient( clientSocket );
    }

    else if ( strnicmp(command, "PrintAllclientIP", strlen("PrintAllclientIP")) == 0) {
      getAllclientIPandIndexInfo(handleString, sizeof handleString);
      SafePrintf("All Client IP:\n%s\n", handleString);
      printfSend(clientSocket, "All Client IP:\n%s\n", handleString);
    }

    else if (strnicmp(command, "setCOMsednWiat", strlen("setCOMsednWiat")) == 0) {
      token = strtok(handleString, DECOLLATOR);
      token = strtok(NULL, DECOLLATOR);
      uint8_t num = atoi(token);
      
      if( num == 0 ){
        COMFreeAsyncSendQueue();
        printfSend(NULL, "set COM sedn NO Wiat\n");
        return;
      }

      BOOL ret = COMInitAsyncSendThread(num);
      printfSend(NULL, "set COM sedn Wiat %s set Queue num %d\n", 
        ret? "OK!":"Fail! scope 10~200 !", num);
    }

    else if (strnicmp(command, "setRecvCOMdataTo", strlen("setRecvCOMdataTo")) == 0) {
      token = strtok(handleString, DECOLLATOR);
      token = strtok(NULL, DECOLLATOR);
      
      char clientStr[5];
      memset(clientStr, 0, sizeof clientStr);
      if( strnicmp(token, "my", strlen("my") ) == 0 ){
        runInfo.monopolizeSoclet = clientSocket;
        runInfo.monopolizeIndex = clientIndex;
        sprintf(clientStr, "%d", clientIndex);
      }
      else{
        strcpy(clientStr, "All");
        runInfo.monopolizeSoclet = NULL;
      }
      printfSend(NULL, "set COM --> TCP %s client\n", clientStr); 
    }

    else if (strnicmp(command, "exit", strlen("exit")) == 0) {
      printfSend(NULL, "server ready exit\n" );
      exit(0);
    }

    else if (strnicmp(command, "serverPrintData", strlen("serverPrintData") ) == 0) {
        token = strtok(handleString, DECOLLATOR);
        token = strtok(NULL, DECOLLATOR); // 显示模式
        runInfo.serverPrintData = 0;
        if( strnicmp(token, "NULL", strlen("NULL") ) == 0 )
          runInfo.serverPrintData = 0;
        if( strnicmp(token, "ASCII", strlen("ASCII")) == 0 )
          runInfo.serverPrintData = 1;
        if( strnicmp(token, "HEX", strlen("HEX")) == 0 )
          runInfo.serverPrintData = 2;
        if( strnicmp(token, "CMD", strlen("CMD")) == 0 )
          runInfo.serverPrintData = 3;
        printfSend(NULL, "server Print Data: %d %s \n", 
            runInfo.serverPrintData, token);
    }

    else if (strnicmp(command, "system", strlen("system") ) == 0) {
        token = strtok(handleString, DECOLLATOR);
        token = strtok(NULL, DECOLLATOR); // 指令 
        int ret = system(token);
        printfSend(clientSocket, "execute system %s :%d\n", 
            ret == 0? "success": "failed", ret);
    }

    else if (strnicmp(command, "open", strlen("open")) == 0) {
        token = strtok(handleString, DECOLLATOR);
        token = strtok(NULL, DECOLLATOR); // 串口号

        if( token == NULL || strnicmp(token, "COM", strlen("COM") ) != 0 ) {
          printfSend(clientSocket, "The input is not :%s\n", token == NULL? "NULL":token);
          return;
        }

        char portName[10], *endptr;
        memset(portName, 0, sizeof portName);
        sprintf(portName, "COM%d", (int)strtol(token + strlen("COM"), &endptr, 10) );

        if( strcmp(portName, comPort.portName) == 0 ){ // 防止重复打开同一个串口浪费资源
          printfSend(clientSocket, "the %s has been turned on\n", portName);
          return;
        }
        
        /* 解析串口设置的参数 */
        uint32_t baudRate = 921600;
        uint8_t dataBits = 8, stopBits = 1, parity = 0;
        token = strtok(NULL, DECOLLATOR); // 波特率
        if (token) baudRate = atoi(token);
        
        token = strtok(NULL, DECOLLATOR); // 数据位
        if (token) dataBits = atoi(token);
        
        token = strtok(NULL, DECOLLATOR); // 停止位
        if (token) stopBits = atoi(token);
        
        token = strtok(NULL, DECOLLATOR); // 校验位
        if (token) parity = atoi(token);

        if ( comPort.isOpen )
          CloseComPort();

        printfSend(clientSocket, "opening %s...\n", portName); 
        int8_t ret = OpenComPort(portName, baudRate, dataBits, stopBits, parity); 
        DWORD error = (ret != 0)? GetLastError(): 0;

        memset(handleString, 0, sizeof handleString);
        sprintf(handleString, "open [%s,%d,%d,%d,%d] %s! (%d:%ld)\n", 
          portName,baudRate,dataBits,stopBits,parity,
          ret == 0 ? "success":"failed", ret, error);
        printfSend(NULL, "%s", handleString);
        SafePrintf("%s", handleString);
    }
    else
      printfSend(clientSocket, "Not Command:%s\n", command);
}


static void microFuncCodeTest(void)
{
 
}
