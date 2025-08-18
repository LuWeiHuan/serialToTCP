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
#include "traffic.h"
#include "main.h"
#include "public.h"
#include "client.h"
#include <windows.h>

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/


GlobalTrafficStats_t trafficStats;

/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/
/*================== 外部函数和变量声明    ==================================*/
 






DWORD WINAPI TrafficMonitorThread(LPVOID lpParam)
{
  if( lpParam ){}
    const DWORD intervalMs = 1000; // 统计间隔1秒
    uint64_t lastComSent = 0, lastComRecv = 0;
    uint64_t lastClientSent[MAX_CLIENTS] = {0};
    uint64_t lastClientRecv[MAX_CLIENTS] = {0};

    while (1) {
        // 统计串口流量
        uint64_t currentComSent = trafficStats.comTraffic.totalBytesSent;
        uint64_t currentComRecv = trafficStats.comTraffic.totalBytesReceived;
        
        trafficStats.comTraffic.currentSendRate = (currentComSent - lastComSent) * 1000.0 / intervalMs;
        trafficStats.comTraffic.currentRecvRate = (currentComRecv - lastComRecv) * 1000.0 / intervalMs;
        
        formatSpeedString((uint64_t)trafficStats.comTraffic.currentSendRate, 
                         trafficStats.comTraffic.sendRateStr);
        formatSpeedString((uint64_t)trafficStats.comTraffic.currentRecvRate, 
                         trafficStats.comTraffic.recvRateStr);
        
        lastComSent = currentComSent;
        lastComRecv = currentComRecv;

        // 统计各客户端流量
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].socket == INVALID_SOCKET) continue;
            
            uint64_t currentSent = trafficStats.clients[i].totalBytesSent;
            uint64_t currentRecv = trafficStats.clients[i].totalBytesReceived;
            
            trafficStats.clients[i].currentSendRate = 
                (currentSent - lastClientSent[i]) * 1000.0 / intervalMs;
            trafficStats.clients[i].currentRecvRate = 
                (currentRecv - lastClientRecv[i]) * 1000.0 / intervalMs;
            
            formatSpeedString((uint64_t)trafficStats.clients[i].currentSendRate,
                            trafficStats.clients[i].sendRateStr);
            formatSpeedString((uint64_t)trafficStats.clients[i].currentRecvRate,
                            trafficStats.clients[i].recvRateStr);
            
            lastClientSent[i] = currentSent;
            lastClientRecv[i] = currentRecv;
        }

        Sleep(intervalMs);
    }
    return 0;
}

// 启动流量统计线程
void StartTrafficMonitor(void) {
  memset(&trafficStats, 0, sizeof(trafficStats));
  CreateThread(NULL, 0, TrafficMonitorThread, NULL, 0, NULL);
}