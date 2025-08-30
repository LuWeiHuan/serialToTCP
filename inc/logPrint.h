
#ifndef __LOG_PRINT_H_
#define __LOG_PRINT_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>

/*================== 宏定义声明			=========================================*/
//#define

/*================== 数据类型声明		=========================================*/
//typedef struct enum union


/*================== 外部变量声明		=========================================*/

/*================== 外部函数声明		=========================================*/
void logPrintResourceInit(bool start);
int SafePrintf(const char* format, ...) __attribute__((format(printf, 1, 2)));
void printf_hex8(const uint8_t *pdata, uint16_t len, uint8_t numEnter, uint8_t endEnter);

#ifdef __cplusplus
}
#endif

#endif /*__LOG_PRINT_H_*/







