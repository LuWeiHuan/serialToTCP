#ifndef __CLIENTS_H_
#define __CLIENTS_H_

#include <stdint.h>
#include <stdbool.h>

#include "platform.h"

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 数据类型声明    ========================================*/
typedef struct {
  uint16_t count;
  uint16_t max;
} ClientsNum_t;

extern ClientsNum_t const * const g_clientsNum;
#define getClientNum() g_clientsNum->count
#define getMaxClient() g_clientsNum->max

/*================== 外部函数声明    ========================================*/
void ClientResourceInit(bool start);
bool addNewClient(socket_t socket, const char *ip);
void CloseClientSocket(const socket_t *socket, const char *reason);

int sendDataToClients(const socket_t *Socket, const char* buff, int len);
int printfSend(const socket_t *Socket, const char *fmt, ...);

bool getClientIndex(const socket_t *Socket, uint16_t *retIndex);
const socket_t *getClientSocket(uint16_t index);
const char *getClientIP(uint16_t index);
void getAllClientIPandIndexInfo(const socket_t *Socket, char *retStr, uint16_t len);
void KickAllClients(const char* reason);

void sendComPortsListToClient(const socket_t *socket, bool VPID);
#ifdef __cplusplus
}
#endif

#endif /*__CLIENTS_H_*/