
#ifndef __HOST_CONNECT_H_
#define __HOST_CONNECT_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>

#include <winsock2.h>
/*================== 宏定义声明			=========================================*/
/*================== 数据类型声明		=========================================*/
//struct enum union

/*================== 外部变量声明		=========================================*/
//extern

/*================== 外部函数声明		=========================================*/
int8_t resolveHostname(const char* hostname, char* ipBuffer, uint8_t bufferSize, int*);
bool trueConnectToServer(const char* host, uint16_t port, 
        uint16_t timeoutMs, SOCKET *retSocket, char *retIP);
        
#ifdef __cplusplus
}
#endif

#endif /*__HOST_CONNECT_H_*/
