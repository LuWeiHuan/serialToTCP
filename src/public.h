
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
//#define

/*================== 数据类型声明		=========================================*/
//typedef struct enum union
typedef struct {
    uint8_t serverPrintData; // 0，不显示，1为字符串显示，2为Hex显示
    uint8_t clientCount;
    SOCKET *monopolizeSoclet; // 独占串口收到的数据
    int8_t  monopolizeIndex;
    uint16_t port;
    time_t startTime;
    uint32_t linkCount;
} runInfo_t;





/*================== 外部变量声明		=========================================*/
extern runInfo_t  runInfo;

/*================== 外部函数声明		=========================================*/
void print_build_info(void) ;
__int64 GetCurrentTimeMillis(void);
char *getCurrentTime(void) ;
void updataConsoleTitle(char *threadName, DWORD theradID);
char *getSendRecvDirectionStr(char *direct, uint8_t index);

void formatSpeedString(uint64_t bytesPerSec, char* output);

#ifdef __cplusplus
}
#endif

#endif /*__PUBLIC_H_*/







