#ifndef __COMMON_UTILS_H_
#define __COMMON_UTILS_H_

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 数据类型声明    ========================================*/
/*================== 外部变量声明    ========================================*/


/*================== 外部函数声明    ========================================*/
void start1SecRunOneThread(void);
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

#endif /* __COMMON_UTILS_H_ */