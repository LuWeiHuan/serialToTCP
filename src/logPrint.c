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
#include "platform.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#if MAX_ASYNC_PRINTF_LEN
#include "Queue.h"
#endif

/*================== 本地常量声明    ========================================*/
static mutex_type g_log_cs;

/*================== 本地变量声明    ========================================*/
#if MAX_ASYNC_PRINTF_LEN
static AsyncQueue_t AsyncPrintQueue;
static void AsyncPrintfCallBack(char *, uint32_t);
#endif
void DisableQuickEditMode(void);

void logPrintResourceInit(bool start)
{
  if( start ){
    InitializeCriticalSection_Wrapper(&g_log_cs);
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
    DeleteCriticalSection_Wrapper(&g_log_cs);
  }
  
}

#if MAX_ASYNC_PRINTF_LEN
static void AsyncPrintfCallBack(char *data, uint32_t len)
{ 
  fwrite(data, 1, len, stdout);
  fflush(stdout);
}
#endif

int SafePrintf(const char* format, ...)
{
  EnterCriticalSection_Wrapper(&g_log_cs);
  
#if !MAX_ASYNC_PRINTF_LEN
  va_list args;
  va_start(args, format);
  int retLen = vprintf(format, args);
  va_end(args);
  fflush(stdout);
#else 
  static char stringBuff[MAX_ASYNC_PRINTF_LEN];

  va_list args; 
  va_start(args, format);
  int retLen = vsnprintf(stringBuff, sizeof stringBuff, format, args);
  va_end(args);

  if(retLen <= 0) {
    memset(stringBuff, 0, sizeof stringBuff);
    strcpy(stringBuff, "SafePrintf Format error");
    retLen = strlen(stringBuff);
  }

  if( false == AsyncPrintQueue.running || 
      false == AddDataToAsyncQueue(&AsyncPrintQueue, stringBuff, retLen) ) {
    fwrite(stringBuff, 1, retLen, stdout);
    fflush(stdout);
  }
#endif
  
  LeaveCriticalSection_Wrapper(&g_log_cs);
  return retLen;
}

void printHex(const uint8_t *pdata, uint16_t len, uint8_t numEnter, uint8_t endEnter)
{
  EnterCriticalSection_Wrapper(&g_log_cs);
  static char outputBuffer[4096];
  static const char *hexTable = "0123456789ABCDEF";
  
  char *ptr = outputBuffer;
  uint16_t bufferRemaining = sizeof outputBuffer;
  
  for(uint16_t i = 0; i < len; i++) {
    if(numEnter && i % numEnter == 0 && i != 0)
      if(bufferRemaining > 1) {
          *ptr++ = '\n';
          bufferRemaining--;
      }
    
    if(bufferRemaining < 3) {
        *ptr = '\0'; 
        fwrite(outputBuffer, 1, ptr - outputBuffer, stdout);
        ptr = outputBuffer;
        bufferRemaining = sizeof outputBuffer;
    }
    
    *ptr++ = hexTable[pdata[i] >> 4];
    *ptr++ = hexTable[pdata[i] & 0x0F];
    *ptr++ = ' ';
    bufferRemaining -= 3;
  }
  
  if(ptr > outputBuffer) {
    *ptr = '\0';
    fwrite(outputBuffer, 1, ptr - outputBuffer, stdout);
  }
  
  for(uint8_t i = 0; i < endEnter; i++)
    putchar('\n');
  
  fflush(stdout);
  LeaveCriticalSection_Wrapper(&g_log_cs);
}

char *getPrintf(const char *format, ...)
{
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
  
  return stringBuff[buffIndex];
}

// 禁用快捷编辑模式，防止Win10以上系统点击控制台导致程序阻塞挂起
void DisableQuickEditMode(void) 
{
#ifdef _WIN32
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
#endif
}