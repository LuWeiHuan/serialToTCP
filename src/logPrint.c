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

#include <winsock2.h>
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>


/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
static CRITICAL_SECTION g_log_cs;

/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/
DWORD WINAPI ConsoleInputThread(LPVOID lpParam);
static void DisableQuickEditMode(void);
/*================== 外部函数和变量声明    ==================================*/




void logPrintResourceInit(bool start)
{
  if( start ){
    InitializeCriticalSection(&g_log_cs);
    DisableQuickEditMode();
  }
    
  else{

     DeleteCriticalSection(&g_log_cs);
  }
   
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


// 创建一个线程来定期处理控制台输入
DWORD WINAPI ConsoleInputThread(LPVOID lpParam) {
  (void)lpParam;

    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD inputRecord;
    DWORD numEvents;
    
    while (1) {
        // 检查是否有输入事件
        if (GetNumberOfConsoleInputEvents(hInput, &numEvents) && numEvents > 0) {
            // 读取但不处理输入事件，防止队列积压
            ReadConsoleInput(hInput, &inputRecord, 1, &numEvents);
            
            // 如果是鼠标点击事件，可以忽略或处理
            if (inputRecord.EventType == MOUSE_EVENT) {
                // 忽略鼠标点击，防止卡住
                // 或者可以处理特定的鼠标事件
            }
            // 如果是键盘事件，可以处理特定按键
            else if (inputRecord.EventType == KEY_EVENT && inputRecord.Event.KeyEvent.bKeyDown) {
                if (inputRecord.Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE) {
                    // ESC键处理
                    SafePrintf("\nESC pressed, exiting...\n");
                    exit(0);
                }
            }
        }
        
        Sleep(100); // 每100ms检查一次
    }
    
    return 0;
}


// 禁用快捷编辑模式，防止Win10以上系统点击控制台导致程序阻塞挂起
void DisableQuickEditMode(void) {
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    
    if (GetConsoleMode(hInput, &mode)) {
        // 清除快速编辑模式标志位
        mode &= ~ENABLE_QUICK_EDIT_MODE;
        // 启用窗口输入和鼠标输入（可选）
        mode |= ENABLE_EXTENDED_FLAGS;
        mode |= ENABLE_WINDOW_INPUT;
        mode |= ENABLE_MOUSE_INPUT;
        
        SetConsoleMode(hInput, mode);
    }
}




