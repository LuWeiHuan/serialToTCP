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
#include "clients.h"
#include "main.h"
#include "public.h"
#include "logPrint.h"
#include "COM.h"
#include "COMinfo.h"
#include "TrafficStats.h"
#include "Command.h"
#include "configSave.h"

#include <stdio.h>
#include <stdarg.h>
#include "uthash.h"

#ifdef _WIN32
#include <process.h>
#else
#include <errno.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#endif

/*================== 本地数据类型   =========================================*/
typedef struct ClientNode {
  socket_t           socket;
  thread_t           hThread;
  threadID_t         threadId;
  uint16_t           index;
  char               ip[50];
  uint64_t           connectTime;
  bool               sendTempUnav;
  uint64_t           tempUnavStart;
  volatile long      isClosing;
  UT_hash_handle     hh;
  struct ClientNode* next;
} ClientNode_t;

typedef struct {
  ClientNode_t* head;
  ClientNode_t* tail;
  ClientsNum_t  num;
} ClientList_t;

/*================== 本地变量声明    ========================================*/
static mutex_type csClient; 
static ClientNode_t *clientPool = NULL, *socketHashTable = NULL;
static ClientList_t clientList = { .num.max = MAX_CLIENTS };

/*================== 全局共享变量    ========================================*/
ClientsNum_t const * const g_clientsNum = &clientList.num;

/*================== 本地函数声明    ========================================*/
static void ClientList_Init(void);
static void ClientList_Add(ClientNode_t* node);
static void ClientList_Remove(ClientNode_t* node);
static ClientNode_t* ClientList_GetOldest(void);
static ClientNode_t* ClientPool_Alloc(void);
static void ClientPool_Free(ClientNode_t* node);
static ClientNode_t* FindClientBySocket(const socket_t *socket, bool isHASH);
static void CloseClient(ClientNode_t* node, const char *reason);
static bool sendMonopolizeExamine(ClientNode_t* clientInfo);

static threadRet WINAPI ClientRecvDataThread(void *param);

const char *getClientIP(uint16_t index)
{
  return index < clientList.num.max ? clientPool[index].ip : "no index";
}

const socket_t *getClientSocket(uint16_t index)
{
  if( index >= clientList.num.max )
    return NULL;
  return clientPool[index].socket != INVALID_SOCKET_VALUE ? &clientPool[index].socket : NULL;
}

bool getClientIndex(const socket_t *Socket, uint16_t *retIndex)
{ 
  if( Socket == NULL )
    return false;
  
  EnterCriticalSection_Wrapper(&csClient); 
  ClientNode_t *targetClient = FindClientBySocket(Socket, true);
   if( targetClient && retIndex )
    *retIndex = targetClient->index; 
  LeaveCriticalSection_Wrapper(&csClient);
  return targetClient ? true : false; 
}

void CloseClientSocket(const socket_t *socket, const char *reason)
{ 
  EnterCriticalSection_Wrapper(&csClient); 
  ClientNode_t* nodeAddr = FindClientBySocket(socket, true);
  char *socketCloseInfo = getPrintf("关闭套接字，节点%s，%s",
      nodeAddr ? "存在":"没有", reason ? reason:"未知"); 
  LeaveCriticalSection_Wrapper(&csClient);
  CloseClient(nodeAddr, socketCloseInfo);
}

void ClientResourceInit(bool start) 
{
  static ClientNode_t LocalStaticClientPool[3];
  memset(LocalStaticClientPool, 0, sizeof LocalStaticClientPool);
  
  if (start) {
    clientPool = malloc(sizeof(ClientNode_t) * clientList.num.max);
    if (clientPool == NULL) {
      SafePrintf("malloc client Pool fail!\n");
      clientPool = LocalStaticClientPool;
      clientList.num.max = sizeof LocalStaticClientPool / sizeof LocalStaticClientPool[0];
    }

    ClientList_Init();
    InitializeCriticalSection_Wrapper(&csClient); 
  } 
  else {
    KickAllClients("资源释放");
    if (clientPool != LocalStaticClientPool && clientPool != NULL)
      free(clientPool);
    clientPool = NULL;
    
    DeleteCriticalSection_Wrapper(&csClient);
  }
}

static ClientNode_t* ClientList_GetOldest(void) 
{
  ClientNode_t* oldest = NULL; 
  for (ClientNode_t* curr = clientList.head; curr; curr = curr->next)
    if (!oldest || curr->connectTime < oldest->connectTime)
      oldest = curr; 
  return oldest;
}

static ClientNode_t* ClientPool_Alloc(void) 
{
  for (uint16_t i = 0; i < clientList.num.max; i++) 
    if (clientPool[i].socket == INVALID_SOCKET_VALUE) 
      return &clientPool[i];
  return NULL;
}

static void ClientPool_Free(ClientNode_t* node) 
{
  if (node && node->index < clientList.num.max) {
    node->socket = INVALID_SOCKET_VALUE;
    node->hThread = (thread_t)0;
    node->sendTempUnav = 0;
    node->tempUnavStart = 0;
  }
}

static void ClientList_Add(ClientNode_t* node) 
{
  if (!node) return;
  node->next = NULL;
  if (!clientList.head) {
    clientList.head = node;
    clientList.tail = node;
  }
  else {
    clientList.tail->next = node;
    clientList.tail = node;
  }
  clientList.num.count++;
  HASH_ADD_INT(socketHashTable, socket, node);
}

static void ClientList_Remove(ClientNode_t* node) 
{
  if (!node || !clientList.head) return;
  HASH_DEL(socketHashTable, node);

  if (clientList.head == node) {
    clientList.head = node->next;
    if (clientList.tail == node)
        clientList.tail = NULL;
  }
  else {
    ClientNode_t* prev = clientList.head;
    while (prev->next != node) 
      prev = prev->next;
    prev->next = node->next;
    if (clientList.tail == node)
        clientList.tail = prev;
  }
  clientList.num.count--;
  ClientPool_Free(node);
}

static ClientNode_t* FindClientBySocket(const socket_t *socket, bool isHASH)
{
  if( socket == NULL )
    return NULL;
  
  ClientNode_t *found = NULL;

  if( isHASH )
    HASH_FIND_INT(socketHashTable, socket, found);
  else 
    for (ClientNode_t* curr = clientList.head; curr; curr = curr->next)  
      if( curr->socket == *socket)
        return curr;
  
  return found;
}

static void ClientList_Init(void) {
  clientList.head = NULL;
  clientList.tail = NULL;
  clientList.num.count = 0;
  
  for (uint16_t i = 0; i < clientList.num.max; i++) {
    clientPool[i].socket = INVALID_SOCKET_VALUE;
    clientPool[i].index = i;
    clientPool[i].next = NULL;
    clientPool[i].sendTempUnav = 0;
  }
}

static threadRet WINAPI ClientRecvDataThread(void *param)
{
  if (param == NULL) { 
    SafePrintf("client recv data thread not Client info introduction\n");
    return (threadRet)-1;
  }

  ClientNode_t* clientInfo = (ClientNode_t*)param;
  int bytesReceived = 0, bytesWritten, retSelect, error;
  fd_set readSet;
  struct timeval timeout;
  uint64_t sendCount = 0;
  const char *threadExitInfo = "NULL";
  static __thread char tcpRecvBuffer[RECV_BUFFER_SIZE];
  char titleString[20];
  memset(titleString, 0, sizeof titleString);
  snprintf(titleString, sizeof titleString, "client %d", clientInfo->index);

  // 设置socket为非阻塞模式
#ifdef _WIN32
  u_long mode = 1;
  int block = ioctlsocket(clientInfo->socket, FIONBIO, &mode);
#else
  int flags = fcntl(clientInfo->socket, F_GETFL, 0);
  int block = fcntl(clientInfo->socket, F_SETFL, flags | O_NONBLOCK);
#endif
  
  if (block != 0) {
    error = GetLastError();
    threadExitInfo = getPrintf("线程退出，设置非阻塞失败。错误代码：%d ", error); 
  }
  
  printfSend(&clientInfo->socket, "%s! your index %d\n", block==0?"OK":"Fail", clientInfo->index);
  sendComPortsListToClient( &clientInfo->socket, true );

  while ( block == 0 ) {
    if (clientInfo->socket == INVALID_SOCKET_VALUE){
      error = GetLastError(); 
      threadExitInfo = getPrintf("线程退出，套接字无效。错误代码：%d，接收：%d", 
          error, bytesReceived);
      break;
    }
    
    FD_ZERO(&readSet);
    FD_SET(clientInfo->socket, &readSet);
    timeout.tv_sec = 0;
    timeout.tv_usec = 200000;

    retSelect = select(clientInfo->socket + 1, &readSet, NULL, NULL, &timeout);
    if (retSelect == SOCKET_ERROR) { 
      error = GetLastError(); 
      threadExitInfo = getPrintf("线程退出，选择错误。接收：%d，错误代码：%d ", 
          bytesReceived, error);
      break;
    }
    else if (retSelect == 0) { 
      updataConsoleTitle(titleString);
      continue;
    }

    bytesReceived = recv(clientInfo->socket, tcpRecvBuffer, sizeof tcpRecvBuffer - 1, 0);
    if (bytesReceived == 0) {
      error = GetLastError();
      threadExitInfo = getPrintf("线程退出，优雅地断开连接。错误代码：%d，接收：%d", 
          error, bytesReceived); 
      break;
    }
    else if (bytesReceived < 0) {
      error = GetLastError();
#ifdef _WIN32
      if (error == WSAEWOULDBLOCK) 
#else
      if (error == EWOULDBLOCK || error == EAGAIN)
#endif
        continue;
#ifdef _WIN32
      else if (error == WSAECONNRESET || error == WSAECONNABORTED) {
#else
      else if (error == ECONNRESET || error == ECONNABORTED) {
#endif
        threadExitInfo = getPrintf( "线程退出，连接被重置。错误代码：%d，接收：%d", 
            error, bytesReceived);
        break;
      }
      else {
        threadExitInfo = getPrintf("线程退出，接收错误。错误代码：%d，接收：%d", 
            error, bytesReceived);
        break;
      }
    }

    tcpRecvBuffer[bytesReceived] = '\0';

    if (strnicmp(tcpRecvBuffer, CTRL_HEADER, strlen(CTRL_HEADER)) == 0) {
      if (saveInfo.serverPrintData == 3) 
        SafePrintf("Client [%-2d]IP:%s len:%d cmd: %-60s\n", 
            clientInfo->index, clientInfo->ip, bytesReceived, tcpRecvBuffer);
      
      HandleClientCommand(&clientInfo->socket, tcpRecvBuffer + strlen(CTRL_HEADER));
      continue;
    }
    
    if (getComIsOpen() == false) {
      printfSend(&clientInfo->socket, "COM not open !\n");
      continue;
    }

    if( sendMonopolizeExamine(clientInfo) )
        continue;
        
    uint32_t getError = 0;
    bytesWritten = ComPortSendData((uint8_t*)tcpRecvBuffer, bytesReceived, &getError);
    if( bytesWritten != bytesReceived )
      printfSend(&clientInfo->socket, "COM write error: %ld\n", getError);

    char *Direct = getSendRecvDirectionStr("[TCP --> COM]", clientInfo->index);
    char *timeStr = getCurrentTimeStringSec(); 

    SafePrintf("%-21s%10" PRIu64 " [%s]  %-6d/%-6d Byte (%s : %d)%s%c", timeStr, ++sendCount, Direct,
            bytesWritten, bytesReceived, bytesWritten == bytesReceived? "OK" : "Fail", getError,
            saveInfo.serverPrintData != 0 ? " data:" : " ", saveInfo.COMsendPoll? '\n':'\r');
    
    if (saveInfo.serverPrintData != 0) {
      if (saveInfo.serverPrintData == 1)
        SafePrintf("%s", tcpRecvBuffer);
        
      if (saveInfo.serverPrintData == 2) 
        printHex((uint8_t*)tcpRecvBuffer, bytesReceived, 40, 2);
    }
    
    #ifdef __TRAFFIC_STATS_H_
    trafficStats.net.totalBytesReceived += bytesReceived;
    trafficStats.com.totalBytesSent += bytesWritten;
    #endif
  } 

  CloseClient(clientInfo, threadExitInfo);
  return (threadRet)0;
}

static bool sendMonopolizeExamine(ClientNode_t* client)
{
  if( client == NULL )
    return false;
  
  if( runInfo.monopolizeComRecvIndex && *runInfo.monopolizeComRecvIndex != client->index){ 
    const char *ClientIP = getClientIP(*runInfo.monopolizeComRecvIndex);
    if( ClientIP != NULL && runInfo.monopolizeComSendIndex == NULL)
      printfSend(&client->socket, "Send data to COM, but [%-2d]IP:%s "
          "monopolize! You cannot receive COM data\n", 
          *runInfo.monopolizeComRecvIndex, ClientIP ); 
    if( ClientIP == NULL )
      runInfo.monopolizeComRecvIndex = NULL;
  }
  
  if( runInfo.monopolizeComSendIndex && *runInfo.monopolizeComSendIndex != client->index){ 
    const char *ClientIP = getClientIP(*runInfo.monopolizeComSendIndex);
    if( ClientIP != NULL ) {
      printfSend(&client->socket, "Send data to COM, but [%-2d]IP:%s monopolize!\n",
        *runInfo.monopolizeComSendIndex, ClientIP ); 
      return true; 
    }
    else
      runInfo.monopolizeComSendIndex = NULL;
  }
  return false; 
}

bool addNewClient(socket_t socket, const char *ip)
{
  if( clientPool == NULL || ip == NULL )
    return false;

  EnterCriticalSection_Wrapper(&csClient);
  
  ClientNode_t* newNode = ClientPool_Alloc();
  if (!newNode) {
    newNode = ClientList_GetOldest();
    if (newNode) {
      printfSend(&newNode->socket, "You are kicked due to server full! "
        "Your index %d\n", newNode->index);
      CloseClient(newNode, "客户端数量已满");
    }
  }

  if ( newNode == NULL  ) {
    LeaveCriticalSection_Wrapper(&csClient);
    SafePrintf("No Client Node\n");
    return false;
  }

  newNode->socket = socket;
  newNode->connectTime = getRuningTimeMs();
  memset(newNode->ip, 0, sizeof newNode->ip);
  strcpy(newNode->ip, ip);
  ClientList_Add(newNode);

  #ifdef _WIN32
  InterlockedExchange(&newNode->isClosing, 0);
  #else
  __sync_lock_test_and_set(&newNode->isClosing, 0);
  #endif

  newNode->hThread = threadCreate(&newNode->threadId, ClientRecvDataThread, newNode);
  
  if (newNode->hThread) {
    static uint64_t connectCount = 0;
    SafePrintf("Client [%-2d]IP:%-16s Connected %d/%d Count:%" PRIu64 "\n",
        newNode->index, newNode->ip, clientList.num.count, getMaxClient(), ++connectCount);
  }
  else 
    ClientList_Remove(newNode);
  
  LeaveCriticalSection_Wrapper(&csClient);
  return true;
}

void getAllClientIPandIndexInfo(char *retStr, uint16_t len) 
{
  if(retStr == NULL || len == 0)
    return;
    
  EnterCriticalSection_Wrapper(&csClient);

  uint16_t strLen = 0;
  memset(retStr, 0, len);
  char clientInfo[128];
  
  for (ClientNode_t* curr = clientList.head; curr && strLen < len; curr = curr->next) {
    memset(clientInfo, 0, sizeof clientInfo);
    snprintf(clientInfo, sizeof clientInfo, 
        "client [%-2d]IP:%-16s\n", curr->index, curr->ip);
    uint16_t infoLen = strlen(clientInfo);
    if (strLen + infoLen >= len) 
      break;
    strcat(retStr, clientInfo);
    strLen += infoLen; 
  }
  LeaveCriticalSection_Wrapper(&csClient);
}

static void CloseClient(ClientNode_t* node, const char *reason)
{ 
  if (!node){
    SafePrintf("Client [-1]IP:0.0.0.0          Closed NO node,"
      " reason: %s\n", reason? reason:"未知");
    return;
  }
  
  EnterCriticalSection_Wrapper(&csClient); 
  bool isSelfCall = (node->threadId == GetCurrentThreadId_Wrapper());

  #ifdef _WIN32
  if (InterlockedCompareExchange(&node->isClosing, 1, 0)) {
  #else
  if (__sync_val_compare_and_swap(&node->isClosing, 0, 1)) {
  #endif
    SafePrintf("Client [%-2d]IP:%-16s Closed [SelfCall %s] is Already, reason: %s%s", 
        node->index, node->ip, isSelfCall? "YES":"NO ",
        reason? reason:"未知", g_clientsNum->count == 0 ? "\n\n":"\n");
    LeaveCriticalSection_Wrapper(&csClient);
    return;
  }

  thread_t closeThread = node->hThread;
  int closeSocketRet = -1;
  if (node->socket != INVALID_SOCKET_VALUE)  
    closeSocketRet = closeSocket(node->socket); 
  
  ClientList_Remove(node);
  LeaveCriticalSection_Wrapper(&csClient);
  
  const char *CloseInfo = " ";

  if (closeThread && isSelfCall == false) {
    DWORD waitResult = WaitForSingleObject_Wrapper(closeThread, 1000); 
    if (waitResult == WAIT_TIMEOUT) {
      SafePrintf("Client [%-2d]IP:%-16s recv thread wait timeout\n", 
              node->index, node->ip);
    }
    bool CloseRet = CloseHandle(closeThread);
    CloseInfo = getPrintf("Handle:%s wait:%ld ", CloseRet? "OK":"Fail", waitResult);
  }

  SafePrintf("Client [%-2d]IP:%-16s Closed [SelfCall %s] sok:%s %sreason: %s%s", 
      node->index, node->ip, isSelfCall? "YES":"NO ",
      closeSocketRet==0? "OK":"Fail", CloseInfo, reason, 
      g_clientsNum->count == 0 ? "\n\n":"\n");
}


// sendDataToClients 专用错误处理函数不可外用
static void sendFailErrorHandle(bool wide, ClientNode_t *ClientInfo, int error, 
  uint16_t *closeCount, int *errorList, ClientNode_t **clientsToClose, 
  uint64_t *retCurrentTime, int sendRet, int sendLen)
{
  *retCurrentTime = getRuningTimeMs(); 
  uint64_t timeDiff = ClientInfo? *retCurrentTime - ClientInfo->tempUnavStart: 0;
  if( timeDiff > 2000 )
    timeDiff = 1;

  static uint8_t timeDiffIsChange = 0;
  if( timeDiffIsChange != timeDiff/200){
    timeDiffIsChange = timeDiff/200;

    SafePrintf("%s播发送 错误:%6d，超时:%4" PRIu64 " ms ==> %-2d %-16s]  %-6d/%-6d Byte (%s : %d)     \n",
        wide? "广":"单", error, timeDiff, ClientInfo? ClientInfo->index:-1, 
        ClientInfo? ClientInfo->ip:"Unknown IP", 
        sendRet, sendLen, sendRet - sendLen == 0? "OK":"Fail", sendRet - sendLen );
  }

  // 没有对应节点就不处理
  if (ClientInfo == NULL) 
    return;
  
  // 资源暂时不可用，断开WIFI的时候会出现或网络环境不好的情况下会出现
  if (error == WSAEWOULDBLOCK ) {
      // 第一次遇到资源暂时不可用，记录开始时间
      if( ClientInfo->sendTempUnav == false ){
        ClientInfo->tempUnavStart = *retCurrentTime;
        ClientInfo->sendTempUnav = true; 
      }
  }
  else
    ClientInfo->tempUnavStart = ClientInfo->sendTempUnav = false; 
  
  if( error == WSAECONNRESET || error == WSAECONNABORTED || 
      (ClientInfo->sendTempUnav && (*retCurrentTime - ClientInfo->tempUnavStart) >= 1000) ) { 
      // 连接重置或中止，立即关闭
      errorList[*closeCount] = error;
      clientsToClose[(*closeCount)++] = ClientInfo;
      ClientInfo->sendTempUnav = false; 
  }
}

int sendDataToClients(const socket_t *socket, const char* buff, int len) 
{
  EnterCriticalSection_Wrapper(&csClient);
  int sendRet = 0, error = 0; 
  uint64_t currentTime = 0; 
  
  uint16_t closeCount = 0;
  static int errorList[ MAX_CLIENTS ] = {0};
  static ClientNode_t *clientsToClose[ MAX_CLIENTS ] = {0};

  if (socket && *socket != INVALID_SOCKET_VALUE) {
    do {
      sendRet = send(*socket, buff, len, 0);
      if ( sendRet > 0 ) 
          break;
      
      error = GetLastError();

      static ClientNode_t *lastFoundClient = NULL;
      ClientNode_t *targetClient = NULL;
      if (lastFoundClient && lastFoundClient->socket == *socket) 
        targetClient = lastFoundClient;
      else
        targetClient = lastFoundClient = FindClientBySocket(socket, true);

      sendFailErrorHandle(false, targetClient, error, &closeCount, 
          errorList, clientsToClose, &currentTime, sendRet, len);
      if( closeCount && targetClient == lastFoundClient )
        lastFoundClient = NULL;
    } while (0);
  }
  else for (ClientNode_t *next, *curr = clientList.head; curr; curr = next ) {
    next = curr->next;
    if( curr->socket == INVALID_SOCKET_VALUE ) 
      continue;
    
    int ret = send(curr->socket, buff, len, 0);
    if (ret > 0) {
      curr->tempUnavStart = curr->sendTempUnav = false;
      sendRet += ret;
      continue;
    }

    error = GetLastError(); 
    sendFailErrorHandle(true, curr, error, &closeCount, errorList, 
        clientsToClose, &currentTime, sendRet, len);
  }

  #ifdef __TRAFFIC_STATS_H_
  trafficStats.net.totalBytesSent += sendRet; 
  #endif

  LeaveCriticalSection_Wrapper(&csClient);
  
  for (uint16_t i = 0; i < closeCount; i++) {
    char *sendFailInfo = getPrintf("%s播发送失败，持续不可用时间：%" PRIu64 " ms，错误代码：%d",
        socket? "单" : "广", currentTime - clientsToClose[i]->tempUnavStart, errorList[i]);
    CloseClient(clientsToClose[i], sendFailInfo); 
  }

  return sendRet;
}

int printfSend(const socket_t *Socket, const char *fmt, ...)
{
    static __thread char stringBuff[1024 * 4 + sizeof(uint32_t)];
    strcpy(stringBuff, CTRL_HEADER);
    static uint8_t ctrlHeaderLen = sizeof CTRL_HEADER - 1; // 减去字符串结尾的 '\0'

    va_list args; 
    va_start(args, fmt);
    int retLen = vsnprintf(stringBuff + ctrlHeaderLen, 
          sizeof stringBuff - ctrlHeaderLen - sizeof(uint32_t), fmt, args);
    va_end(args);

    uint32_t totalLength = ctrlHeaderLen + retLen;
    uint8_t paddingZeros = sizeof(uint64_t);
    
    if (totalLength + paddingZeros > sizeof stringBuff)
        paddingZeros = sizeof stringBuff - totalLength;
    
    memset(stringBuff + totalLength, 0, sizeof(uint16_t));
    
    return sendDataToClients(Socket, stringBuff, totalLength + sizeof(uint16_t)); 
}

void KickAllClients(const char* reason)
{
  const char* kickReason = reason? reason : "NULL";
  char allExitInfoChs[100];
  snprintf(allExitInfoChs, sizeof allExitInfoChs, "所有客户端下线 %s", kickReason);
  printfSend(NULL, "Kicking all clients, reason:%s\n", kickReason);
  for (ClientNode_t* next, *curr = clientList.head; curr; curr = next) {
    next = curr->next;
    CloseClient(curr, allExitInfoChs);
  }
  
  clientList.num.count = 0;
  runInfo.monopolizeComSendIndex = NULL;
  runInfo.monopolizeComRecvIndex = NULL;
}

void sendComPortsListToClient(socket_t *socket, bool VPID)
{
  const char *comList = getComPortList(VPID);
  printfSend(socket, "%s\n", comList? comList: "Failed to get COM port list");
  
  if( getComIsOpen() && strstr(comList, getComName() ) == NULL )
    CloseComPort("disconnection inexistence!");
}