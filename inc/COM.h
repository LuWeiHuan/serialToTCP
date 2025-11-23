#ifndef __COM_H_
#define __COM_H_

#include <stdint.h>
#include <stdbool.h>

/*================== 数据类型声明    ========================================*/
/*================== 外部变量声明    ========================================*/
/*================== 外部函数声明    ========================================*/

#ifdef __cplusplus  
extern "C" {
#endif
void ComPortResourceInit(bool start);
const char *getComPortList(bool VPID);
int8_t OpenComPort(const char* portName, uint32_t baudRate, uint8_t dataBits, uint8_t stopBits, uint8_t parity);
int ComPortSendData(const uint8_t *tcpRecvBuffer, int bytesReceived, uint32_t *retError);
void CloseComPort(const char * reason);

bool COM_UseAsyncRecv(uint16_t num);
bool COM_UseAsyncSend(uint16_t num);
bool getComIsOpen(void);
const char *getComName(void);

#ifdef __cplusplus
}
#endif

#endif /*__COM_H_*/