#ifndef __DISCOVERY_H_
#define __DISCOVERY_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>

#include "platform.h"
/*================== 宏定义声明			=========================================*/
#define DISCOVERY_PORT              19000             // UDP发现端口(IPv4/IPv6共用)
#define DISCOVERY_MAGIC             "COM2TCP_SERVER"  // 魔术字标识

// IPv6组播地址（链路本地范围，所有节点）
#define DISCOVERY_IPV6_MULTICAST    "ff02::1"


/*================== 数据类型声明		=========================================*/
/*================== 外部变量声明		=========================================*/
/*================== 外部函数声明 ===========================================*/
void DiscoveryService(bool start);
bool isDiscoveryServiceSocket(socket_t sock);
uint8_t getDiscoveryServiceNewClientIPvNum(void);
int DiscoveryServiceSend(socket_t sock, const char *buf, int len);
const char *getDiscoveryServiceNewClientIPAddr(bool autoFormat);
uint16_t getDiscoveryServiceNewClientPort(void);
void DiscoveryServiceTestIsNormal(void);
#ifdef __cplusplus
}
#endif

#endif /*__DISCOVERY_H_*/