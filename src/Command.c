/******************************************************************************
  * @file    文件 Command.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 处理命令
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "Command.h"
#include "public.h"
#include "COM.h"
#include "clients.h"
#include "logPrint.h"
#include "Queue.h"
#include "ServerConnect.h"
#include "main.h"
#include "discovery.h"

#include <stdio.h>

/*================== 本地数据类型   =========================================*/
typedef void (*CmdHandlerFunc)(SOCKET*, char*);

typedef struct {
    const char* cmdName;
    CmdHandlerFunc handler;
} CommandEntry;

/*================== 本地宏定义     =========================================*/
#define DECOLLATOR    ",\n"

/*================== 全局共享变量   =========================================*/
/*================== 本地常量声明   =========================================*/
/*================== 本地变量声明   =========================================*/
/*================== 本地函数声明   =========================================*/
static void broadcastSendHandleResult(SOCKET *Socket, const char *info);
static void cmdServerOverExit(SOCKET*, char*);
static void cmdComlist(SOCKET*, char*);
static void cmdRunNewServer(SOCKET*, char*);
static void cmdSetComAsyncSend(SOCKET*, char*);
static void cmdSetComAsyncRecv(SOCKET*, char*);
static void cmdPrintAllclientIP(SOCKET*, char*);
static void cmdSetMonopolize(SOCKET*, char*);
static void cmdDataPrintMode(SOCKET*, char*);
static void cmdRunSystemCmd(SOCKET*, char*);
static void cmdServerConnect(SOCKET*, char*);
static void cmdOpenSerialCom(SOCKET*, char*);
static void cmdDoNotConnectCOM2TCP(SOCKET*, char*);
static void cmdSetLogPollCut(SOCKET*, char*);
static void cmdKickAllClients(SOCKET*, char*);


/*================== 命令映射表     =========================================*/
static const CommandEntry cmdTable[] = {
  {"comlistVPID",           cmdComlist},
  {"comlistID",             cmdComlist},
  {"comlist",               cmdComlist},
  {"runNewServer",          cmdRunNewServer},
  {"PrintAllclientIP",      cmdPrintAllclientIP},
  {"setCOMasyncSend",       cmdSetComAsyncSend},
  {"setCOMasyncRecv",       cmdSetComAsyncRecv},
  {"setCOMdata",            cmdSetMonopolize},
  {"exit",                  cmdServerOverExit},
  {"serverPrintData",       cmdDataPrintMode},
  {"SystemCommands",        cmdRunSystemCmd},
  {"ExecuteSystemCommands", cmdRunSystemCmd},
  {"serverConnect",         cmdServerConnect},
  {"OK! your index",        cmdDoNotConnectCOM2TCP},
  {"Not Command",           cmdDoNotConnectCOM2TCP},
  {"SetLogPollCut",         cmdSetLogPollCut},
  {"KickAllClients",        cmdKickAllClients},
  {"open",                  cmdOpenSerialCom}
};

/*================== 外部函数声明   =========================================*/
/*================== 外部变量声明   =========================================*/

/**
 * @brief 处理客户端发过来的指令
 * @param Socket    客户端套接字，必须有！
 * @param command   命令字符串
 * @return
 * @attention
 */
void HandleClientCommand(SOCKET *Socket, const char* command)
{ 
  if( Socket == NULL ){
    SafePrintf("Command Handle Not Client Socket !\n");
    return;
  }

  static char handleCommand[200];
  memset(handleCommand, 0, sizeof handleCommand);
  uint8_t maxCopyLen = strlen(command) < (sizeof handleCommand) - 2?
                       strlen(command) : (sizeof handleCommand) - 2; 
  memcpy(handleCommand, command, maxCopyLen); 
  // 查找并执行命令
  for (uint8_t i = 0; i < sizeof cmdTable / sizeof cmdTable[0]; i++) 
    if (strnicmp(command, cmdTable[i].cmdName, strlen(cmdTable[i].cmdName)) == 0) {
        cmdTable[i].handler(Socket, handleCommand);
        return;
    }
  printfSend(Socket, "Not Command:%s\n", handleCommand);
}

// 让所有客户端下线
static void cmdKickAllClients(SOCKET *Socket, char* commandData)
{
  (void)commandData;

  printfSend(Socket, "Kick Clients Num %d\n", getClientNum());
  if( getClientNum() == 0) 
    return; 

  if( getDiscoverySocket() == *Socket ) {
    const char *exitInfo = getPrintf("UDP IP %s:%d", 
        getDiscoveryNewClientIPAddr(), getDiscoveryNewClientPort());
    KickAllClients(exitInfo);
    return;
  }

  printfSend(Socket, "Sorry, it can't be achieved for the time being\n" );
}

// 收发日志是否滚动
static void cmdSetLogPollCut(SOCKET *Socket, char* commandData)
{
  (void)Socket; (void)commandData;
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR); //Recv or Send
  if(token != NULL){
    if( strnicmp(token, "Recv", strlen("Recv") ) == 0 )
      runInfo.COMrecvPoll = !runInfo.COMrecvPoll;
    else if( strnicmp(token, "send", strlen("send") ) == 0 )
      runInfo.COMSendPoll = !runInfo.COMSendPoll;
    else{
      runInfo.COMrecvPoll = !runInfo.COMrecvPoll;
      runInfo.COMSendPoll = !runInfo.COMSendPoll;
    }
  }
  else{
    runInfo.COMrecvPoll = !runInfo.COMrecvPoll;
    runInfo.COMSendPoll = !runInfo.COMSendPoll;
  }
  
  const char *setInfo = getPrintf( "Set log Poll [send %-3s | recv %-3s]\n",
    runInfo.COMSendPoll? "YES":"NO", runInfo.COMrecvPoll? "YES":"NO" );
  broadcastSendHandleResult(Socket, setInfo);
}

// 异步自我关闭
static void AsyncSelfCloseClient(void *arg)
{
  CloseClientExt((SOCKET*)arg, "请不要互联串口转服务器程序！");
}

static void cmdDoNotConnectCOM2TCP(SOCKET *Socket, char* commandData)
{
  (void)commandData;
  printfSend(Socket, "Please do not connect COM2TCP!\n" );
  addAsyncFuncHandle(AsyncSelfCloseClient, Socket);
}

static void cmdComlist(SOCKET *Socket, char* commandData)
{
  // 如果遇到小写id就改成大写ID
  for( uint8_t i=0; commandData[i]; i++ ){
    if( commandData[i] == 'i' ) commandData[i] = 'I';
    if( commandData[i] == 'd' ) commandData[i] = 'D';
  }

  sendComPortsListToClient(Socket, strstr(commandData, "ID") ? true:false ); 
}

static void cmdServerOverExit(SOCKET *Socket, char* commandData)
{ 
  (void)commandData;
  uint16_t port = getMainServerPort();
  if( port == 10000 ){
    printfSend(Socket, "sorry the server refuse exit. because Port:%d\n", port);
    return;
  }
  
  uint16_t ClientIndex = 0;
  bool getRet = false;
  char *exitInfo = "Unknown Client Ask For Server Ready Exit\n";
  if( getDiscoverySocket() == *Socket){
    exitInfo = getPrintf("Discovery UDP IP %s:%d Ask For Server Ready Exit\n", 
      getDiscoveryNewClientIPAddr(), getDiscoveryNewClientPort());
  }
  else{ 
    getRet = getClientIndex(Socket, &ClientIndex);
    if( getRet )
      exitInfo = getPrintf("Client [%-2d]IP:%s Ask For Server Ready Exit\n", 
        ClientIndex, getClientIP(ClientIndex) );
  }

  broadcastSendHandleResult(Socket, exitInfo);
  SafePrintf("\033[H\033[J \n%s%s\n", exitInfo, exitInfo); 
  exit(0);
}

static void cmdPrintAllclientIP(SOCKET *Socket, char* commandData)
{
  (void)commandData;
  static char handleString[512];
  memset(handleString, 0, sizeof handleString);
  getAllClientIPandIndexInfo(handleString, sizeof handleString);
  SafePrintf("All %d/%d Client index IP:\n%s\n", getClientNum(), getMaxClient(), handleString);
  printfSend(Socket, "All %d/%d Client index IP\n%s\n", getClientNum(), getMaxClient(), handleString);
}

// 运行一个新服务器程序
static void cmdRunNewServer(SOCKET *Socket, char* commandData)
{
  char path[MAX_PATH + 50];
  strcpy(path, "start \"\" \"");
  if (GetModuleFileName(NULL, path + strlen(path), MAX_PATH) == 0) { 
    printfSend(Socket, "Error: Get Server File Name Path failed (%ld)\n",  GetLastError());
    return;
  }
  strcat(path, "\" ");
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR); // 传递参数
  if( token )
    strcat(path, token);

  system(path);
  SafePrintf("run New Server, Run Cmd: %s\n", path);
  printfSend(Socket, "run New Server, arg:%s\n", token?token:"NULL");
}

// 设置客户端数据异步发给串口
static void cmdSetComAsyncSend(SOCKET *Socket, char* commandData)
{
  (void)Socket; 
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
  uint16_t num = token==NULL? 0:atoi(token);
  BOOL ret = COM_UseAsyncSend(num);
  const char * setInfo = getPrintf("set COM Send %csync %s Queue num %d/%d ~ %d\n", 
    num  == 0? ' ':'A', ret? "OK!":"Fail! scope !", num, MIN_QUEUE_SIZE, MAX_QUEUE_SIZE);
  broadcastSendHandleResult(Socket, setInfo);
}

// 设置收到串口数据异步发给客户端
static void cmdSetComAsyncRecv(SOCKET *Socket, char* commandData)
{
  (void)Socket; 
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
  uint16_t num = token==NULL? 0:atoi(token);
  BOOL ret = COM_UseAsyncRecv(num);
  const char *setInfo = getPrintf("set COM Recv %csync %s Queue num %d/%d ~ %d\n", 
    num  == 0? ' ':'A', ret? "OK!":"Fail! scope !", num, MIN_QUEUE_SIZE, MAX_QUEUE_SIZE);
  broadcastSendHandleResult(Socket, setInfo);
}

// 设置独占信息
static void cmdSetMonopolize(SOCKET *Socket, char* commandData)
{
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR); //Recv Send  my or All

  char clientStr[30];
  memset(clientStr, 0, sizeof clientStr);

  if( strnicmp(token, "Recv", strlen("Recv") ) == 0 ){
    token = strtok(NULL, DECOLLATOR); // my or All 

    if( strnicmp(token, "my", strlen("my") ) == 0 ){
      static uint16_t monopolizeComRecvIndex = 0;
      bool retIndexSta = getClientIndex(Socket, &monopolizeComRecvIndex);
      runInfo.monopolizeComRecvIndex = retIndexSta? &monopolizeComRecvIndex:NULL;
      if( retIndexSta )
        snprintf(clientStr, sizeof clientStr,
          "[%-2d]:IP%s", monopolizeComRecvIndex, getClientIP(monopolizeComRecvIndex));
      else{
        strcpy(clientStr, "[NO client] amend All");
        printfSend(Socket, "%s\n", clientStr);
      }
    }
    else{
      strcpy(clientStr, "All");
      runInfo.monopolizeComRecvIndex = NULL;
    } 
    const char *setInfo = getPrintf("Set COM --> TCP %s Client\n", clientStr);
    broadcastSendHandleResult(Socket, setInfo);
  }
  else if( strnicmp(token, "Send", strlen("Send") ) == 0){
    token = strtok(NULL, DECOLLATOR); // my or All 

    if( strnicmp(token, "my", strlen("my") ) == 0 ){
      static uint16_t monopolizeComSendIndex = 0;
      bool retIndexSta = getClientIndex(Socket, &monopolizeComSendIndex);
      runInfo.monopolizeComSendIndex = retIndexSta? &monopolizeComSendIndex:NULL;
      if( retIndexSta )
        snprintf(clientStr, sizeof clientStr,
          "[%-2d]:IP%s", monopolizeComSendIndex, getClientIP(monopolizeComSendIndex));
      else{
        strcpy(clientStr, "[NO client] amend All");
        printfSend(Socket, "%s\n", clientStr);
      }
    }
    else{
      strcpy(clientStr, "All");
      runInfo.monopolizeComSendIndex = NULL;
    } 
    const char *setInfo = getPrintf("Set TCP %s Client --> COM \n", clientStr);
    broadcastSendHandleResult(Socket, setInfo);
  }
  else
    printfSend(Socket, "Set Monopolize No Recv or send\n"); 
}

// 数据打印模式
static void cmdDataPrintMode(SOCKET *Socket, char* commandData)
{ 
  (void)Socket;

  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR); // 显示模式
  runInfo.serverPrintData = 0;
  if( strnicmp(token, "NULL", strlen("NULL") ) == 0 )
    runInfo.serverPrintData = 0;
  if( strnicmp(token, "ASCII", strlen("ASCII")) == 0 )
    runInfo.serverPrintData = 1;
  if( strnicmp(token, "HEX", strlen("HEX")) == 0 )
    runInfo.serverPrintData = 2;
  if( strnicmp(token, "CMD", strlen("CMD")) == 0 )
    runInfo.serverPrintData = 3;
  
  const char *setInfo = getPrintf("server Print Data: %d %s \n", 
      runInfo.serverPrintData, token);
  broadcastSendHandleResult(Socket, setInfo);
}

// 执行一条系统命令
static void cmdRunSystemCmd(SOCKET *Socket, char* commandData)
{
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR); // 指令 
  int ret = system(token);
  printfSend(Socket, "execute system %s :%d\n", 
      ret == 0? "success": "failed", ret);
}

// 连接服务器结果回调
static void connectServerResultCoback(ConnectState_t State, 
        void* arg, const char *hsot, uint16_t port, uint16_t residueTimeMs)
{
  if( State > 4 )
    State = 4;
  static const char* stateStrings[] = {
    " failed Disconnected",
    "ing",
    "ed OK",
    " invalid", };

  SOCKET *replySocket = (SOCKET*)arg;

  char resolvedIP[46] = {0}, residueTimeMsString[50] = {0};
  bool ResolveRet = ResolveDomainName(hsot, resolvedIP, sizeof resolvedIP);
  
  snprintf(residueTimeMsString, sizeof residueTimeMsString,
    "Please Wait %d/%d ms", residueTimeMs, CONNECT_TIMEOUT_MS);

  if( replySocket )
    printfSend(replySocket, "Server [%s] [%s:%d] Connect%s %s\n", 
              ResolveRet? hsot:"IP",
              ResolveRet? resolvedIP:hsot, port,
              stateStrings[State], State==1? residueTimeMsString:" ");  
}

// 连接一个远端服务器
static void cmdServerConnect(SOCKET *Socket, char* commandData) 
{
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR); // 远端服务器地址（域名或IP） 或查询连接状态和断开连接
  if (token == NULL) {
    if( Socket )
      printfSend(Socket, "Usage: serverConnect,<hostname|ip>[,port]\n");
    return;
  }

  if (strnicmp(token, "disconnect", strlen("disconnect")) == 0) {
    ConnectToServer("disconnect", 0, connectServerResultCoback, Socket);
    printfSend(Socket, "Disconnected from server\n");
    return;
  }
  char serverAddress[256]; 
  memset(serverAddress, 0, sizeof serverAddress);
  strcpy(serverAddress, token); 
  token = strtok(NULL, DECOLLATOR); // 端口号 
  uint16_t port = atoi( token? token: "1000");
  if ( port == 0) {
    if( Socket )
      printfSend(Socket, "Invalid port number: %s\n", token);
    return;
  }
  
  // 先测试域名解析 
  printfSend(Socket, "Ready Connect to %s:%d ...\n", serverAddress, port);
  ConnectToServer(serverAddress, port, connectServerResultCoback, Socket);
}

// 打开串口命令
static void cmdOpenSerialCom(SOCKET *Socket, char* commandData) 
{
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR); // 串口号

  if( token == NULL || strnicmp(token, "COM", strlen("COM") ) != 0 ) {
    printfSend(Socket, "The input is not :%s\n", token == NULL? "NULL":token);
    return;
  }

  char portName[10], *endptr;
  memset(portName, 0, sizeof portName);
  snprintf(portName, sizeof portName, "COM%d", (int)strtol(token + strlen("COM"), &endptr, 10) );

  if( strcmp(portName, ComPort->portName) == 0 ){ // 防止重复打开同一个串口浪费资源
    printfSend(Socket, "the %s has been turned on\n", portName);
    return;
  }

  /* 解析串口设置的参数 */
  uint32_t baudRate = 921600;
  uint8_t dataBits = 8, stopBits = 1, parity = 0;
  token = strtok(NULL, DECOLLATOR); // 波特率
  if (token) baudRate = atoi(token);

  token = strtok(NULL, DECOLLATOR); // 数据位
  if (token) dataBits = atoi(token);

  token = strtok(NULL, DECOLLATOR); // 停止位
  if (token) stopBits = atoi(token);

  token = strtok(NULL, DECOLLATOR); // 校验位
  if (token) parity = atoi(token);

  if ( ComPort->isOpen ){ 
    char *reason = getPrintf("Open New %s", portName);
    CloseComPort(reason, false);
  }

  printfSend(Socket, "opening %s...\n", portName);
  int8_t ret = OpenComPort(portName, baudRate, dataBits, stopBits, parity); 
  DWORD error = (ret != 0)? GetLastError(): 0;

  const char *comParameter = getPrintf( "open [%s,%d,%d,%d,%d] %s! (%d:%ld)\n", 
      portName,baudRate,dataBits,stopBits,parity,
      ret == 0 ? "success":"failed", ret, error);
  
  broadcastSendHandleResult(Socket, comParameter);
  SafePrintf("%s", comParameter);
}

// 广播发送处理结果
static void broadcastSendHandleResult(SOCKET *Socket, const char *info)
{ 
  uint16_t sendLen = strlen(info);
  if( getDiscoverySocket() == *Socket )
    sendDataToClients(Socket, info, sendLen);
  sendDataToClients(NULL, info, sendLen);
}