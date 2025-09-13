
#ifndef __COMMAND_H_
#define __COMMAND_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>
#include <winsock2.h>

/*================== 宏定义声明			=========================================*/
#define CTRL_HEADER         "ctrlInfo:"

/*================== 数据类型声明		=========================================*/
//struct enum union

/*================== 外部变量声明		=========================================*/
//extern

/*================== 外部函数声明		=========================================*/
void HandleClientCommand(SOCKET *clientSocket, uint8_t clientIndex, const char* command);

#ifdef __cplusplus
}
#endif

#endif /*__COMMAND_H_*/
