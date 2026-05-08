/******************************************************************************
  * @file    文件 main.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 本代码绝大部分都由AI完成，部分经过人工修改
  * 
  ******************************************************************************
  * @attention 注意
  * 
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "Queue.h"
#include "log.h"
#include "commonUtils.h"
#include "COM.h"
#include "DCM.h"
#include "TrafficStats.h"
#include "discovery.h"
#include "clients.h"
#include "serverListen.h"
#include "ServerConnect.h"
#include "exception.h"
#include "configSave.h"

#ifndef _WIN32
#include <unistd.h>
#include <stdlib.h>
#endif



/*================== 本地宏定义     =========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static serverInfo_t mainServer;

/*================== 全局共享变量    ========================================*/
const uint16_t * const mainServerPort = &mainServer.port;

/*================== 本地函数声明    ========================================*/
static void microFuncCodeTest(void);
static bool startServer(int argc, char const *argv[]);
static void PlatformSpecificInit(void);
static void PlatformSpecificCleanup(void);
static void linuxPlatformIsRoot(void);
static bool logFileInit(void);

/*=============================================================================
 功   能：主函数
 参   数：argc  传递数量
          argv  传递内容
 返   回：无
 描   述：无
=============================================================================*/
int main(int argc, char const *argv[])
{ 
  PlatformSpecificInit(); // 平台通用初始化 
  printBuildInfo();

  // 初始化异常监控，它会设置信号处理
  ProcessExceptionMonitorInit(); 
  microFuncCodeTest();

  SafePrintResourceInit(true);

  if( !startServer(argc, argv) )  // 启动服务器
    return -1;

  if( !logFileInit() )            // 初始化日志文件
    return -1;

  loadConfig();                   // 加载配置信息

  // 启动各种服务   
  DiscoveryService(true);         // 启动发现服务
    
  startAsyncFuncHandle(true);     // 启动异步函数处理
  ClientResourceInit(true);       // 客户端管理初始化
  ComPortResourceInit(true);      // 串口资源初始化
  DeviceChangeMonitor(true);      // 启动设备插拔变化监听 
  ServerConnectInit(true);        // 服务器连接初始化
  start1SecRunOneThread();        // 启动1秒执行一次的线程
  
  // 测试广播功能是否正常
  addAsyncFuncHandle(TestBroadcastCapabilityIsOK, NULL);  

  while( true ) {                 // 主事件循环 循环
    
    // 检查是否有新的客户端连接
    int8_t listenStartRet = listenNewClientConnect(&mainServer);
    
    if( listenStartRet == -1 ) {
      SafePrintf("服务器监听套接字无效，退出主循环\n");
      break;
    }
    
    if( listenStartRet && listenStartRet != 0 ){
      updataConsoleTitle("Main");
      continue;
    }
    
    bool addRet = addNewClient(mainServer.newSocket, mainServer.newIP);
    if( addRet == false ) {
      SafePrintf("添加新客户端失败，关闭套接字\n");
      closeSocket(mainServer.newSocket);
    }
  }

  // 清理资源
  SafePrintf("开始清理资源...\n"); 
  serverCleanup(&mainServer);
  ServerConnectInit(false);
  DiscoveryService(false);
  ClientResourceInit(false);
  ComPortResourceInit(false);
  DeviceChangeMonitor(false);
  startAsyncFuncHandle(false);
  logStorageUninit();
  SafePrintResourceInit(false);
  CleanupProcessExceptionMonitor();
  Platform_Cleanup();
  PlatformSpecificCleanup();
  

  SafePrintf("程序正常退出\n");
  return 0;
}

void voluntaryWithdrawal(const char *reason)
{
  printf("\n程序主动退出，原因：%s\n", reason? reason:"未知");
  logPrintFull(LOG_LEVEL_INFO, "程序主动退出，原因：%s", reason? reason:"未知");
  serverCleanup(&mainServer);
  exit(0);
}

static bool logFileInit(void)
{ 
  char *platform = "Win";
  #ifdef __linux
  platform = "Linux";
  #endif

#ifndef CLOSE_EXCEPTION_MONITOR 
  LogLevel_t logLevel = LOG_LEVEL_DEBUG;
#else
  LogLevel_t logLevel = LOG_LEVEL_INFO;
#endif

  const char *logFileName = getPrintf("logFile%s-PORT%d", platform, mainServer.port); 
  bool start = logStorageInit(SAVE_DIR, logFileName, logLevel, 2, 5, 200);

  if(!start)
    printf("Failed to initialize log storage\n");
  return start;
}

// 启动服务器
static bool startServer(int argc, char const *argv[])
{
  // 解析来自程序传递的端口号
  uint16_t retPort = ParsePortParameter(argc, argv);
  mainServer.port = retPort == 0 ? DEFAULT_PORT : retPort;
  mainServer.socket = INVALID_SOCKET_VALUE; 
  mainServer.newSocket = INVALID_SOCKET_VALUE;
  strcpy(mainServer.newIP, "NULL");

  // 真实启动服务器
  bool serRet = serverStart(&mainServer);
  SafePrintf("Server Started %s!  Port: %d\n",
      serRet ? "Succeed" : "Fail", mainServer.port);
      
  return serRet;
}

// 平台特定初始化
static void PlatformSpecificInit(void)
{
  linuxPlatformIsRoot();

#ifdef _WIN32
  system("cls"); 
#else
  if(system("clear")){}
#endif
  
#ifdef _WIN32
  // Windows 特定初始化
  SetConsoleOutputCP(65001);  // 设置控制台为UTF-8编码
  SetConsoleCP(65001);
  EnableVTMode();             // 启用VT模式支持ANSI转义序列
  SetConsoleFontSize(8, 16);  // 设置控制台字体
  
  // 设置控制台标题
  SetConsoleTitleA("串口转TCP服务器 - 启动中...");

  if( !Platform_Initialize() )
    printf("平台初始化失败\n");
#else
  // Linux 特定初始化
  // 检查是否在终端中运行
  if (isatty(STDOUT_FILENO)) 
      printf("\033[?25h");  // 启用终端颜色和支持 显示光标
  
  // 注意：信号处理现在由 exception.c 统一管理
  // 不需要在这里重复设置信号处理
#endif
}

// 平台特定清理
static void PlatformSpecificCleanup(void)
{
#ifdef _WIN32
    // 恢复控制台设置
    SetConsoleOutputCP(GetACP());  // 恢复原始代码页 
#else
    // 恢复终端设置
    if (isatty(STDOUT_FILENO)) {
        printf("\033[?25h");  // 确保光标显示
        printf("\033[0m");    // 重置所有属性
    }
#endif
}



// 微功能代码测试（可用于调试和测试）
static void microFuncCodeTest(void)
{
  // 测试内存分配
  void* test_ptr = malloc(100);
  if (test_ptr) 
      free(test_ptr);
    
  // 测试时间函数
  uint16_t start_time = getRuningTimeMs(); 
  Sleep(10);  // 跨平台的Sleep
  uint16_t end_time = getRuningTimeMs();

  printf("内存分配测试: %s，时间函数测试: 耗时 %d/10 ms，微功能测试结束。已经运行：%d ms。\n", 
    test_ptr? "成功":"失败", end_time - start_time, (uint16_t)getRuningTimeMs());
}

// 检查 Linux 环境是否为 root
static void linuxPlatformIsRoot(void)
{
#ifndef _WIN32
  if (geteuid() == 0)
    return;
    
  printf("此程序需要管理员权限才能操作串口\n");
  printf("请输入密码以继续或CRTL+C退出...\n");
  
  // 重新以sudo运行自身
  char command[256];
  snprintf(command, sizeof command, "sudo %s", program_invocation_name);
  
  int result = system(command);
  if (result != 0) {
      printf("权限提升失败，程序退出\n");
      exit(EXIT_FAILURE);
  }
  exit(EXIT_SUCCESS); // 子进程成功启动后退出原进程

#endif
}
