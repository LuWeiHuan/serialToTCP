/******************************************************************************
  * @file    文件 public.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 公共资源
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "public.h"
#include "main.h"
#include "COM.h"
#include "TrafficStats.h"
#include "logPrint.h"
#include "serverListen.h"
#include "client.h"

#include <stdio.h>
#include <time.h>

#include <winsock2.h>
#include <windows.h>

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
runInfo_t  runInfo = {
  .serverPrintData = 0,
  .clientCount = 0,
  .monopolizeSocket = NULL,
  .monopolizeIndex = 0,
  .startTime = 0,
  .connectCount = 0,
};

/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/


/*================== 外部函数和变量声明    ==================================*/

// 获取从运行到现在的间戳（毫秒）程序运行要调用一次
uint64_t GetCurrentTimeMillis(void) 
{
  struct _timeb timebuffer; 
  _ftime_s(&timebuffer);

  static uint64_t initialTimeMs = 0;
  if( initialTimeMs == 0 ){
    initialTimeMs = timebuffer.time * 1000 + timebuffer.millitm;
    time(&runInfo.startTime);  // 获取当前时间（从 1970-01-01 00:00:00 开始的秒数） 
  }
  
  uint64_t atPresent = timebuffer.time * 1000 + timebuffer.millitm;
  return atPresent - initialTimeMs;
}


void updataConsoleTitle(char *threadName, DWORD theradID)
{ 
  char title[100];
  memset(title, 0, sizeof title);
  time_t currentTime;
  time(&currentTime); 
  currentTime -= runInfo.startTime;
  //currentTime += 60*60*24 - 6;

  uint8_t sec = currentTime % 60;
  uint8_t min = currentTime / 60 % 60;
  uint8_t hour = currentTime / 360 % 24;
  uint32_t day = currentTime / 86400;

  #ifdef __TRAFFIC_STATS_H_
  if( sec % 3 == 0 || sec % 4 == 0 )
    snprintf(title, sizeof title, "串口转TCP     串口:↑ %s  ↓ %s   网络：↑ %s  ↓ %s    线程%ld：%s",
      trafficStats.com.recvRate,
      trafficStats.com.sendRate,
      trafficStats.net.sendRate,
      trafficStats.net.recvRate,
      theradID, threadName =! NULL? threadName:"No thread Name");
  else
  #endif
    snprintf(title, sizeof title, "串口转TCP     服务端口号：%d   "
      "已运行%d天：%02d:%02d:%02d  客户端：%d/%d  线程%ld：%s",
        g_server.port, day, hour, min,sec, runInfo.clientCount, MAX_CLIENTS, 
        theradID, threadName =! NULL? threadName:"No thread Name");
  
  SetConsoleTitleA( title );
}


char *getCurrentTime(void) 
{
  static char timeStr[40];
  memset(timeStr, 0, sizeof timeStr);
  SYSTEMTIME st;
  GetLocalTime(&st);  // 获取本地时间

  // 格式化为 "YYYY-MM-DD HH:MM:SS"
  snprintf(timeStr, sizeof timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
          st.wYear, st.wMonth, st.wDay,
          st.wHour, st.wMinute, st.wSecond);
  return timeStr;
}

void printBuildInfo(void) 
{
  printf("========================================\n");
  printf("  Program    : %s\n", "串口转TCP服务端");
  printf("  Version    : %s\n", VERSIONS);
  printf("  Build Date : %s %s\n", __DATE__, __TIME__);
  printf("  Compiler   : GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
  printf("========================================\n\n");
}


/*
获取收发方向字符串
 direct 参数如下是如下字符串
   [COM --> TCP]
   [TCP --> COM]

  index 客户端索引号
*/
char *getSendRecvDirectionStr(char *direct, uint8_t index)
{
  char *endptr;  // 用于检测未转换的字符 
  uint8_t comNum = strtol(comPort.portName + 3, &endptr, 10);

  static char retStr[30];
  memset(retStr, 0, sizeof retStr);
  strcpy(retStr, "    -->    ");
#if 0
  if( strcmp(direct, "[TCP --> COM]") == 0 ){
    memset(retStr, 0, sizeof retStr);
    snprintf(retStr, sizeof retStr, "TCP%-3d--> COM%-3d" , index, comNum);
  }

  if( strcmp(direct, "[COM --> TCP]") == 0 ){
    memset(retStr, 0, sizeof retStr);

    if( runInfo.monopolizeSocket != NULL ) // 独占串口数据
      snprintf(retStr, sizeof retStr, "COM%-3d--> TCP%-3d", 
        comNum, runInfo.monopolizeIndex);
    else
      snprintf(retStr, sizeof retStr, "COM%-3d--> TCP%3d",
        comNum, runInfo.clientCount);
  }
#else
  if( strcmp(direct, "[TCP --> COM]") == 0 ){
    memset(retStr, 0, sizeof retStr);
    snprintf(retStr, sizeof retStr, "%-16s--> COM%-3d" , getClientIP(index), comNum);
  }

  if( strcmp(direct, "[COM --> TCP]") == 0 ){
    memset(retStr, 0, sizeof retStr);
    static char clientString[32] = {0};
    memset(clientString, 0, sizeof clientString);
    if (runInfo.monopolizeSocket != NULL) 
      snprintf(clientString, sizeof clientString, "%s", getClientIP(runInfo.monopolizeIndex));
    else if (runInfo.clientCount == 0) 
      snprintf(clientString, sizeof clientString, "No client");
    else 
      snprintf(clientString, sizeof clientString, "All client %d", runInfo.clientCount);

    snprintf(retStr, sizeof(retStr), "COM%-3d--> %-16s", comNum, clientString);
  }
#endif
  return retStr;
}

/**
 * 获取计算机全名（DNS全名）
 * 返回值：计算机名字符串
 */
char *GetComputerFullName(void) 
{
    DWORD nameLen = 0;
    static char computerName[20]; // Win提示计算机名最大15个字符
    memset(computerName, 0, sizeof computerName);

    // 第一次调用获取所需缓冲区大小
    BOOL result = GetComputerNameEx(ComputerNameDnsFullyQualified, NULL, &nameLen);
    if (result == FALSE && GetLastError() != ERROR_MORE_DATA){ 
      snprintf(computerName, sizeof computerName, "A not name:%ld", GetLastError());
      return computerName;
    }
    
    // 第二次调用获取实际名称
    result = GetComputerNameEx(ComputerNameDnsFullyQualified, computerName, &nameLen);
    if (result == FALSE) { 
      snprintf(computerName, sizeof computerName, "B not name:%ld", GetLastError());
      return computerName;
    }

    return computerName; // 成功
}
