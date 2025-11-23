#ifndef __MAIN_H_
#define __MAIN_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>

/*================== 宏定义声明			=========================================*/
#define MAX_CLIENTS         50
#define DEFAULT_PORT        9000
#define RECV_BUFFER_SIZE    1024*100+1

#define VERSIONS            "V0.5"

#if MAX_CLIENTS<3
#error "请不要把客户端数量设置那么少，至少给3个嘛，老铁！"
#endif

/*================== 数据类型声明		=========================================*/
//struct enum union

/*================== 外部变量声明		=========================================*/
/*================== 外部函数声明		=========================================*/
void voluntaryWithdrawal(const char *reason );
static inline uint16_t getMainServerPort(void)
{
  extern const uint16_t * const mainServerPort;
  return *mainServerPort;
}
#ifdef __cplusplus
}
#endif

#endif /*__MAIN_H_*/
