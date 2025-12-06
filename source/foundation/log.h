#ifndef __LOG_PRINT_H_
#define __LOG_PRINT_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>

/*================== 宏定义声明			=========================================*/
#define MAX_LOG_FILE_SIZE (1024*10)  // 10MB

/*================== 数据类型声明		=========================================*/
//struct enum union
typedef enum {
  LOG_LEVEL_DEBUG = 0,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_END
} LogLevel_t;

/*================== 外部变量声明		=========================================*/

/*================== 外部函数声明		=========================================*/
void SafePrintResourceInit(bool start);
int SafePrintf(const char* format, ...) __attribute__((format(printf, 1, 2)));
char *getPrintf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void printHex(const uint8_t *pdata, uint16_t len, uint8_t numEnter, uint8_t endEnter);

// 新增日志存储功能
bool logStorageInit(const char* logDir, const char* logFileName, LogLevel_t level, 
                    uint8_t maxFilesToKeep, uint8_t checkIntervalSec, uint16_t logFileSizeKB);
void logStorageUninit(void);

void logPrintFull(LogLevel_t level, const char* format, ...) __attribute__((format(printf, 2, 3)));
void logPrint(const char* format, ...) __attribute__((format(printf, 1, 2)));

void Time1SecCheckAndRotateLogFile(void);
#ifdef __cplusplus
}
#endif

#endif /*__LOG_PRINT_H_*/