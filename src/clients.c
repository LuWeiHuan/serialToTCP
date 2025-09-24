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

#include "Queue.h"
#include "uthash.h"  // 使用uthash库，单文件头文件库

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
    bool      sendTempUnav;
    uint64_t  tempUnavStart;
    char ipAddress[16];
    volatile LONG isClosing;  // 添加关闭状态标志
    UT_hash_handle hh;        // 用于哈希表
    struct ClientNode* next;
} ClientNode_t;

typedef struct {
    ClientNode_t* head;
    ClientNode_t* tail;
    uint16_t      count;
} ClientList_t;

typedef struct {
    ClientNode_t  node;
    ClientNode_t  *nodeAddr;
    BOOL          isSelfCall;
    int           closeSocketRet;
    char          reason[512];
} asyncRequest_t;

/*================== 本地宏定义     =========================================*/
/*================== 本地常量声明    ========================================*/


/*================== 本地变量声明    ========================================*/
static AsyncQueue_t asyncCloseQueue = {0};
static CRITICAL_SECTION csClient;
static ClientList_t clientList = {0};
static ClientNode_t *clientPool = NULL;
static ClientNode_t *socketHashTable = NULL;

static ClientsNum_t ClientsNum = {
  .max = MAX_CLIENTS,
  .count = &clientList.count,
};

/*================== 全局共享变量    ========================================*/
ClientsNum_t const * const g_clientsNum = &ClientsNum;

/*================== 本地函数声明    ========================================*/
static void ClientList_Init(void);
static void ClientList_Add(ClientNode_t* node);
static void ClientList_Remove(ClientNode_t* node);
static ClientNode_t* ClientList_GetOldest(void);
static ClientNode_t* ClientPool_Alloc(void);
static void ClientPool_Free(ClientNode_t* node);
static ClientNode_t* FindClientBySocket(const SOCKET *socket);

static void AsyncCloseCallback(queueData_t *);
static void CloseClient(ClientNode_t* node, BOOL isSelfCall, const char *reason);

static bool sendMonopolizeExamine(ClientNode_t* clientInfo);

/*================== 外部函数和变量声明    ==================================*/

const char *getClientIP(uint16_t index)
{
  return index < ClientsNum.max? clientPool[index].ipAddress:"no index";
}

const SOCKET *getClientSocket(uint16_t index)
{
  if( index > ClientsNum.max )
    return NULL;

  return clientPool[index].socket != INVALID_SOCKET? &clientPool[index].socket:NULL;
}

// 通过套接字获取客户端索引，返回真代表这个索引有效
bool getClientIndex(const SOCKET *Socket, uint16_t *retIndex)
{ 
  if( Socket == NULL )
    return false;
  
  EnterCriticalSection(&csClient); 
  ClientNode_t *targetClient = FindClientBySocket( Socket );
   if( targetClient && retIndex )
    *retIndex = targetClient->index; 
  LeaveCriticalSection(&csClient);

  return targetClient ? true:false; 
}


void CloseClientSocket(SOCKET socket, const char *reason)
{ 
  EnterCriticalSection(&csClient); 
  ClientNode_t *targetClient = NULL;
  for (ClientNode_t* curr = clientList.head; curr; curr = curr->next)  
    if( curr->socket == socket){
      targetClient = curr;
      break;
    } 
  LeaveCriticalSection(&csClient);

  char *reasonInfo = getPrintf("关闭套接字，搜索节点%s，%s", 
      targetClient? "存在":"没有", reason? reason:"未知");
  
  CloseClient(targetClient, false, reasonInfo);
}

void ClientResourceInit(bool start) 
{
  static ClientNode_t LocalStaticClientPool[3] = {0};
  
  if (start) {
    clientPool = malloc(sizeof(ClientNode_t) * ClientsNum.max);
    if (clientPool == NULL) {
      SafePrintf("malloc client Pool fail!\n");
      clientPool = LocalStaticClientPool;
      ClientsNum.max = sizeof LocalStaticClientPool / sizeof LocalStaticClientPool[0];
    }

    ClientList_Init();
    InitializeCriticalSection(&csClient);
    
    // 初始化异步关闭客户端队列，优化后同步关闭也蛮快
    startAsyncDataHandleThread(&asyncCloseQueue,
      AsyncCloseCallback, 5, sizeof(asyncRequest_t) );
  } 
  else {
    // 首先停止接受新的异步关闭请求
    asyncCloseQueue.running = FALSE;
    
    // 先同步清理所有客户端（不使用异步队列）
    EnterCriticalSection(&csClient); 
    for(ClientNode_t *next, * curr = clientList.head; curr; curr = next) {
        next = curr->next;
        #if 1
        CloseClient(curr, false, "资源释放");
        #else
        if (curr->socket != INVALID_SOCKET) {
            closesocket(curr->socket);
            curr->socket = INVALID_SOCKET;
        }
        // 同步关闭客户端，避免使用异步队列
        if (curr->hThread) {
            WaitForSingleObject(curr->hThread, 1000);
            CloseHandle(curr->hThread);
            curr->hThread = NULL;
        }
        // 从列表中移除但不调用完整的 客户端关闭
        ClientList_Remove(curr);
        #endif 
        HASH_DEL(socketHashTable, curr);// 从哈希表中移除
    }
    clientList.count = 0;
    LeaveCriticalSection(&csClient);
    
    // 现在安全关闭异步队列
    FreeAsyncSendQueue(&asyncCloseQueue);
    
    // 释放资源
    if (clientPool != LocalStaticClientPool && clientPool != NULL)
      free(clientPool);
    
    clientPool = NULL;
    
    DeleteCriticalSection(&csClient);
  }
}

static ClientNode_t* ClientPool_Alloc(void) 
{
  for (uint16_t i = 0; i < ClientsNum.max; i++) 
    if (clientPool[i].socket == INVALID_SOCKET) 
        return &clientPool[i];
  return NULL; // 池满
}

static void ClientPool_Free(ClientNode_t* node) 
{
  if (node && node->index < ClientsNum.max) {
    node->socket = INVALID_SOCKET;
    node->hThread = NULL;
    node->sendTempUnav = 0;
    node->tempUnavStart = 0;  // 重置时间计数器
  //memset(node->ipAddress, 0, sizeof node->ipAddress);
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
    clientList.count++;
    // 添加到哈希表
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
  clientList.count--;
  ClientPool_Free(node);
}

// 快速通过Socket查找节点
static ClientNode_t* FindClientBySocket(const SOCKET *socket)
{
  if( socket == NULL )
    return NULL;
    
  ClientNode_t *found = NULL;
  HASH_FIND_INT(socketHashTable, socket, found);
  return found;
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


static void ClientList_Init(void) {
    clientList.head = NULL;
    clientList.tail = NULL;
    clientList.count = 0;
    // 初始化空闲池
    for (uint16_t i = 0; i < ClientsNum.max; i++) {
        clientPool[i].socket = INVALID_SOCKET;
        clientPool[i].index = i;
        clientPool[i].next = NULL;
        clientPool[i].sendTempUnav = 0;
    }
}

static DWORD WINAPI ClientRecvDataThread(LPVOID lpParam) 
{
  if (lpParam == NULL) { 
      SafePrintf("client data recv thread not Client info introduction\n");
      return -1;
  }
  
  ClientNode_t* clientInfo = (ClientNode_t*)lpParam; 
  int bytesReceived = 0, retSelect, WSAerror;
  fd_set readSet;
  struct timeval timeout;
  uint64_t sendCount = 0;
  const char *exitInfo = "NULL";
  static __thread char tcpRecvBuffer[RECV_BUFFER_SIZE];
  char titleString[20];
  memset(titleString, 0, sizeof titleString);
  snprintf(titleString, sizeof titleString, "client %d", clientInfo->index);

  // 设置socket为非阻塞模式
  u_long mode = 1;  // 1表示非阻塞，0表示阻塞
  int block = ioctlsocket(clientInfo->socket, FIONBIO, &mode);
  if (block != 0) { 
    exitInfo = getPrintf("设置非阻塞失败，接收：%d，WAS代码：:%d ", 
        bytesReceived, WSAGetLastError()); 
  }
  // 发送连接成功消息
  printfSend(&clientInfo->socket, "%s! your index %d\n", block==0?"OK":"Fail", clientInfo->index);
  sendComPortsListToClient( &clientInfo->socket, true );  // 向该客户端发送可用端口号

  while ( block == 0 ) {
    // 检查客户端socket是否仍然有效
    if (clientInfo->socket == INVALID_SOCKET){
      WSAerror = WSAGetLastError(); 
      exitInfo = getPrintf("线程退出，套接字无效，WSA代码：%d，接收：%d", 
            WSAerror, bytesReceived);
      break;
    }
    
    FD_ZERO(&readSet);
    FD_SET(clientInfo->socket, &readSet);

    // 设置超时时间为100毫秒
    timeout.tv_sec = 1;
    timeout.tv_usec = 0; // 100毫秒

    retSelect = select(0, &readSet, NULL, NULL, &timeout);
    if (retSelect == SOCKET_ERROR) { 
      WSAerror = WSAGetLastError(); 
      exitInfo = getPrintf("线程退出，选择错误，接收：%d，WAS代码：:%d ", 
          bytesReceived, WSAerror);
      break;
    }
    else if (retSelect == 0) {  // 超时，没有数据可读，继续循环 
      updataConsoleTitle(titleString, GetCurrentThreadId());
      continue;
    }

    // 接收数据
    bytesReceived = recv(clientInfo->socket, tcpRecvBuffer, sizeof tcpRecvBuffer - 1, 0);
    if (bytesReceived == 0) { // 客户端正常关闭连接 
        WSAerror = WSAGetLastError();
        exitInfo = getPrintf("线程退出，优雅地断开连接，WSA代码：%d，接收：%d", 
            WSAerror, bytesReceived);
        break;
    }
    else if (bytesReceived <= SOCKET_ERROR) {
        WSAerror = WSAGetLastError();
        if (WSAerror == WSAEWOULDBLOCK) 
            continue;       // 非阻塞模式下没有数据是正常情况 
        else if (WSAerror == WSAECONNRESET || WSAerror == WSAECONNABORTED) { 
            exitInfo = getPrintf( "线程退出，连接被重置（拔网线），WSA代码：%d，接收：%d", 
                WSAerror, bytesReceived);
            break;
        }
        else { // 其他错误，断开连接
            exitInfo = getPrintf("线程退出，接收错误，WSA代码：%d，接收：%d", 
                WSAerror, bytesReceived);
            break;
        }
    }

    // 正常接收到数据
    tcpRecvBuffer[bytesReceived] = '\0';  // 防止命令解析异常

    // 检查是否是控制命令
    if (strncmp(tcpRecvBuffer, CTRL_HEADER, strlen(CTRL_HEADER)) == 0) {
        if (runInfo.serverPrintData == 3) 
          SafePrintf("Client [%-2d]IP:%s len:%d cmd: %-60s\n", 
            clientInfo->index, clientInfo->ipAddress, bytesReceived, tcpRecvBuffer);
 
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
    DWORD bytesWritten = ComPortSendData(tcpRecvBuffer, bytesReceived, &getError);
    if( bytesWritten != (DWORD)bytesReceived )
      printfSend(&clientInfo->socket, "COM write error: %ld\n", getError);

    char *Direct = getSendRecvDirectionStr("[TCP --> COM]", clientInfo->index);
    char *timeStr = getCurrentTime(); 

    SafePrintf("%-21s%6I64d [%s]  %-6ld/%-6d Byte (%s : %ld)%s%c", timeStr, ++sendCount, Direct,
            bytesWritten, bytesReceived, bytesWritten == (DWORD)bytesReceived? "OK" : "Fail", getError,
            runInfo.serverPrintData != 0 ? " data:" : " ", runInfo.COMSendPoll? '\n':'\r');
            
    if (runInfo.serverPrintData != 0) {
        if (runInfo.serverPrintData == 1)
          SafePrintf("%s", tcpRecvBuffer);
          
        if (runInfo.serverPrintData == 2) 
          printf_hex8((uint8_t*)tcpRecvBuffer, bytesReceived, 40, 2);
    }

    // 收到客户端数据时（发送到串口）
    #ifdef __TRAFFIC_STATS_H_
    trafficStats.net.totalBytesReceived += bytesReceived;
    trafficStats.com.totalBytesSent += bytesWritten;
    #endif
  }

  CloseClient(clientInfo, true, exitInfo);
  return 0;
}

// 返回：如果为真请结束循环不要发给串口，假的放形继续
static bool sendMonopolizeExamine(ClientNode_t* clientInfo)
{
  // 串口发上来的数据是否被独占。
  if( runInfo.monopolizeComRecvIndex && *runInfo.monopolizeComRecvIndex != clientInfo->index){ 
    const char *ClientIP = getClientIP(*runInfo.monopolizeComRecvIndex);
    if( ClientIP != NULL && runInfo.monopolizeComSendIndex == NULL) {
      printfSend(&clientInfo->socket, "Send data to COM, but [%-2d]IP:%s "
        "monopolize! You cannot receive COM data\n", *runInfo.monopolizeComRecvIndex, ClientIP ); 
    }

    if( ClientIP == NULL )
      runInfo.monopolizeComRecvIndex = NULL;
  }
  
  // 发送给串口的数据是否被指定客户端独占
  if( runInfo.monopolizeComSendIndex && *runInfo.monopolizeComSendIndex != clientInfo->index){ 
    const char *ClientIP = getClientIP(*runInfo.monopolizeComSendIndex);
    if( ClientIP != NULL ) {
      printfSend(&clientInfo->socket, "Send data to COM, but [%-2d]IP:%s monopolize!\n",
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
  if( ip == NULL )
    return false;
  
  if( clientPool == NULL )
    return false;

  EnterCriticalSection(&csClient);
  
  ClientNode_t* newNode = ClientPool_Alloc();
  if (!newNode) { // 池满，踢掉最老的 
    newNode = ClientList_GetOldest();
    if (newNode) {
      printfSend(&newNode->socket, "You are kicked due to server full! Your index %d\n", newNode->index);
      CloseClient(newNode, false, "客户端数量已满");
    }
  }

  if ( newNode == NULL  ) {
    LeaveCriticalSection(&csClient);
    SafePrintf("No Client Node\n");
    return false;
  }

  newNode->socket = socket;
  newNode->connectTime = GetCurrentTimeMs();
  memset(newNode->ipAddress, 0, sizeof newNode->ipAddress);
  strncpy(newNode->ipAddress, ip, strlen(ip) < sizeof newNode->ipAddress? 
          strlen(ip) : sizeof newNode->ipAddress);
  ClientList_Add(newNode);
  
  // 创建线程
  InterlockedExchange(&newNode->isClosing, 0);
  newNode->hThread = CreateThread(NULL, 0, ClientRecvDataThread, newNode, 0, &newNode->threadId);
  if (newNode->hThread) {
      static uint64_t connectCount = 0;
      SafePrintf("Client [%-2d]IP:%-16s Connected %d/%d Count:%I64d\n",
          newNode->index, newNode->ipAddress, *ClientsNum.count, getMaxClient(), ++connectCount);
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
  char clientInfo[40];
  
  for (ClientNode_t* curr = clientList.head; curr && strLen < len; curr = curr->next) {
    memset(clientInfo, 0, sizeof clientInfo);
    snprintf(clientInfo, sizeof clientInfo, 
        "client [%-2d]IP:%-16s\n", curr->index, curr->ipAddress);
    uint16_t infoLen = strlen(clientInfo);
    if (strLen + infoLen >= len) 
      break;
    strcat(retStr, clientInfo);
    strLen += infoLen; 
  }
  LeaveCriticalSection(&csClient);
}

static void ClientTrueClose(asyncRequest_t* request, bool Async) 
{ 
  DWORD waitResult = 0;
  BOOL CloseHandleRet = true;
  char *CloseInfo = "reason:";

  // 先关闭套接字，促使客户端接收线程退出。异步关闭不能放在这里执行
  // if (request->node.socket != INVALID_SOCKET) 
  //     request->closeSocketRet = closesocket( request->node.socket );

  // 如果是线程自己调用的关闭，不等待也不立即关闭句柄
  if (request->node.hThread && request->isSelfCall == false) {
    // 外部调用，等待线程退出
    waitResult = WaitForSingleObject(request->node.hThread, 300);
    if (waitResult == WAIT_TIMEOUT) { 
        DWORD exitCode;
        BOOL GetExitRet = GetExitCodeThread(request->node.hThread, &exitCode);
        if ( GetExitRet && exitCode == STILL_ACTIVE) {
            TerminateThread(request->node.hThread, 0); // 线程不退出会始终卡死
            // CloseHandleRet = CloseHandle(request->node.hThread);
            SafePrintf("Force terminated client thread %d\n", request->node.index);
        }
    }

    CloseHandleRet = CloseHandle(request->node.hThread);
    CloseInfo = getPrintf("HandleExit:%s wait:%ld reason:", CloseHandleRet ? "OK":"Fail", waitResult);
  }

  SafePrintf("Client [%-2d]IP:%-16s Closed %csync [%s] sok:%s %s%s%s", 
      request->node.index, request->node.ipAddress, Async? 'A' : ' ',
      request->isSelfCall? "SelfCall":"ExternalCall", request->closeSocketRet==0? "OK":"Fail",
      CloseInfo, request->reason, g_clientsNum->count == 0 ? "\n\n":"\n");
}


static void CloseClient(ClientNode_t* node, BOOL isSelfCall, const char *reason)
{ 
  if (!node) return;

  // 使用原子操作确保只有一个线程执行关闭
  if (InterlockedCompareExchange(&node->isClosing, 1, 0) != 0) {
      SafePrintf("Client [%-2d]IP:%-16s is already closed [%s], reason: %s%s", 
          node->index, node->ipAddress, isSelfCall?"SelfCall":"ExternalCall", 
          reason? reason:"未知", g_clientsNum->count == 0 ? "\n\n":"\n"  );
      return;
  }
  
  asyncRequest_t request;
  strcpy(request.reason, reason);
  request.node = *node; // 保存客户端节点副本
  request.isSelfCall = isSelfCall; // 记录是否是线程自己调用的
  //先关闭套接字，促使客户端接收线程退出，异步关闭的话要尽快促使线程退出
  request.closeSocketRet = SOCKET_ERROR;
  if (request.node.socket != INVALID_SOCKET) 
    request.closeSocketRet = closesocket( request.node.socket );

  EnterCriticalSection(&csClient);
  ClientList_Remove(node);
  LeaveCriticalSection(&csClient);   

  BOOL useAsync = AddDataToAsyncQueue(&asyncCloseQueue, (char*)&request, sizeof request); 
  if (!useAsync)
    ClientTrueClose(&request, false); 
}

// 异步关闭回调函数
static void AsyncCloseCallback(queueData_t *queueData) 
{
  if (queueData == NULL)  return;
  ClientTrueClose((asyncRequest_t*)queueData->data, true);
}


// sendDataToClients 专用错误处理函数不可外用
static void sendFailErrorHandle(bool wide, ClientNode_t *ClientInfo, int error, 
  uint16_t *closeCount, int *errorList, ClientNode_t **clientsToClose, 
  uint64_t *retCurrentTime, int sendRet, int sendLen)
{
  *retCurrentTime = GetCurrentTimeMs(); 
  uint64_t timeDiff = ClientInfo? *retCurrentTime - ClientInfo->tempUnavStart: 0;
  if( timeDiff > 9999 )
    timeDiff = 9999;

  static uint8_t timeDiffIsChange = 0;
  if( timeDiffIsChange != timeDiff/200 ){
    timeDiffIsChange = timeDiff/200;

    SafePrintf("%s播发送 错误:%6d，超时:%4I64d ms ==> %-2d %-16s]  %-6d/%-6d Byte (%s : %d)     \n",
    wide? "广":"单", error, timeDiff,
    ClientInfo? ClientInfo->index:-1, 
    ClientInfo? ClientInfo->ipAddress:"Unknown IP", 
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
        targetClient = lastFoundClient = FindClientBySocket(socket);

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
    char *failInfo = getPrintf("%s播发送失败，持续不可用时间：%I64d ms，WSA代码：%d",
        socket? "单" : "广", currentTime - clientsToClose[i]->tempUnavStart, errorList[i]);
    CloseClient(clientsToClose[i], false, failInfo); 
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
int printfSend(SOCKET *Socket, const char *fmt, ...)
{
	static char stringBuff[1024]; // 字符串缓冲区
	memset(stringBuff, 0, sizeof stringBuff);
  strcpy(stringBuff, CTRL_HEADER);
  uint8_t ctrlHeaderLen = strlen(CTRL_HEADER);

  // args为定义的一个指向可变参数的变量，va_list以及下边要用到的
  // va_start,va_end都是是在定义可变参数函数中必须要用到宏，在stdarg.h头文件中定义
	va_list args; 
  va_start(args, fmt);
  int retLen = vsnprintf(stringBuff + ctrlHeaderLen, 
    sizeof stringBuff - ctrlHeaderLen, fmt, args);
  va_end(args); // 初始化args的函数，使其指向可变参数的第一个参数，fmt是可变参数的前一个参数

  return sendDataToClients(Socket, stringBuff, retLen + ctrlHeaderLen); 
}


/**
 * @brief 踢掉所有已连接的客户端
 * @param reason 踢掉客户端的原因（可选，可为NULL）
 * @param graceful 是否优雅关闭（TRUE:发送通知后关闭, FALSE:强制立即关闭）
 */
void KickAllClientsEx(const char* reason) 
{
  const char* kickReason = reason? reason : "NULL";
  printfSend(NULL, "Kicking all clients: %s\n", kickReason);

  for (ClientNode_t* next, * curr = clientList.head; curr; curr = next) {
      next = curr->next; 
      CloseClient(curr, false, kickReason); // 使用 FALSE 表示外部调用
  }
  
  // 重置计数和状态
  clientList.count = 0;
  runInfo.monopolizeComSendIndex = NULL;
  runInfo.monopolizeComRecvIndex = NULL;
  SafePrintf("Kicking all clients : %s\n", kickReason); 
}
