#ifndef __SERVER_LISTEN_H_
#define __SERVER_LISTEN_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>
#include "platform.h"

/*================== 宏定义声明			=========================================*/

/*================== 数据类型声明		=========================================*/
//struct enum union
typedef struct {
  uint16_t  port;
  socket_t  socket;
  socket_t  newSocket;
  char      newIP[20];
} serverInfo_t;
/*================== 外部变量声明		=========================================*/

/*================== 外部函数声明		=========================================*/
uint16_t ParsePortParameter(int argc, char const* argv[]); 
bool serverStart(serverInfo_t*);
int8_t listenNewClientConnect(serverInfo_t*, uint8_t);
void serverCleanup(serverInfo_t *server); 

#ifdef __cplusplus
}
#endif

#endif /*__SERVER_H_*/