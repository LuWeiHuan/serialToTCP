/******************************************************************************
  * @file    文件 traffic.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 流量统计
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "TrafficStats.h"
#include "public.h"

#include <windows.h>
#include <stdio.h>

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
GlobalTrafficStats_t trafficStats;

/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/
static void formatSpeedString(uint64_t bytesPerSec, char* output, uint16_t retMax) ;
static DWORD WINAPI TrafficMonitorThread(LPVOID lpParam);

/*================== 外部函数和变量声明    ==================================*/

// 启动流量统计线程
void StartTrafficMonitor(void) 
{
  memset(&trafficStats, 0, sizeof trafficStats);
  trafficStats.run = true;
  CreateThread(NULL, 0, TrafficMonitorThread, NULL, 0, NULL);
}

static DWORD WINAPI TrafficMonitorThread(LPVOID lpParam)
{
  (void)( lpParam );
  uint8_t updataConsoConut = 0;
 
  while ( trafficStats.run ) {
 
    formatSpeedString(trafficStats.com.totalBytesSent, 
      trafficStats.com.sendRate, sizeof trafficStats.com.sendRate);

    formatSpeedString(trafficStats.com.totalBytesReceived, 
      trafficStats.com.recvRate, sizeof trafficStats.com.recvRate);
 
    formatSpeedString(trafficStats.net.totalBytesSent,
      trafficStats.net.sendRate, sizeof trafficStats.net.sendRate);

    formatSpeedString(trafficStats.net.totalBytesReceived,
      trafficStats.net.recvRate, sizeof trafficStats.net.recvRate);

    trafficStats.com.totalBytesSent = 0;
    trafficStats.com.totalBytesReceived = 0;
    trafficStats.net.totalBytesSent = 0;
    trafficStats.net.totalBytesReceived = 0;
    
    if( ++updataConsoConut > 1 ){
      updataConsoConut = 0;
      updataConsoleTitle("TrafficMonitor", GetCurrentThreadId());
    }
    Sleep( 1000 );// 统计间隔1秒
  }
  return 0;
}

// 自动转换单位为最佳可读格式（Byte/s → KB/s → MB/s → GB/s）
static void formatSpeedString(uint64_t bytesPerSec, char* output, uint16_t retMax) 
{
  const char *units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
  double speed = (double)bytesPerSec;
  uint8_t unitIndex = 0;
  for (unitIndex = 0; 1024 <= speed && unitIndex < 3; unitIndex++) 
    speed /= 1024.0;
  #if 1
  uint32_t speedInt = (uint32_t)speed;
  uint8_t speedDec = (uint8_t)(speed * 10) % 10;

  memset(output, 0, retMax);
  if( speedDec == 0)
    snprintf(output, retMax, "%d %s", speedInt, units[unitIndex]);
  else
    snprintf(output, retMax, "%d.%d %s", speedInt, speedDec, units[unitIndex]);
  #else
  snprintf(output, retMax, "%0.1f %s", speed, units[unitIndex]);
  #endif
  
}
