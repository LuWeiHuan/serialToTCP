#ifndef __DISCOVERY_H_
#define __DISCOVERY_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>

/*================== 宏定义声明			=========================================*/
#define DISCOVERY_PORT              19000       // UDP发现端口
//#define DISCOVERY_RESPONSE_PORT   19001       // UDP响应端口
#define DISCOVERY_MAGIC             "COMTCP_SERVER_V1.0"  // 魔术字标识

/*================== 数据类型声明		=========================================*/
/*================== 外部变量声明		=========================================*/
/*================== 外部函数声明		=========================================*/
void DiscoveryServiceStart(void);
void DiscoveryServiceStop(void);
void UpdateDiscoveryInfo(uint16_t port, uint32_t clientCount);

#ifdef __cplusplus
}
#endif

#endif /*__DISCOVERY_H_*/