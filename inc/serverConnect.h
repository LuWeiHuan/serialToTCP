#ifndef __SERVER_CONNECT_H_
#define __SERVER_CONNECT_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含     =========================================*/
#include <stdint.h>
#include <stdbool.h>
#include <winsock2.h>

/*================== 宏定义声明     =========================================*/
#define CONNECT_TIMEOUT_MS     5000    // 连接超时时间（毫秒）

/*================== 数据类型声明   =========================================*/
// 连接状态枚举
typedef enum { 
  CONNECT_STATE_FAILURE_DISCONNECTED = 0,  // 失败与断接
  CONNECT_STATE_CONNECTING ,  // 连接中
  CONNECT_STATE_CONNECTED  ,  // 已连接 
} ConnectState_t;

typedef void(*connectResultCallback)(ConnectState_t state, 
                  void*arg, const char * IP, uint16_t port, uint16_t residueTimeMs);

/*================== 外部变量声明   =========================================*/
//extern

/*================== 外部函数声明   =========================================*/
void ServerConnectInit(bool);
void ConnectToServer(const char* host, uint16_t port, 
          connectResultCallback connectResult, void *arg);

bool ResolveDomainName(const char* domain, char* ipBuffer, uint8_t bufferSize);

#ifdef __cplusplus
}
#endif

#endif /*__SERVER_CONNECT_H_*/
