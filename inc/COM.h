
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
/*================== 数据类型声明		=========================================*/
//struct enum union
typedef struct {
    HANDLE hCom;
    BOOL isOpen;
    char portName[10];
    DCB dcb;
    HANDLE hThread;
    DWORD threadId;
    uint64_t sendCount;
} ComPortInfo_t;

/*================== 外部变量声明		=========================================*/
extern ComPortInfo_t const * const ComPort;

/*================== 外部函数声明		=========================================*/
void ComPortResourceInit(bool start);
const char *getComPortList(bool VPID);
int8_t OpenComPort(const char* portName, uint32_t baudRate, uint8_t dataBits, uint8_t stopBits, uint8_t parity);
DWORD ComPortSendData(char const *tcpRecvBuffer, int bytesReceived, DWORD *retError);
void CloseComPort(const char * reason, bool isSelfCall);

BOOL COM_UseAsyncRecv(uint16_t num);
BOOL COM_UseAsyncSend(uint16_t num);

void sendComPortsListToClient(SOCKET *socket, bool VPID);

#ifdef __cplusplus
}
#endif

#endif /*__HEAK_FILE_NAME_H_*/







