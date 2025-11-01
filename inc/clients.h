
#ifndef __CLIENTS_H_
#define __CLIENTS_H_

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
  uint16_t count;
  uint16_t max;
}ClientsNum_t;

/*================== 外部变量声明		=========================================*/


/*================== 外部函数声明		=========================================*/
void ClientResourceInit(bool start);
bool addNewClient(SOCKET socket, const char *ip);
void CloseClientSocket(const SOCKET *socket, const char *reason);

int sendDataToClients(const SOCKET *Socket, const char* buff, int len);
int printfSend(const SOCKET *Socket, const char *fmt, ...) __attribute__ ((__format__ (__printf__, 2, 3)));

bool getClientIndex(const SOCKET *Socket, uint16_t *retIndex);
const SOCKET *getClientSocket(uint16_t index);
const char *getClientIP(uint16_t index);
void getAllClientIPandIndexInfo(char *retStr, uint16_t len);
void KickAllClients(const char* reason);

extern ClientsNum_t const * const g_clientsNum;
#if 0
inline uint16_t getClientNum(void){ 
  extern ClientsNum_t const * const g_clientsNum;
  return g_clientsNum->count;
}

inline uint16_t getMaxClient(void){
  extern ClientsNum_t const * const g_clientsNum;
  return g_clientsNum->max;
}
#else
#define getClientNum() g_clientsNum->count
#define getMaxClient() g_clientsNum->max
#endif
#ifdef __cplusplus
}
#endif

#endif /*__CLIENTS_H_*/







