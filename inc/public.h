
#ifndef __PUBLIC_H_
#define __PUBLIC_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>

#include <winsock2.h>
#include <windows.h>

/*================== 宏定义声明			=========================================*/

/*================== 数据类型声明		=========================================*/
//struct enum union
typedef struct {
  time_t    startTime;
  uint8_t   serverPrintData;  // 0，不显示，1为字符串显示，2为Hex显示，3只显示命令 
  uint16_t *monopolizeComRecvIndex;  // 独享 串口收到的数据
  uint16_t *monopolizeComSendIndex;  // 独享 数据发给串口
  bool    COMrecvPoll;  // 就是串口发上来的每条数据条目要不要滚动
  bool    COMSendPoll;  // 就是就是发给串口的日志要不要滚动
} runInfo_t;

/*================== 外部变量声明		=========================================*/
extern runInfo_t runInfo;

/*================== 外部函数声明		=========================================*/
void printBuildInfo(void);
uint64_t GetCurrentTimeMs(void);
char *getCurrentTime(void);
void updataConsoleTitle(const char *threadName, DWORD theradID);
char *getSendRecvDirectionStr(char *direct, uint8_t index);
const char *GetComputerFullName(void);
bool InitializeWinSocket(void);
#ifdef __cplusplus
}
#endif

#endif /*__PUBLIC_H_*/







