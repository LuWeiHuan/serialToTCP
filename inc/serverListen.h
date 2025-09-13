#ifndef __SERVER_H_
#define __SERVER_H_

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
typedef struct {
  uint16_t  port;
  SOCKET    socket;
  SOCKET    newSocket;
  char      newIP[20];
}server_t;
/*================== 外部变量声明		=========================================*/
extern server_t g_server;

/*================== 外部函数声明		=========================================*/
uint16_t ParsePortParameter(int argc, char const* argv[]); 
bool serverInit(server_t*);
int8_t listenNewClientConnect(server_t*);

#ifdef __cplusplus
}
#endif

#endif /*__SERVER_H_*/
