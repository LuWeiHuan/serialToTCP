#ifndef __PUBLIC_H_
#define __PUBLIC_H_

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 数据类型声明    ========================================*/
typedef struct {
  time_t    startTimeSec;               // 程序启动时间
  uint16_t *monopolizeComRecvIndex;  // 独享 串口收到的数据
  uint16_t *monopolizeComSendIndex;  // 独享 数据发给串口  
} runInfo_t;

/*================== 外部变量声明    ========================================*/
extern runInfo_t runInfo;

/*================== 外部函数声明    ========================================*/
void printBuildInfo(void);
uint64_t getRuningTimeMs(void);
char *getCurrentTimeStringSec(void);
void updataConsoleTitle(const char *threadName);
char *getSendRecvDirectionStr(const char *direct, uint8_t index);
const char *getComputerFullName(void);
void GetAllLocalIPs(char ips[][20], uint8_t *count, uint8_t num);

const char *GetSystemUniqueIdentifier(void);
uint8_t getWindowsVersionSimple(char *retStr);

// 平台函数
bool Platform_Initialize(void);
void Platform_Cleanup(void);

#ifdef _WIN32
bool SetConsoleFontSize(int width, int height);
bool EnableVTMode(void);
#endif

void ErrorCodeTest(void);

#ifdef __cplusplus
}
#endif

#endif /*__PUBLIC_H_*/