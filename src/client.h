
#ifndef __CLIENT_H_
#define __CLIENT_H_

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


/*================== 外部变量声明		=========================================*/
//extern

/*================== 外部函数声明		=========================================*/
void ClientResourceInit(bool start);
int8_t findClientSlot(void);

int SendDataToClients(SOCKET *Socket, const char* buff, int len);
void CloseClient(uint8_t index, char *func);
void addNewClient(uint8_t index, SOCKET socket, char *ip);
int printfSend(SOCKET *Socket, const char *fmt, ...) __attribute__ ((__format__ (__printf__, 2, 3)));

void sendComPortsListToClient( SOCKET *socket);
void getAllclientIPandIndexInfo(char *retCahr, uint16_t len);
#ifdef __cplusplus
}
#endif

#endif /*__CLIENT_H_*/







