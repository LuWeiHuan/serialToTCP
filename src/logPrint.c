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

/*================== 本地宏定义     =========================================*/
//#define MAX_ASYNC_PRINTF_LEN 1024*10  // 异步队列发送

/*================== 头文件包含     =========================================*/
#include "logPrint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#if MAX_ASYNC_PRINTF_LEN
#include "Queue.h"
#endif

#include <windows.h>



/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
static CRITICAL_SECTION g_log_cs;


/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/
DWORD WINAPI ConsoleInputThread(void *lpParam);
static void DisableQuickEditMode(void);

#if MAX_ASYNC_PRINTF_LEN
static AsyncQueue_t AsyncPrintQueue;
static void AsyncPrintfCallBack(char *, uint32_t);
#endif

/*================== 外部函数和变量声明    ==================================*/

void logPrintResourceInit(bool start)
{
  if( start ){
    InitializeCriticalSection(&g_log_cs);
    #if MAX_ASYNC_PRINTF_LEN 
    startAsyncQueue(&AsyncPrintQueue, 
        AsyncPrintfCallBack, 200, MAX_ASYNC_PRINTF_LEN, "Printf");
    #endif
    DisableQuickEditMode();
  }
  else{ 
    #if MAX_ASYNC_PRINTF_LEN
    FreeAsyncQueue(&AsyncPrintQueue);
    #endif
    DeleteCriticalSection(&g_log_cs);
  }
}

#if MAX_ASYNC_PRINTF_LEN
typedef struct {
    const char* format;
    va_list args;
} AsyncPrintfData_t;

static void AsyncPrintfCallBack(char *data, uint32_t len)
{ 
  fwrite(data, 1, len, stdout);
}
#endif

int SafePrintf(const char* format, ...)
{
  EnterCriticalSection(&g_log_cs);
  #if !MAX_ASYNC_PRINTF_LEN
  va_list args;
  va_start(args, format);
  int retLen = vprintf(format, args);
  va_end(args);
  #else 
  static char stringBuff[MAX_ASYNC_PRINTF_LEN];

  va_list args; 
  va_start(args, format);
  int retLen = vsnprintf(stringBuff, sizeof stringBuff, format, args);
  va_end(args);

  if(retLen <= 0) {
    memset(stringBuff, 0, sizeof stringBuff);
    strcpy(stringBuff, "SafePrintf Format error");
  }
  else
    stringBuff[retLen] = '\0';

  if( false == AsyncPrintQueue.running || 
      false == AddDataToAsyncQueue(&AsyncPrintQueue, stringBuff, retLen) ) 
    fwrite(stringBuff, 1, retLen, stdout); 
  #endif
  LeaveCriticalSection(&g_log_cs);
  return retLen;
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
void printHex(const uint8_t *pdata, uint16_t len, uint8_t numEnter, uint8_t endEnter)
{
  EnterCriticalSection(&g_log_cs);
  static char outputBuffer[4096];  // 更大的缓冲区，可以容纳更多数据
  static const char *hexTable = "0123456789ABCDEF";  // 十六进制字符表
  
  char *ptr = outputBuffer;
  uint16_t bufferRemaining = sizeof outputBuffer;
  
  for(uint16_t i = 0; i < len; i++) {
    // 检查是否需要换行
    if(numEnter && i % numEnter == 0 && i != 0)
      if(bufferRemaining > 1) {
          *ptr++ = '\n';
          bufferRemaining--;
      }
    
    // 检查缓冲区是否足够存放当前字节的十六进制表示（3字符：XX+空格）
    if(bufferRemaining < 3) {
        // 缓冲区不足，先输出已缓存的内容
        *ptr = '\0'; 
        fwrite(outputBuffer, 1, ptr - outputBuffer, stdout);
        ptr = outputBuffer;
        bufferRemaining = sizeof outputBuffer;
    }
    
    // 将字节转换为十六进制
    *ptr++ = hexTable[pdata[i] >> 4];  // 高4位
    *ptr++ = hexTable[pdata[i] & 0x0F];         // 低4位
    *ptr++ = ' ';                               // 空格分隔
    bufferRemaining -= 3;
  }
  
  // 输出缓冲区中剩余的内容
  if(ptr > outputBuffer) {
    *ptr = '\0';
    fwrite(outputBuffer, 1, ptr - outputBuffer, stdout);
  }
  
  // 输出结束换行
  for(uint8_t i = 0; i < endEnter; i++)
    putchar('\n');
  
  LeaveCriticalSection(&g_log_cs);
}

/**
 * @brief  格式化字符串并返回字符串空间
 * @param 
 *		@arg printf 格式
 * @retval 格式化后的字符串
 * 注意每个线程尽量不要嵌套超过6次容易字符串混淆
 */
char *getPrintf(const char *format, ...)
{
  // 使用线程局部存储，每个线程有自己的副本
  // 线程副本会让程序体积增加不少
  static __thread uint8_t buffIndex = 0;
  static __thread char stringBuff[6][1024];

  if( ++buffIndex >= sizeof stringBuff / sizeof stringBuff[0] )
    buffIndex = 0;
  
  va_list args; 
  va_start(args, format);
  int retLen = vsnprintf(stringBuff[buffIndex], sizeof stringBuff[buffIndex], format, args);
  va_end(args);

  if(retLen <= 0) {
    memset(stringBuff[buffIndex], 0, sizeof stringBuff[buffIndex]);
    strcpy(stringBuff[buffIndex], "get Printf Format error");
  }
  else
    stringBuff[buffIndex][retLen] = '\0';
  
  return stringBuff[buffIndex];
}






// 创建一个线程来定期处理控制台输入
DWORD WINAPI ConsoleInputThread(void *lpParam) 
{
  (void)lpParam;

  HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
  INPUT_RECORD inputRecord;
  DWORD numEvents;
  
  while ( true ) {
    Sleep(100); // 每100ms检查一次

    // 检查是否有输入事件
    if (GetNumberOfConsoleInputEvents(hInput, &numEvents) == false || numEvents == 0)
      continue;
     
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
  
  return 0;
}


// 禁用快捷编辑模式，防止Win10以上系统点击控制台导致程序阻塞挂起
void DisableQuickEditMode(void) 
{
  HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
    
  if (GetConsoleMode(hInput, &mode) == false) 
    return;
  // 清除快速编辑模式标志位
  mode &= ~ENABLE_QUICK_EDIT_MODE;
  // 启用窗口输入和鼠标输入（可选）
  mode |= ENABLE_EXTENDED_FLAGS;
  mode |= ENABLE_WINDOW_INPUT;
  mode |= ENABLE_MOUSE_INPUT;
  
  SetConsoleMode(hInput, mode); 
}