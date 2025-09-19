 /******************************************************************************
  * @file    文件 main.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 本代码绝大部分都由AI完成，部分经过人工修改
  * 
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
#include "COM.h"
#include "DCM.h"
#include "TrafficStats.h"
#include "discovery.h"
#include "clients.h"
#include "serverListen.h"
#include "ServerConnect.h"
#include "exception.h"


/*================== 本地宏定义     =========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static serverInfo_t mainServer;

/*================== 全局共享变量    ========================================*/
const uint16_t * const mainServerPort = &mainServer.port;

/*================== 本地函数声明    ========================================*/
static void microFuncCodeTest(void);
static bool startServer(int argc, char const *argv[]);
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
  SetupExceptionHandler();
  
  logPrintResourceInit(true);
  microFuncCodeTest();
  InitializeWinSocket();
  
  if( startServer(argc, argv) == false )
    return 1;
  DiscoveryService(true);       // 启动发现服务
  StartTrafficMonitor();        // 流量统计
  ClientResourceInit(true);
  ComPortResourceInit(true);
  DeviceChangeMonitor(true);    // 启动设备插拔变化监听 
  UpdateDiscoveryInfo(mainServer.port, 0); // 初始客户端数量为0 
  ServerConnectInit(true);
  
  int8_t listenStartRet;
  bool addRet;
  while( true ) {
    
    // 看看是否有新的客户端连接
    listenStartRet = listenNewClientConnect(&mainServer);
    if( listenStartRet == -1 ) 
      break;
    if( listenStartRet && listenStartRet != 0 ){
      updataConsoleTitle("Main", GetCurrentThreadId());
      continue;
    }

    // 添加新客户端
    addRet = addNewClient(mainServer.newSocket, mainServer.newIP);
    if( addRet == false )
      closesocket( mainServer.newSocket );
  }

  closesocket(mainServer.socket); 
  ServerConnectInit(false);
  DiscoveryService(false);    // 在退出前停止发现服务 
  ClientResourceInit(false);
  ComPortResourceInit(false);
  DeviceChangeMonitor(false);
  
  logPrintResourceInit(false);
  WSACleanup();
  printf("main Program exit\n");
  return 0;
}

// 启动服务器
static bool startServer(int argc, char const *argv[])
{
  // 解析来自程序传递的端口号
  uint16_t retPort = ParsePortParameter(argc, argv);
  mainServer.port = retPort == 0? DEFAULT_PORT:retPort;
  mainServer.socket = INVALID_SOCKET; 
  mainServer.newSocket = INVALID_SOCKET;
  strcpy(mainServer.newIP, "NULL"); 
  
  // 初始化服务器资源
  bool serRet = serverInit(&mainServer); 
  SafePrintf("Server started %s! port: %d\n", 
    serRet? "succeed":"fail", mainServer.port);
  return serRet;
}



static void microFuncCodeTest(void)
{

}
