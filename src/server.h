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
//typedef struct enum union


/*================== 外部变量声明		=========================================*/
//extern

/*================== 外部函数声明		=========================================*/
int ParsePortParameter(int argc, char const* argv[]); 
int serverInit(int port, SOCKET *ServerSocket);
int8_t listenNewClientLink( SOCKET *ServerSocket, SOCKET * retSocket);

#ifdef __cplusplus
}
#endif

#endif /*__HEAK_FILE_NAME_H_*/
