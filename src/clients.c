/******************************************************************************
  * @file    文件 clients.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 连接的客户端
  ******************************************************************************
  * @attention 注意
  *
  * 日志：2025-09-17
  * 客户端列表使用的是数组，改成内核链链表作为客户端列表，新客户端添加到尾，
  * 这样客户端满了，剔除最早的客户端基本就是最早添加的节点。
  * 设计一个实现定义好的客户端数组，链表的节点就从数组里面寻找空闲的数组成员，
  * 这样可以避免频繁的申请内存，串口出来的数据发送给所有客户端也会很高效。
  * 
  * 如果需要进一步优化，可以考虑：
  * 使用双向链表以便更快的删除操作
  * 添加心跳机制自动清理死连接
  * 使用线程池处理客户端数据（但当前单线程 per client 模型简单可靠）
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "client.h"
#include "main.h"
#include "public.h"
#include "logPrint.h"
#include "COM.h"
#include "TrafficStats.h"

#include "Command.h"
#include <stdio.h>

#include <winsock2.h>
#include <windows.h>

/*================== 本地数据类型   =========================================*/
typedef struct ClientNode {
    SOCKET socket;
    HANDLE hThread;
    DWORD threadId;
    uint16_t index;          // 在客户端池里中的索引
    uint64_t connectTime;
    char ipAddress[16];
    char tcpRecvBuffer[RECV_BUFFER_SIZE];
    struct ClientNode* next;
} ClientNode_t;

typedef struct {
    ClientNode_t* head;
    ClientNode_t* tail;
    uint16_t      count;
} ClientList_t;

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static ClientList_t clientList = {0};
static ClientNode_t *clientPool = NULL;
static uint16_t maxClients = MAX_CLIENTS;
static uint16_t clientNumCount = 0;

static CRITICAL_SECTION csClient;

/*================== 本地函数声明    ========================================*/
static void ClientList_Init(void);
static void ClientList_Add(ClientNode_t* node);
static void ClientList_Remove(ClientNode_t* node);
static ClientNode_t* ClientList_GetOldest(void);
static ClientNode_t* ClientPool_Alloc(void);
static void ClientPool_Free(ClientNode_t* node);

static void CloseClient(ClientNode_t* node, const char *reason);

/*================== 外部函数和变量声明    ==================================*/

uint16_t getMaxClient(void)
{
  return maxClients;
}

uint16_t getClientNum(void)
{
  return clientNumCount;
}



const char *getClientIP(uint8_t index)
{
  return index < maxClients? clientPool[index].ipAddress:"NULL index";
}

void ClientResourceInit(bool start) 
{
  static ClientNode_t LocalClientPool[3] = {0};
  if (start) {
      clientPool = malloc( sizeof(ClientNode_t) * maxClients );
      // SafePrintf("malloc client Pool %s! %I64d KByte\n", clientPool? "succeed":"fail", 
      //   (sizeof(ClientNode_t) * maxClients) / 1024);
      if( clientPool == NULL){
        SafePrintf("malloc client Pool fail!\n");
        clientPool = LocalClientPool;
        maxClients = sizeof LocalClientPool / sizeof LocalClientPool[0];
      }

      ClientList_Init();
      InitializeCriticalSection(&csClient);
  } else {
      // 清理所有客户端
      EnterCriticalSection(&csClient);
      ClientNode_t* curr = clientList.head;
      while (curr) {
          ClientNode_t* next = curr->next;
          CloseClient(curr, "清理客户端");
          curr = next;
      }
      LeaveCriticalSection(&csClient);
      
      if( clientPool != LocalClientPool )
        free(clientPool);
      DeleteCriticalSection(&csClient);
  }
}

static ClientNode_t* ClientPool_Alloc(void) 
{
  for (int i = 0; i < maxClients; i++) 
    if (clientPool[i].socket == INVALID_SOCKET) 
        return &clientPool[i];
  return NULL; // 池满
}

static void ClientPool_Free(ClientNode_t* node) 
{
    if (node && node->index < maxClients) {
        node->socket = INVALID_SOCKET;
        node->hThread = NULL;
        memset(node->ipAddress, 0, sizeof(node->ipAddress));
    }
}


static void ClientList_Add(ClientNode_t* node) 
{
    if (!node) return;
    node->next = NULL;
    if (!clientList.head) {
        clientList.head = node;
        clientList.tail = node;
    } else {
        clientList.tail->next = node;
        clientList.tail = node;
    }
    clientList.count++;
}

static void ClientList_Remove(ClientNode_t* node) 
{
    if (!node || !clientList.head) return;

    if (clientList.head == node) {
        clientList.head = node->next;
        if (clientList.tail == node)
            clientList.tail = NULL;
    }
    else {
        ClientNode_t* prev = clientList.head;
        while (prev->next != node) prev = prev->next;
        prev->next = node->next;
        if (clientList.tail == node)
            clientList.tail = prev;
    }
    clientList.count--;
    ClientPool_Free(node);
}

// 检查独占客户端是否存在
void examineMonopolizeClient(void)
{
  BOOL clientStillExists = FALSE;
  EnterCriticalSection(&csClient);
  ClientNode_t* curr = clientList.head;
  while (curr) {
      if (&curr->socket == runInfo.monopolizeSocket || 
          curr->socket == *runInfo.monopolizeSocket) {
          clientStillExists = TRUE;
          break;
      }
      curr = curr->next;
  }
  LeaveCriticalSection(&csClient);
  
  if (!clientStillExists) {
      runInfo.monopolizeSocket = NULL;
      SafePrintf("Monopolize client disconnected, switching to all clients\n");
  } 
}

// 获取最早的客户端
static ClientNode_t* ClientList_GetOldest(void) {
    ClientNode_t* oldest = NULL;
    ClientNode_t* curr = clientList.head;
    while (curr) {
        if (!oldest || curr->connectTime < oldest->connectTime)
            oldest = curr;
        curr = curr->next;
    }
    return oldest;
}


static void ClientList_Init(void) {
    clientList.head = NULL;
    clientList.tail = NULL;
    clientList.count = 0;
    // 初始化空闲池
    for (int i = 0; i < maxClients; i++) {
        clientPool[i].socket = INVALID_SOCKET;
        clientPool[i].index = i;
        clientPool[i].next = NULL;
    }
}

static DWORD WINAPI ClientRecvDataThread(LPVOID lpParam) 
{
  if (lpParam == NULL) { 
      SafePrintf("client thread not Client info introduction\n");
      return -1;
  }
  
  ClientNode_t* clientInfo = (ClientNode_t*)lpParam; 
  int bytesReceived = 0, retSelect, WSAerror;
  fd_set readSet;
  struct timeval timeout;
  uint64_t sendCount = 0;
  char infoString[100];
  // 设置socket为非阻塞模式
  u_long mode = 1; // 1表示非阻塞，0表示阻塞
  if (ioctlsocket(clientInfo->socket, FIONBIO, &mode) != 0) {
    memset(infoString, 0, sizeof infoString);
    snprintf(infoString, sizeof infoString, 
        "客户端设置非阻塞失败，接收：%d，WAS代码：:%d ", 
        bytesReceived, WSAGetLastError());
    CloseClient(clientInfo, infoString); 
    return -1;
  }

  // 发送连接成功消息  
  printfSend(&clientInfo->socket, "OK! your index %d\n", clientInfo->index);
  sendComPortsListToClient( &clientInfo->socket, true );  // 像该客户端发送可用端口号

  while ( clientInfo->tcpRecvBuffer != NULL ) {
    // 检查客户端socket是否仍然有效
    if (clientInfo->socket == INVALID_SOCKET)
        break;

    FD_ZERO(&readSet);
    FD_SET(clientInfo->socket, &readSet);

    // 设置超时时间为100毫秒
    timeout.tv_sec = 1;
    timeout.tv_usec = 0; // 100毫秒

    retSelect = select(0, &readSet, NULL, NULL, &timeout);
    if (retSelect == SOCKET_ERROR) { 
      WSAerror = WSAGetLastError();
      memset(infoString, 0, sizeof infoString);
      snprintf(infoString, sizeof infoString, 
          "线程退出，客户端选择错误，接收：%d，WAS代码：:%d ", 
          bytesReceived, WSAerror);
      break;
    }
    else if (retSelect == 0) {  // 超时，没有数据可读，继续循环 
        memset(infoString, 0, sizeof infoString);
        sprintf(infoString, "client %d ", clientInfo->index);
        updataConsoleTitle(infoString, GetCurrentThreadId());
        continue;
    }

    // 有数据可读
    bytesReceived = recv(clientInfo->socket, clientInfo->tcpRecvBuffer, 
                                      sizeof clientInfo->tcpRecvBuffer - 1, 0);
    if (bytesReceived == 0) { // 客户端正常关闭连接 
        WSAerror = WSAGetLastError();
        memset(infoString, 0, sizeof infoString);
        snprintf(infoString, sizeof infoString, 
            "线程退出，优雅地断开连接，WSA代码：%d，接收：%d", 
            WSAerror, bytesReceived);
        break;
    }
    else if (bytesReceived <= SOCKET_ERROR) {
        WSAerror = WSAGetLastError();
        if (WSAerror == WSAEWOULDBLOCK) 
            continue;       // 非阻塞模式下没有数据是正常情况 
        else if (WSAerror == WSAECONNRESET || WSAerror == WSAECONNABORTED) {
            // 连接被重置或中止
            memset(infoString, 0, sizeof infoString);
            snprintf(infoString, sizeof infoString, 
                "线程退出，连接被重置，WSA代码：%d，接收：%d", 
                WSAerror, bytesReceived);
            break;
        } 
        else { // 其他错误，断开连接 
            memset(infoString, 0, sizeof infoString);
            snprintf(infoString, sizeof infoString, 
                "线程退出，接收错误，WSA代码：%d，接收：%d", 
                WSAerror, bytesReceived);
            break;
        }
    }

    // 正常接收到数据
    clientInfo->tcpRecvBuffer[bytesReceived] = '\0';  // 防止命令解析异常

    // 检查是否是控制命令
    if (strncmp(clientInfo->tcpRecvBuffer, CTRL_HEADER, strlen(CTRL_HEADER)) == 0) {
        if (runInfo.serverPrintData == 3) 
          SafePrintf("%s\n", clientInfo->tcpRecvBuffer);
        HandleClientCommand(&clientInfo->socket, clientInfo->index, 
                          clientInfo->tcpRecvBuffer + strlen(CTRL_HEADER));
        continue;
    }
    
    // 判断串口是否已经打开
    if (comPort.isOpen == FALSE) {
      printfSend(&clientInfo->socket, "COM not open !\n");
      continue;
    }

    // 普通数据，发送到串口  
    DWORD getError = 0;
    DWORD bytesWritten = ComPortSendData(clientInfo->tcpRecvBuffer, bytesReceived, &getError);
    if( bytesWritten != (DWORD)bytesReceived )
      printfSend(&clientInfo->socket, "COM write error: %ld\n", getError);

    char *Direct = getSendRecvDirectionStr("[TCP --> COM]", clientInfo->index);
    char *timeStr = getCurrentTime();
    strcat(timeStr, " " );

    SafePrintf("%s%6I64d [%s]  %-6ld/%-6d Byte (%s : %ld)%s\n", timeStr, ++sendCount, Direct,
            bytesWritten, bytesReceived, bytesWritten == (DWORD)bytesReceived? "OK" : "Fail", getError,
            runInfo.serverPrintData != 0 ? " data:" : " ");
            
    if (runInfo.serverPrintData != 0) {
        if (runInfo.serverPrintData == 1) 
          SafePrintf("%s", clientInfo->tcpRecvBuffer);
          
        if (runInfo.serverPrintData == 2) 
          printf_hex8((uint8_t*)clientInfo->tcpRecvBuffer, bytesReceived, 40, 2);
    }

    // 收到客户端数据时（发送到串口）
    #ifdef __TRAFFIC_STATS_H_
    trafficStats.net.totalBytesReceived += bytesReceived;
    trafficStats.com.totalBytesSent += bytesWritten;
    #endif
  }

  CloseClient(clientInfo, infoString);
  return 0;
}

void addNewClient(SOCKET socket, const char *ip)
{
  EnterCriticalSection(&csClient);
  
  ClientNode_t* newNode = ClientPool_Alloc();
  if (!newNode) { // 池满，踢掉最老的 
    newNode = ClientList_GetOldest();
    if (newNode) {
      printfSend(&newNode->socket, "You are kicked due to server full! Your index %d\n", newNode->index);
      CloseClient(newNode, "客户端数量已满");
    }
  }

  if ( newNode ) {
      newNode->socket = socket;
      newNode->connectTime = GetCurrentTimeMillis();
      strncpy(newNode->ipAddress, ip ? ip : "Unknown", sizeof(newNode->ipAddress)-1);
      ClientList_Add(newNode);
      
      // 创建线程
      newNode->hThread = CreateThread(NULL, 0, ClientRecvDataThread, newNode, 0, &newNode->threadId);
      if (newNode->hThread) {
          clientNumCount++;
          static uint64_t connectCount = 0;
          SafePrintf("Client connected IP:%s, index:%d, Count:%d/%d, Total request:%I64d\n",
              newNode->ipAddress, newNode->index, clientNumCount, getMaxClient(), ++connectCount);
      }
      else 
        ClientList_Remove(newNode);
  }
  
  LeaveCriticalSection(&csClient);
}



// 获取所有客户端IP和索引
void getAllclientIPandIndexInfo(char *retStr, uint16_t len) 
{
  EnterCriticalSection(&csClient);

  uint16_t strLen = 0;
  memset(retStr, 0, len);

  ClientNode_t* curr = clientList.head;
  while (curr && strLen < len) {
      char clientInfo[40];
      snprintf(clientInfo, sizeof(clientInfo), 
          "client index:%d, IP:%s\n", curr->index, curr->ipAddress);
      uint16_t infoLen = strlen(clientInfo);
      if (strLen + infoLen >= len) break;
      strcat(retStr, clientInfo);
      strLen += infoLen;
      curr = curr->next;
  }
  LeaveCriticalSection(&csClient);
}

static void CloseClient(ClientNode_t* node, const char *reason) 
{
    if (!node || node->socket == INVALID_SOCKET) return;

    EnterCriticalSection(&csClient);
    SafePrintf("Closed Client IP:%-16s index:%d, reason: %s\n", 
      node->ipAddress, node->index, reason? reason : "NULL");

    closesocket(node->socket);
    node->socket = INVALID_SOCKET;

    if (node->hThread) {
        WaitForSingleObject(node->hThread, 1000);
        CloseHandle(node->hThread);
        node->hThread = NULL;
    }

    ClientList_Remove(node);
    if (clientNumCount) 
      clientNumCount--;

    LeaveCriticalSection(&csClient);
}




// Socket 如果为就会发送给所有客户端，不为空且有效的话就会只发送给指定的客户端
int SendDataToClients(SOCKET *socket, const char* buff, int len) 
{
  EnterCriticalSection(&csClient);
  int sendRet = 0;
  
  if (socket && *socket != INVALID_SOCKET) {
      sendRet = send(*socket, buff, len, 0);
      // 检查发送是否失败，如果失败说明客户端可能已断开
      if (sendRet == SOCKET_ERROR) {
          int error = WSAGetLastError();
          if (error == WSAECONNRESET || error == WSAECONNABORTED) {
              // 客户端已断开，需要清理
              ClientNode_t* curr = clientList.head;
              while (curr) {
                  if (curr->socket == *socket) {
                      CloseClient(curr, "Send failed - client disconnected");
                      break;
                  }
                  curr = curr->next;
              }
          }
          sendRet = 0; // 重置为0表示发送失败
      }
  } else {
      // 处理发送给所有客户端的情况
      ClientNode_t* curr = clientList.head;
      ClientNode_t* next = NULL;
      
      while (curr) {
          next = curr->next; // 先保存下一个节点，因为curr可能在循环中被删除
          
          if (curr->socket != INVALID_SOCKET) {
              int ret = send(curr->socket, buff, len, 0);
              
              if (ret == SOCKET_ERROR) {
                  int error = WSAGetLastError();
                  if (error == WSAECONNRESET || error == WSAECONNABORTED) {
                      // 客户端已断开，需要清理
                      CloseClient(curr, "Send to all failed - client disconnected");
                  }
              } else if (ret > 0) {
                  sendRet += ret;
              }
          }
          curr = next;
      }
  }
  
  LeaveCriticalSection(&csClient);
  return sendRet;
}

/**
 * @brief  套接字发送字符串，使用类似于printf函数
 * @param 
 *		@arg Socket：指定发给客户端套接字指针，如果为孔就不指定客户端发送给所有客户端
 *		@arg fmt: printf 格式
 * @retval 
 */
int printfSend(SOCKET *Socket, const char *fmt, ...)
{
  //EnterCriticalSection(&csClient);

	static char char_buff[1024]; // 字符串缓冲区
	memset(char_buff, 0, sizeof char_buff);
  strcpy(char_buff, CTRL_HEADER);
  uint8_t ctrlHeaderLen = strlen( CTRL_HEADER);

  // args为定义的一个指向可变参数的变量，va_list以及下边要用到的
  // va_start,va_end都是是在定义可变参数函数中必须要用到宏，在stdarg.h头文件中定义
	va_list args; 
  va_start(args, fmt);
  int retLen = vsnprintf(char_buff + ctrlHeaderLen, 
    sizeof char_buff - ctrlHeaderLen, fmt, args);
  va_end(args); // 初始化args的函数，使其指向可变参数的第一个参数，fmt是可变参数的前一个参数

  //LeaveCriticalSection(&csClient);

  return SendDataToClients(Socket, char_buff, retLen + ctrlHeaderLen); 
}
