
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

typedef struct {
    SOCKET socket;
    HANDLE hThread;
    DWORD threadId;
    uint8_t index;
    __int64 connectTime;    // 连接时间（毫秒级时间戳）
    char ipAddress[16];     // 存储IPv4地址（如"192.168.1.1"）
} ClientInfo_t;



/*================== 外部变量声明		=========================================*/
extern ClientInfo_t clients[]; 

/*================== 外部函数声明		=========================================*/
void ClientResourceInit(bool start);
int8_t findClientSlot(void);

int SendDataToClients(SOCKET *Socket, const char* buff, int len);
void CloseClient(uint8_t index, char *func);
void addNewClient(uint8_t index, SOCKET socket, char *ip);
int printfSend(SOCKET *Socket, const char *fmt, ...);

void sendComPortsListToClient( SOCKET *socket);
#ifdef __cplusplus
}
#endif

#endif /*__CLIENT_H_*/







