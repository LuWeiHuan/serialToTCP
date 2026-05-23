
#ifndef __TRAFFIC_STATS_H_
#define __TRAFFIC_STATS_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>

/*================== 宏定义声明			=========================================*/

/*================== 数据类型声明		=========================================*/
//struct enum union
typedef struct {
  uint64_t totalBytesSent;      // 总发送字节数
  uint64_t totalBytesReceived;  // 总接收字节数
  char sendRate[15];            // 格式化后的发送速率字符串（如"1.23 MB/s"）
  char recvRate[15];            // 格式化后的接收速率字符串
} TrafficStats_t;

// 全局流量统计
typedef struct {
  TrafficStats_t com;   // 串口流量
  TrafficStats_t net;   // 网络流量
} GlobalTrafficStats_t;

/*================== 外部变量声明		=========================================*/
extern GlobalTrafficStats_t trafficStats;

/*================== 外部函数声明		=========================================*/
void Time1SecUpdataTrafficMonitor(void* arg);

#ifdef __cplusplus
}
#endif

#endif /*__TRAFFIC_STATS_H_*/







