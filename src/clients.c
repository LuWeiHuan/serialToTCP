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
#include "TrafficStats.h"
#include "Command.h"
#include "uthash.h"

#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include <process.h>


/*================== 本地数据类型   =========================================*/
typedef struct ClientNode {
  SOCKET        socket;
  HANDLE        hThread;
  DWORD         threadId;
  uint16_t      index;          // 在客户端池里中的索引
  char          ip[50];
  uint64_t      connectTime;
  bool          sendTempUnav;
  uint64_t      tempUnavStart;
  volatile LONG isClosing;  // 添加关闭状态标志
  UT_hash_handle hh;        // 用于哈希表
  struct ClientNode* next;
} ClientNode_t;

typedef struct {
  ClientNode_t* head;
  ClientNode_t* tail;
  ClientsNum_t  num;  // 客户端数量
} ClientList_t;

typedef struct {
  ClientNode_t  node;
  ClientNode_t  *nodeAddr;
  BOOL          isSelfCall;
  int           closeSocketRet;
  char          reason[256];
} asyncRequest_t;

/*================== 本地宏定义     =========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static CRITICAL_SECTION csClient; 
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
static ClientNode_t* FindClientBySocket(const SOCKET *socket, bool isHASH);
static void CloseClient(ClientNode_t* node, const char *reason);

static bool sendMonopolizeExamine(ClientNode_t* clientInfo);


/*================== 外部函数和变量声明    ==================================*/

const char *getClientIP(uint16_t index)
{
  return index < clientList.num.max? clientPool[index].ip:"no index";
}

const SOCKET *getClientSocket(uint16_t index)
{
  if( index > clientList.num.max )
    return NULL;
  return clientPool[index].socket != INVALID_SOCKET? &clientPool[index].socket:NULL;
}

// 通过套接字获取客户端索引，返回真代表这个索引有效
bool getClientIndex(const SOCKET *Socket, uint16_t *retIndex)
{ 
  if( Socket == NULL )
    return false;
  
  EnterCriticalSection(&csClient); 
  ClientNode_t *targetClient = FindClientBySocket(Socket, true);
   if( targetClient && retIndex )
    *retIndex = targetClient->index; 
  LeaveCriticalSection(&csClient);
  return targetClient? true:false; 
}

 
// 使用套接字的方式关闭客户端，传统搜索过程效率低
void CloseClientSocket(const SOCKET *socket, const char *reason)
{ 
  EnterCriticalSection(&csClient); 
  ClientNode_t* nodeAddr = FindClientBySocket(socket, true); // 保存客户端节点副本 
  char *socketCloseInfo = getPrintf("关闭套接字，节点%s，%s",
      nodeAddr? "存在":"没有", reason? reason:"未知"); 
  LeaveCriticalSection(&csClient);
  CloseClient(nodeAddr, socketCloseInfo);
}

void ClientResourceInit(bool start) 
{
  static ClientNode_t LocalStaticClientPool[3] = {0};
  
  if (start) {
    clientPool = malloc(sizeof(ClientNode_t) * clientList.num.max);
    if (clientPool == NULL) {
      SafePrintf("malloc client Pool fail!\n");
      clientPool = LocalStaticClientPool;
      clientList.num.max = sizeof LocalStaticClientPool / sizeof LocalStaticClientPool[0];
    }

    ClientList_Init();
    InitializeCriticalSection(&csClient); 
  } 
  else {
    KickAllClients("资源释放");
    if (clientPool != LocalStaticClientPool && clientPool != NULL)
      free(clientPool);
    clientPool = NULL;
    
    DeleteCriticalSection(&csClient);
  }
}

// 获取最早的客户端
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
    if (clientPool[i].socket == INVALID_SOCKET) 
      return &clientPool[i];
  return NULL; // 池满
}

static void ClientPool_Free(ClientNode_t* node) 
{
  if (node && node->index < clientList.num.max) {
    node->socket = INVALID_SOCKET;
    node->hThread = NULL;
    node->sendTempUnav = 0;
    node->tempUnavStart = 0;  // 重置时间计数器
  //memset(node->ip, 0, sizeof node->ip);
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

// 通过Socket查找节点
// isHASH 传入真快速搜索，存在客户端列表的套接字，不一定能搜索到
// isHASH 传入假普通搜索，存在客户端列表的套接字，基本都能搜索到
static ClientNode_t* FindClientBySocket(const SOCKET *socket, bool isHASH)
{
  if( socket == NULL )
    return NULL;
  
  ClientNode_t *found = NULL;

  if( isHASH )
    HASH_FIND_INT(socketHashTable, socket, found);
  else for (ClientNode_t* curr = clientList.head; curr; curr = curr->next)  
    if( curr->socket == *socket)
      return curr;
  
  return found;
}

// 初始化空闲池
static void ClientList_Init(void) {
  clientList.head = NULL;
  clientList.tail = NULL;
  clientList.num.count = 0;
  
  for (uint16_t i = 0; i < clientList.num.max; i++) {
    clientPool[i].socket = INVALID_SOCKET;
    clientPool[i].index = i;
    clientPool[i].next = NULL;
    clientPool[i].sendTempUnav = 0;
  }
}


static DWORD WINAPI ClientRecvDataThread(void * param) 
//static unsigned __stdcall ClientRecvDataThread(void *param)
{
  if (param == NULL) { 
    SafePrintf("client recv data thread not Client info introduction\n");
    return -1;
  }

  ClientNode_t* clientInfo = (ClientNode_t*)param; 
  int bytesReceived = 0, bytesWritten, retSelect, WSAerror;
  fd_set readSet;
  struct timeval timeout;
  uint64_t sendCount = 0;
  const char *threadExitInfo = "NULL";
  static __thread char tcpRecvBuffer[RECV_BUFFER_SIZE];
  char titleString[20];
  memset(titleString, 0, sizeof titleString);
  snprintf(titleString, sizeof titleString, "client %d", clientInfo->index);

  // 设置socket为非阻塞模式
  u_long mode = 1;  // 1表示非阻塞，0表示阻塞
  int block = ioctlsocket(clientInfo->socket, FIONBIO, &mode);
  if (block != 0)
    threadExitInfo = getPrintf("线程退出，设置非阻塞失败，WAS代码：:%d ", WSAGetLastError()); 
  
  // 发送连接成功消息
  printfSend(&clientInfo->socket, "%s! your index %d\n", block==0?"OK":"Fail", clientInfo->index);
  sendComPortsListToClient( &clientInfo->socket, true );  // 向该客户端发送可用端口号
  
  while ( block == 0 ) {
    // 检查客户端socket是否仍然有效
    if (clientInfo->socket == INVALID_SOCKET){
      WSAerror = WSAGetLastError(); 
      threadExitInfo = getPrintf("线程退出，套接字无效，WSA代码：%d，接收：%d", 
          WSAerror, bytesReceived);
      break;
    }
    
    FD_ZERO(&readSet);
    FD_SET(clientInfo->socket, &readSet);

    // 设置超时时间为1秒
    timeout.tv_sec = 1;
    timeout.tv_usec = 0; // 100毫秒

    retSelect = select(0, &readSet, NULL, NULL, &timeout);
    if (retSelect == SOCKET_ERROR) { 
      WSAerror = WSAGetLastError(); 
      threadExitInfo = getPrintf("线程退出，选择错误，接收：%d，WAS代码：:%d ", 
          bytesReceived, WSAerror);
      break;
    }
    else if (retSelect == 0) {  // 超时，没有数据可读，继续循环 
      updataConsoleTitle(titleString);
      continue;
    }

    // 接收数据
    bytesReceived = recv(clientInfo->socket, tcpRecvBuffer, sizeof tcpRecvBuffer - 1, 0);
    if (bytesReceived == 0) { // 客户端正常关闭连接 
      WSAerror = WSAGetLastError();
      threadExitInfo = getPrintf("线程退出，优雅地断开连接，WSA代码：%d，接收：%d", 
          WSAerror, bytesReceived);
      break;
    }
    else if (bytesReceived <= SOCKET_ERROR) {
      WSAerror = WSAGetLastError();
      if (WSAerror == WSAEWOULDBLOCK) 
        continue;       // 非阻塞模式下没有数据是正常情况 
      else if (WSAerror == WSAECONNRESET || WSAerror == WSAECONNABORTED) { 
        threadExitInfo = getPrintf( "线程退出，连接被重置（拔网线），WSA代码：%d，接收：%d", 
            WSAerror, bytesReceived);
        break;
      }
      else { // 其他错误，断开连接
        threadExitInfo = getPrintf("线程退出，接收错误，WSA代码：%d，接收：%d", 
            WSAerror, bytesReceived);
        break;
      }
    }

    // 正常接收到数据
    tcpRecvBuffer[bytesReceived] = '\0';  // 防止命令解析异常

    // 检查是否是控制命令
    if (strnicmp(tcpRecvBuffer, CTRL_HEADER, strlen(CTRL_HEADER)) == 0) {
      if (runInfo.serverPrintData == 3) 
        SafePrintf("Client [%-2d]IP:%s len:%d cmd: %-60s\n", 
            clientInfo->index, clientInfo->ip, bytesReceived, tcpRecvBuffer);
      
      HandleClientCommand(&clientInfo->socket, tcpRecvBuffer + strlen(CTRL_HEADER));
      continue;
    }
    
    // 判断串口是否已经打开
    if (ComPort->isOpen == FALSE) {
      printfSend(&clientInfo->socket, "COM not open !\n");
      continue;
    }

    // 发送独占检查
    if( sendMonopolizeExamine(clientInfo) )
        continue;
        
    // 普通数据，发送到串口
    DWORD getError = 0;
    bytesWritten = ComPortSendData(tcpRecvBuffer, bytesReceived, &getError);
    if( bytesWritten != bytesReceived )
      printfSend(&clientInfo->socket, "COM write error: %ld\n", getError);

    char *Direct = getSendRecvDirectionStr("[TCP --> COM]", clientInfo->index);
    char *timeStr = getCurrentTimeStringSec(); 

    SafePrintf("%-21s%10I64d [%s]  %-6d/%-6d Byte (%s : %ld)%s%c", timeStr, ++sendCount, Direct,
            bytesWritten, bytesReceived, bytesWritten == bytesReceived? "OK" : "Fail", getError,
            runInfo.serverPrintData != 0 ? " data:" : " ", runInfo.COMsendPoll? '\n':'\r');
            
    if (runInfo.serverPrintData != 0) {
      if (runInfo.serverPrintData == 1)
        SafePrintf("%s", tcpRecvBuffer);
        
      if (runInfo.serverPrintData == 2) 
        printHex((uint8_t*)tcpRecvBuffer, bytesReceived, 40, 2);
    }
    
    #ifdef __TRAFFIC_STATS_H_ // 流量统计
    trafficStats.net.totalBytesReceived += bytesReceived;
    trafficStats.com.totalBytesSent += bytesWritten;
    #endif
  }

  CloseClient(clientInfo, threadExitInfo);
  return 0;
}

// 客户端数据给串口独占检查
// 返回：真 请结束循环不要发给串口，假 放行继续
static bool sendMonopolizeExamine(ClientNode_t* client)
{
  if( client == NULL )
    return false;
  
  // 串口发上来的数据是否被独占。
  if( runInfo.monopolizeComRecvIndex && *runInfo.monopolizeComRecvIndex != client->index){ 
    const char *ClientIP = getClientIP(*runInfo.monopolizeComRecvIndex);
    if( ClientIP != NULL && runInfo.monopolizeComSendIndex == NULL)
      printfSend(&client->socket, "Send data to COM, but [%-2d]IP:%s "
          "monopolize! You cannot receive COM data\n", 
          *runInfo.monopolizeComRecvIndex, ClientIP ); 
    if( ClientIP == NULL )
      runInfo.monopolizeComRecvIndex = NULL;
  }
  
  // 发送给串口的数据是否被指定客户端独占
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

bool addNewClient(SOCKET socket, const char *ip)
{
  if( clientPool == NULL || ip == NULL )
    return false;

  EnterCriticalSection(&csClient);
  
  ClientNode_t* newNode = ClientPool_Alloc();
  if (!newNode) { // 池满，踢掉最老的 
    newNode = ClientList_GetOldest();
    if (newNode) {
      printfSend(&newNode->socket, "You are kicked due to server full! "
        "Your index %d\n", newNode->index);
      CloseClient(newNode, "客户端数量已满");
    }
  }

  if ( newNode == NULL  ) {
    LeaveCriticalSection(&csClient);
    SafePrintf("No Client Node\n");
    return false;
  }

  newNode->socket = socket;
  newNode->connectTime = getRuningTimeMs();
  memset(newNode->ip, 0, sizeof newNode->ip);
  strcpy(newNode->ip, ip);
  ClientList_Add(newNode);
  InterlockedExchange(&newNode->isClosing, 0);

  // 创建线程
  newNode->hThread = CreateThread(NULL, 0, ClientRecvDataThread, newNode, 0, &newNode->threadId);
  //newNode->hThread = (HANDLE)_beginthreadex(NULL, 0, ClientRecvDataThread, newNode, 0, (unsigned*)&newNode->threadId);
  if (newNode->hThread) {
    static uint64_t connectCount = 0;
    SafePrintf("Client [%-2d]IP:%-16s Connected %d/%d Count:%I64d\n",
        newNode->index, newNode->ip, clientList.num.count, getMaxClient(), ++connectCount);
  }
  else 
    ClientList_Remove(newNode);
  
  LeaveCriticalSection(&csClient);
  return true;
}


// 获取所有客户端IP和索引
void getAllClientIPandIndexInfo(char *retStr, uint16_t len) 
{
  if(retStr == NULL || len == 0)
    return;
    
  EnterCriticalSection(&csClient);

  uint16_t strLen = 0;
  memset(retStr, 0, len);
  char clientInfo[100];
  
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
  LeaveCriticalSection(&csClient);
}



static void CloseClient(ClientNode_t* node, const char *reason)
{ 
  if (!node){
    SafePrintf("Client [-1]IP:0.0.0.0          Closed NO node,"
      " reason: %s\n", reason? reason:"未知");
    return;
  }

  EnterCriticalSection(&csClient); 
  BOOL isSelfCall = node->threadId == GetCurrentThreadId()? true:false;

  // 使用原子操作确保只有一个线程执行关闭
  if (InterlockedCompareExchange(&node->isClosing, 1, 0)) {
    SafePrintf("Client [%-2d]IP:%-16s Closed [SelfCall %s] is Already, reason: %s%s", 
        node->index, node->ip, isSelfCall? "YES":"NO ",
        reason? reason:"未知", g_clientsNum->count == 0 ? "\n\n":"\n");
    LeaveCriticalSection(&csClient);
    return;
  }

  HANDLE closeThread = node->hThread; // 暂存线程副本
  //先关闭套接字，促使客户端接收线程退出，异步关闭的话要尽快促使线程退出
  int closeSocketRet = -1;
  if (node->socket != INVALID_SOCKET)
    closeSocketRet = closesocket( node->socket ); 
  
  ClientList_Remove(node);
  LeaveCriticalSection(&csClient);
  
  const char *CloseInfo = " ";

  // 如果是线程自己调用的关闭，不等待也不立即关闭句柄
  if (closeThread && isSelfCall == false) {
    // 外部调用，等待线程退出
    DWORD waitResult = WaitForSingleObject(closeThread, 1000); 
    if (waitResult == WAIT_TIMEOUT) {
      DWORD exitCode;
      BOOL GetExitRet = GetExitCodeThread(closeThread, &exitCode);
      if ( GetExitRet && exitCode == STILL_ACTIVE) 
        SafePrintf("Client [%-2d]IP:%-16s recv thread wait timeout, exit Code:%ld\n", 
              node->index, node->ip, exitCode);
    }
    BOOL CloseRet = CloseHandle(closeThread);
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

    SafePrintf("%s播发送 错误:%6d，超时:%4I64d ms ==> %-2d %-16s]  %-6d/%-6d Byte (%s : %d)     \n",
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

// Socket 如果为空就会发送给所有客户端，不为空且有效的话就会只发送给指定的客户端
int sendDataToClients(const SOCKET *socket, const char* buff, int len) 
{
  EnterCriticalSection(&csClient);
  int sendRet = 0, error = 0; 
  uint64_t currentTime = 0; 
  
  // 收集需要关闭的客户端，在临界区外处理
  uint16_t closeCount = 0;
  static int errorList[ MAX_CLIENTS ] = {0};
  static ClientNode_t *clientsToClose[ MAX_CLIENTS ] = {0};

  if (socket && *socket != INVALID_SOCKET) {  // 单播 发送给指定客户端
    do {
      sendRet = send(*socket, buff, len, 0);
      if ( sendRet > 0 ) 
          break;
      
      error = WSAGetLastError();

      // 使用静态变量缓存上次找到的客户端节点
      static ClientNode_t *lastFoundClient = NULL;
      ClientNode_t *targetClient = NULL;
      // 首先检查是否是上次找到的客户端
      if (lastFoundClient && lastFoundClient->socket == *socket) 
        targetClient = lastFoundClient;
      else  // 如果不是上次的客户端，重新搜索
        targetClient = lastFoundClient = FindClientBySocket(socket, true);

      sendFailErrorHandle(false, targetClient, error, &closeCount, 
          errorList, clientsToClose, &currentTime, sendRet, len);
      if( closeCount && targetClient == lastFoundClient )
        lastFoundClient = NULL;
    } while (0);
  }     // 广播 发送给所有客户端
  else for (ClientNode_t *next, *curr = clientList.head; curr; curr = next ) {
    next = curr->next; // 先保存下一个节点，因为curr可能在循环中被删除 
    if( curr->socket == INVALID_SOCKET ) 
      continue;
    
    int ret = send(curr->socket, buff, len, 0);
    if (ret > 0) {  // 发送成功，重置计数器 
      curr->tempUnavStart = curr->sendTempUnav = false;
      sendRet += ret;
      continue;
    }

    error = WSAGetLastError(); 
    sendFailErrorHandle(true, curr, error, &closeCount, errorList, 
        clientsToClose, &currentTime, sendRet, len);
  }

  #ifdef __TRAFFIC_STATS_H_
  trafficStats.net.totalBytesSent += sendRet; 
  #endif

  LeaveCriticalSection(&csClient);
  
  // 在临界区外处理关闭客户端的请求
  for (uint16_t i = 0; i < closeCount; i++) {
    char *sendFailInfo = getPrintf("%s播发送失败，持续不可用时间：%I64d ms，WSA代码：%d",
        socket? "单" : "广", currentTime - clientsToClose[i]->tempUnavStart, errorList[i]);
    CloseClient(clientsToClose[i], sendFailInfo); 
  }

  return sendRet;
}

/**
 * @brief  套接字发送字符串，使用类似于printf函数
 * @param 
 *		@arg Socket：指定发给客户端套接字指针，如果为孔就不指定客户端发送给所有客户端
 *		@arg fmt: printf 格式
 * @retval 
 */
int printfSend(const SOCKET *Socket, const char *fmt, ...)
{
    static __thread char stringBuff[1024 * 4 + sizeof(uint32_t)]; // 字符串缓冲区
    strcpy(stringBuff, CTRL_HEADER);
    static uint8_t ctrlHeaderLen = strlen(CTRL_HEADER);

    va_list args; 
    va_start(args, fmt);
    int retLen = vsnprintf(stringBuff + ctrlHeaderLen, 
          sizeof stringBuff - ctrlHeaderLen - sizeof(uint32_t), fmt, args);
    va_end(args);

    // 在字符串后添加多个空字符作为终止符，帮助接收方识别消息边界
    uint32_t totalLength = ctrlHeaderLen + retLen;
    uint8_t paddingZeros = sizeof(uint64_t); // 增加空字符数量，例如使用8个空字符
    
    // 确保不超出缓冲区
    if (totalLength + paddingZeros > sizeof stringBuff)
        paddingZeros = sizeof stringBuff - totalLength;
    
    memset(stringBuff + totalLength, 0, sizeof(uint16_t));
    
    return sendDataToClients(Socket, stringBuff, totalLength + sizeof(uint16_t)); 
}

/**
 * @brief 踢掉所有已连接的客户端。
 * @param reason 踢掉客户端的原因（可选，可为NULL）
 * @attention 不能同步调用，也就是不能由任何客户端发起，
 *  如果要用。必须异步调用或者由不在客户端列表里的成员发起，比如UDP搜索服务。
 */
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
  
  // 重置计数和状态
  clientList.num.count = 0;
  runInfo.monopolizeComSendIndex = NULL;
  runInfo.monopolizeComRecvIndex = NULL;
}
