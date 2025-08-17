
#ifndef __COM_H_
#define __COM_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>

#include <winsock2.h>

/*================== 宏定义声明			=========================================*/
//#define

/*================== 数据类型声明		=========================================*/
//typedef struct enum union
typedef struct {
    HANDLE hCom;
    BOOL isOpen;
    char portName[10];
    DCB dcb;
    HANDLE hThread;
    DWORD threadId;
} ComPortInfo_t;


/*================== 外部变量声明		=========================================*/
extern ComPortInfo_t comPort;

/*================== 外部函数声明		=========================================*/
void ComPortResourceInit(bool start);
char *getComPortList(void);
int8_t OpenComPort(const char* portName, uint32_t baudRate, uint8_t dataBits, uint8_t stopBits, uint8_t parity);
void CloseComPort();

DWORD ComPortSendData(char const *tcpRecvBuffer, int bytesReceived, DWORD *retError);

void setSendDataToCOMwhetherWait(bool wait);
bool InitSerialCompletionPort();

#ifdef __cplusplus
}
#endif

#endif /*__HEAK_FILE_NAME_H_*/







