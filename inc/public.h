
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
    uint8_t   serverPrintData;  // 0，不显示，1为字符串显示，2为Hex显示 
    SOCKET   *monopolizeSocket; // 独占串口收到的数据
    int8_t    monopolizeIndex;
    time_t    startTime;
    uint8_t   clientCount;
    uint8_t   connectCount;
} runInfo_t;

/*================== 外部变量声明		=========================================*/
extern runInfo_t runInfo;

/*================== 外部函数声明		=========================================*/
void printBuildInfo(void);
uint64_t GetCurrentTimeMillis(void);
char *getCurrentTime(void);
void updataConsoleTitle(char *threadName, DWORD theradID);
char *getSendRecvDirectionStr(char *direct, uint8_t index);
char *GetComputerFullName(void);

#ifdef __cplusplus
}
#endif

#endif /*__PUBLIC_H_*/







