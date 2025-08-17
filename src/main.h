#ifndef __MAIN_H_
#define __MAIN_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>
#include <winsock2.h>

/*================== 宏定义声明			=========================================*/
#define MAX_CLIENTS   3
#define DEFAULT_PORT  9000
#define BUFFER_SIZE   1024*10
#define CTRL_HEADER   "ctrlInfo:"
#define DECOLLATOR    ",\n"

/*================== 数据类型声明		=========================================*/
//typedef struct enum union


/*================== 外部变量声明		=========================================*/
//extern

/*================== 外部函数声明		=========================================*/
void HandleClientCommand( SOCKET clientSocket, uint8_t clientIndex, const char* command) ;


#ifdef __cplusplus
}
#endif

#endif /*__HEAK_FILE_NAME_H_*/
