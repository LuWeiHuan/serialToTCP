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
#include "platform.h"
#include "logPrint.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
GlobalTrafficStats_t trafficStats;

/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/
static threadRet WINAPI TrafficMonitorThread(void *);
static void formatSpeedString(uint64_t bytesPerSec, char* output, uint16_t retMax) ;




// 启动流量统计线程
void startTrafficMonitor(void) 
{
  memset(&trafficStats, 0, sizeof trafficStats);
  trafficStats.run = true;
  
  thread_t hThread = threadCreate(NULL, TrafficMonitorThread, NULL);
  
  if (hThread == (thread_t)0) {
    SafePrintf("Failed to create traffic monitor thread\n");
  }
}

static threadRet WINAPI TrafficMonitorThread(void * lpParam)
{
  (void)lpParam;
  uint8_t updataConsoConut = 0;
  uint64_t lastComSent = 0, lastComReceived = 0;
  uint64_t lastNetSent = 0, lastNetReceived = 0;
 
  while ( trafficStats.run ) {
    // 计算每秒流量
    uint64_t comSentPerSec = trafficStats.com.totalBytesSent - lastComSent;
    uint64_t comReceivedPerSec = trafficStats.com.totalBytesReceived - lastComReceived;
    uint64_t netSentPerSec = trafficStats.net.totalBytesSent - lastNetSent;
    uint64_t netReceivedPerSec = trafficStats.net.totalBytesReceived - lastNetReceived;
    
    lastComSent = trafficStats.com.totalBytesSent;
    lastComReceived = trafficStats.com.totalBytesReceived;
    lastNetSent = trafficStats.net.totalBytesSent;
    lastNetReceived = trafficStats.net.totalBytesReceived;
 
    formatSpeedString(comSentPerSec, trafficStats.com.sendRate, sizeof trafficStats.com.sendRate);
    formatSpeedString(comReceivedPerSec, trafficStats.com.recvRate, sizeof trafficStats.com.recvRate);
    formatSpeedString(netSentPerSec, trafficStats.net.sendRate, sizeof trafficStats.net.sendRate);
    formatSpeedString(netReceivedPerSec, trafficStats.net.recvRate, sizeof trafficStats.net.recvRate);
    
    if( ++updataConsoConut > 1 ){
      updataConsoConut = 0;
      updataConsoleTitle("TrafficMonitor");
    }
    Sleep( 1000 );
  }
  return (threadRet)0;
}

// 自动转换单位为最佳可读格式
static void formatSpeedString(uint64_t bytesPerSec, char* output, uint16_t retMax) 
{
  const char *units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
  double speed = (double)bytesPerSec;
  uint8_t unitIndex = 0;
  
  for (unitIndex = 0; 1024 <= speed && unitIndex < 3; unitIndex++) 
    speed /= 1024.0;
  
  uint32_t speedInt = (uint32_t)speed;
  uint8_t speedDec = (uint8_t)(speed * 10) % 10;

  memset(output, 0, retMax);
  if( speedDec == 0)
    snprintf(output, retMax, "%d %s", speedInt, units[unitIndex]);
  else
    snprintf(output, retMax, "%d.%d %s", speedInt, speedDec, units[unitIndex]);
}