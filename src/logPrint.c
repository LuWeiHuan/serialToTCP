/******************************************************************************
  * @file    文件 logPrint.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 安全的日志打印
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "logPrint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
 


/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/
/*================== 外部函数和变量声明    ==================================*/

static CRITICAL_SECTION g_log_cs;


void logPrintResourceInit(bool start)
{
  if( start )
    InitializeCriticalSection(&g_log_cs);
  else
    DeleteCriticalSection(&g_log_cs);
}

int SafePrintf(const char* format, ...)
{
    EnterCriticalSection(&g_log_cs);
    
    va_list args;
    va_start(args, format);
    int ret = vprintf(format, args);
    va_end(args);
    
    LeaveCriticalSection(&g_log_cs);
    return ret;
}


/*=============================================================================
 功   能：以16进制打印输出单字节数组
 参   数：pdata			-->字节数组
					len				-->数组长度
					numEnter 	-->显示多少个字节换行，0则不换行
					endEnter	-->打印结束后进行多少次换行
 返   回：无
 描   述：无
=============================================================================*/
void printf_hex8(const uint8_t *pdata, uint16_t len, uint8_t numEnter, uint8_t endEnter)
{
  EnterCriticalSection(&g_log_cs);
 
	uint16_t i;
	for(i = 0; i< len; i++){
		if(numEnter && i%numEnter == 0 && i!=0)
			printf("\n");
		printf("%02X ", pdata[i]);
	}
	while(endEnter--)
		printf("\n");
  LeaveCriticalSection(&g_log_cs);
}