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
#include "traffic.h"
#include "logPrint.h"

#include <stdio.h>
#include <time.h>

#include <winsock2.h>
#include <windows.h>

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
runInfo_t  runInfo = {
  .serverPrintData = 0,
  .clientCount = 0,
  .monopolizeSoclet = NULL,
  .monopolizeIndex = 0,
  .port = 0,
  .startTime = 0,
  .linkCount = 0,
};

/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/


/*================== 外部函数和变量声明    ==================================*/


// 获取当前时间戳（毫秒）
__int64 GetCurrentTimeMillis(void) 
{
  static __int64 initAt = 0;
  if( initAt == 0 ){ 
    time(&runInfo.startTime);  // 获取当前时间（从 1970-01-01 00:00:00 开始的秒数）
    static struct _timeb timebufferInit; 
    _ftime_s(&timebufferInit);
    initAt = timebufferInit.time * 1000 + timebufferInit.millitm;
  }

  struct _timeb timebuffer;
  
  _ftime_s(&timebuffer);
  __int64 atPresent = timebuffer.time * 1000 + timebuffer.millitm;

  return atPresent - initAt;
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
  uint32_t day = currentTime / 360 / 24;
  
  
  if( sec % 3 == 0 || sec % 4 == 0 )
    snprintf(title, sizeof title, "串口转TCP     串口:↑ %s  ↓ %s   网络：↑ %s  ↓ %s    线程%ld：%s",
      trafficStats.com.recvRateStr,
      trafficStats.com.sendRateStr,
      trafficStats.net.sendRateStr,
      trafficStats.net.recvRateStr,
      theradID, threadName =! NULL? threadName:"NULL");
  else
    snprintf(title, sizeof title, "串口转TCP     服务端口号：%d   "
      "已运行%d天：%02d:%02d:%02d  客户端：%d/%d  线程%ld：%s",
        runInfo.port, day, hour, min,sec, runInfo.clientCount, MAX_CLIENTS, 
        theradID, threadName =! NULL? threadName:" ");



  SetConsoleTitleA( title );
}


char *getCurrentTime(void) 
{
  static char timeStr[40];
  memset(timeStr, 0, sizeof timeStr);
  SYSTEMTIME st;
  GetLocalTime(&st);  // 获取本地时间

  // 格式化为 "YYYY-MM-DD HH:MM:SS"
  sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
          st.wYear, st.wMonth, st.wDay,
          st.wHour, st.wMinute, st.wSecond);
  return timeStr;
}

void printBuildInfo(void) 
{
  printf("========================================\n");
  printf("  Program    : %s\n", "串口转TCP服务端");
  printf("  Version    : %s\n", "1.0.0");
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

  static char retStr[20];
  memset(retStr, 0, sizeof retStr);
  strcpy(retStr, "[    -->    ]");

  if( strcmp(direct, "[TCP --> COM]") == 0 ){
    memset(retStr, 0, sizeof retStr);
    sprintf(retStr, "TCP%-3d--> COM%-3d" , index, comNum);
  }

  if( strcmp(direct, "[COM --> TCP]") == 0 ){
    memset(retStr, 0, sizeof retStr);

    if( runInfo.monopolizeSoclet != NULL ) // 独占串口数据
      sprintf(retStr, "COM%-3d--> TCP%-3d" , comNum, runInfo.monopolizeIndex);
    else
      sprintf(retStr, "COM%-3d--> TCP%3d" , comNum, runInfo.clientCount);
  }

  return retStr;
}
