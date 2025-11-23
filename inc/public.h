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
  time_t    startTime;
  uint8_t   serverPrintData;  // 0，不显示，1为字符串显示，2为Hex显示，3只显示命令 
  uint16_t *monopolizeComRecvIndex;  // 独享 串口收到的数据
  uint16_t *monopolizeComSendIndex;  // 独享 数据发给串口 
  bool      COMsendPoll;    // 就是就是发给串口的日志要不要滚动
  bool      COMrecvPoll;    // 就是串口发上来的每条数据条目要不要滚动
  uint32_t  COMrecv4Knum;  // 设置串口接收多少个4096字节数就发送
} runInfo_t;

/*================== 外部变量声明    ========================================*/
extern runInfo_t runInfo;

/*================== 外部函数声明    ========================================*/
void printBuildInfo(void);
uint64_t getRuningTimeMs(void);
char *getCurrentTimeStringSec(void);
void updataConsoleTitle(const char *threadName);
char *getSendRecvDirectionStr(char *direct, uint8_t index);
const char *getComputerFullName(void);
void GetAllLocalIPs(char ips[][20], uint8_t *count, uint8_t num);

const char* GetSystemUniqueIdentifier(void);

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