
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
/*================== 数据类型声明		=========================================*/
//struct enum union
/*================== 外部变量声明		=========================================*/
/*================== 外部函数声明		=========================================*/
void ClientResourceInit(bool start);
void addNewClient(SOCKET socket, const char *ip);

int SendDataToClients(SOCKET *Socket, const char* buff, int len);
int printfSend(SOCKET *Socket, const char *fmt, ...) __attribute__ ((__format__ (__printf__, 2, 3)));

void examineMonopolizeClient(void);

uint16_t getMaxClient(void);
uint16_t getClientNum(void);
const char *getClientIP(uint8_t index);
void getAllclientIPandIndexInfo(char *retStr, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /*__CLIENT_H_*/







