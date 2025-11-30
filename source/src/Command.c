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
#include "discovery.h"
#include "main.h"
#include "configSave.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef _WIN32
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#endif


/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地数据类型   =========================================*/
typedef void (*CmdHandlerFunc)(socket_t*, char*);

typedef struct {
    const char* cmdName;
    CmdHandlerFunc handler;
} CommandEntry;

/*================== 本地宏定义     =========================================*/
#define DECOLLATOR    ",\n"

/*================== 本地函数声明   =========================================*/
static void broadcastSendHandleResult(socket_t *Socket, const char *info);
static void cmdServerOverExit(socket_t*, char*);
static void cmdComlist(socket_t*, char*);
static void cmdRunNewServer(socket_t*, char*);
static void cmdSetComAsyncSend(socket_t*, char*);
static void cmdSetComAsyncRecv(socket_t*, char*);
static void cmdPrintAllclientIP(socket_t*, char*);
static void cmdSetMonopolize(socket_t*, char*);
static void cmdDataPrintMode(socket_t*, char*);
static void cmdRunSystemCmd(socket_t*, char*);
static void cmdServerConnect(socket_t*, char*);
static void cmdOpenSerialCom(socket_t*, char*);
static void cmdDoNotConnectCOM2TCP(socket_t*, char*);
static void cmdSetLogPollCut(socket_t*, char*);
static void cmdKickAllClients(socket_t*, char*);
static void cmdsetCOMalignedNum(socket_t*, char*);



/*================== 命令映射表     =========================================*/
static const CommandEntry cmdTable[] = {
  {"comlistVPID",           cmdComlist},
  {"comlistID",             cmdComlist},
  {"comlist",               cmdComlist},
  {"runNewServer",          cmdRunNewServer},
  {"PrintAllclientIP",      cmdPrintAllclientIP},
  {"setCOMasyncSend",       cmdSetComAsyncSend},
  {"setCOMasyncRecv",       cmdSetComAsyncRecv},
  {"setCOMalignedNum",      cmdsetCOMalignedNum},
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

void HandleClientCommand(socket_t *Socket, const char* command)
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
  
  for (uint8_t i = 0; i < sizeof cmdTable / sizeof cmdTable[0]; i++) 
    if (strnicmp(command, cmdTable[i].cmdName, strlen(cmdTable[i].cmdName)) == 0) {
        cmdTable[i].handler(Socket, handleCommand);
        return;
    }
  printfSend(Socket, "Not Command:%s\n", handleCommand);
}

static void cmdKickAllClients(socket_t *Socket, char* commandData)
{
  (void)commandData;

  printfSend(Socket, "Kick Clients Num %d\n", getClientNum());
  if( getClientNum() == 0) 
    return; 

  const char *exitInfo;
  if( getDiscoverySocket() == *Socket ) {
    exitInfo = getPrintf("Discovery UDP IP [%s]:%d",
        getDiscoveryNewClientIPAddr(), getDiscoveryNewClientPort());
  }
  else{
    uint16_t ClientIndex = 0;
    bool getRet = getClientIndex(Socket, &ClientIndex); 
    exitInfo = getPrintf("Client TPC IP [%d]:%s", getRet? ClientIndex:-1, 
        getRet? getClientIP(ClientIndex):"invalid");
  }
  SafePrintf("\033[H\033[J 全员下线 %s\n", exitInfo); 
  KickAllClients(exitInfo);
}

static void cmdSetLogPollCut(socket_t *Socket, char* commandData)
{
  (void)Socket; (void)commandData;
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
  
  if(token != NULL){
    if( strnicmp(token, "Recv", strlen("Recv") ) == 0 ){
      saveInfo.COMrecvPoll = !saveInfo.COMrecvPoll;
    }
    else if( strnicmp(token, "send", strlen("send") ) == 0 ){
      saveInfo.COMsendPoll = !saveInfo.COMsendPoll;
    }
    else{
      saveInfo.COMrecvPoll = !saveInfo.COMrecvPoll;
      saveInfo.COMsendPoll = !saveInfo.COMsendPoll;
    }
  }
  else{
    saveInfo.COMrecvPoll = !saveInfo.COMrecvPoll;
    saveInfo.COMsendPoll = !saveInfo.COMsendPoll;
  }
  
  const char *setInfo = getPrintf( "Set log Poll [send %-3s | recv %-3s]\n",
    saveInfo.COMsendPoll? "YES":"NO", saveInfo.COMrecvPoll? "YES":"NO" );
  broadcastSendHandleResult(Socket, setInfo);
  saveConfig();
}

static void cmdDoNotConnectCOM2TCP(socket_t *Socket, char* commandData)
{
  (void)commandData;
  printfSend(Socket, "Please do not connect COM2TCP!\n" );
  CloseClientSocket(Socket, "请不要互联串口转服务器程序！");
}

static void cmdComlist(socket_t *Socket, char* commandData)
{
  for( uint8_t i=0; commandData[i]; i++ ){
    if( commandData[i] == 'i' ) commandData[i] = 'I';
    if( commandData[i] == 'd' ) commandData[i] = 'D';
  }

  sendComPortsListToClient(Socket, strstr(commandData, "ID") ? true:false ); 
}

static void cmdServerOverExit(socket_t *Socket, char* commandData)
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
  voluntaryWithdrawal( exitInfo );
}

static void cmdPrintAllclientIP(socket_t *Socket, char* commandData)
{
  (void)commandData;
  static char handleString[512];
  memset(handleString, 0, sizeof handleString);
  getAllClientIPandIndexInfo(handleString, sizeof handleString);
  SafePrintf("All %d/%d Client index IP:\n%s\n", getClientNum(), getMaxClient(), handleString);
  printfSend(Socket, "All %d/%d Client index IP\n%s\n", getClientNum(), getMaxClient(), handleString);
}

static void cmdRunNewServer(socket_t *Socket, char* commandData)
{
#ifdef _WIN32
  char path[MAX_PATH + 50];
  strcpy(path, "start \"\" \"");
  if (GetModuleFileName(NULL, path + strlen(path), MAX_PATH) == 0) { 
    printfSend(Socket, "Error: Get Server File Name Path failed (%ld)\n",  GetLastError());
    return;
  }
  strcat(path, "\" ");
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
  if( token )
    strcat(path, token);

  int cmdret = system(path);
  SafePrintf("run New Server, result:%d, Run Cmd: %s\n", cmdret, path);
  printfSend(Socket, "run New Server result:%d, arg:%s\n", cmdret, token?token:"NULL");
#else
  char path[1024];
  ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (len != -1) {
    path[len] = '\0';
    
    char full_cmd[2048];
    snprintf(full_cmd, sizeof(full_cmd), "%s &", path);
    
    char *token = strtok(commandData, DECOLLATOR);
    token = strtok(NULL, DECOLLATOR);
    if( token ) {
      snprintf(full_cmd, sizeof(full_cmd), "%s %s &", path, token);
    }
     
    int cmdret = system(full_cmd);
    SafePrintf("run New Server, result:%d, Run Cmd: %s\n", cmdret, path);
    printfSend(Socket, "run New Server result:%d, arg:%s\n", cmdret, token?token:"NULL");
  } else {
    printfSend(Socket, "Error: Get Server File Name Path failed\n");
  }
#endif
}

static void cmdSetComAsyncSend(socket_t *Socket, char* commandData)
{
  (void)Socket; 
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
  uint16_t num = token==NULL? 0:atoi(token);
  bool ret = COM_UseAsyncSend(num);
  const char * setInfo = getPrintf("set COM Send %csync %s Queue num %d/%d ~ %d\n", 
    num  == 0? ' ':'A', ret? "OK!":"Fail! scope !", num, MIN_QUEUE_SIZE, MAX_QUEUE_SIZE);
  broadcastSendHandleResult(Socket, setInfo);
}

static void cmdSetComAsyncRecv(socket_t *Socket, char* commandData)
{
  (void)Socket; 
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
  uint16_t num = token==NULL? 0:atoi(token);
  bool ret = COM_UseAsyncRecv(num);
  const char *setInfo = getPrintf("set COM Recv %csync %s Queue num %d/%d ~ %d\n", 
    num  == 0? ' ':'A', ret? "OK!":"Fail! scope !", num, MIN_QUEUE_SIZE, MAX_QUEUE_SIZE);
  broadcastSendHandleResult(Socket, setInfo);
}

static void cmdsetCOMalignedNum(socket_t *Socket, char* commandData)
{
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
  uint8_t in4KBnum = atoi(token? token : "0");
  uint8_t max4KBnum = ((RECV_BUFFER_SIZE) / 4096) - 1 ;

  if( in4KBnum > max4KBnum )
    in4KBnum = max4KBnum;
  saveInfo.COMalignedRecv4K = in4KBnum * 4096;
  printfSend(Socket, "Set COM revc 4KByte Number: %d/%d\n", 
        in4KBnum, max4KBnum);
}

static void cmdSetMonopolize(socket_t *Socket, char* commandData)
{
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);

  char clientStr[30];
  memset(clientStr, 0, sizeof clientStr);

  if( strnicmp(token, "Recv", strlen("Recv") ) == 0 ){
    token = strtok(NULL, DECOLLATOR);

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
    token = strtok(NULL, DECOLLATOR);

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

static void cmdDataPrintMode(socket_t *Socket, char* commandData)
{ 
  (void)Socket;

  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
  saveInfo.serverPrintData = 0;
  if( strnicmp(token, "NULL", strlen("NULL") ) == 0 )
    saveInfo.serverPrintData = 0;
  if( strnicmp(token, "ASCII", strlen("ASCII")) == 0 )
    saveInfo.serverPrintData = 1;
  if( strnicmp(token, "HEX", strlen("HEX")) == 0 )
    saveInfo.serverPrintData = 2;
  if( strnicmp(token, "CMD", strlen("CMD")) == 0 )
    saveInfo.serverPrintData = 3;
  
  const char *setInfo = getPrintf("server Print Data: %d %s \n", 
      saveInfo.serverPrintData, token);
  broadcastSendHandleResult(Socket, setInfo);
}

static void cmdRunSystemCmd(socket_t *Socket, char* commandData)
{
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
  int ret = system(token);
  printfSend(Socket, "execute system cmd %s :%d\n", 
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

  socket_t *replySocket = (socket_t*)arg;

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

static void cmdServerConnect(socket_t *Socket, char* commandData) 
{
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
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
  token = strtok(NULL, DECOLLATOR);
  uint16_t port = atoi( token? token: "1000");
  if ( port == 0) {
    if( Socket )
      printfSend(Socket, "Invalid port number: %s\n", token);
    return;
  }
  
  printfSend(Socket, "Ready Connect to %s:%d ...\n", serverAddress, port);
  ConnectToServer(serverAddress, port, connectServerResultCoback, Socket);
}

static void cmdOpenSerialCom(socket_t *Socket, char* commandData) 
{
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);

  if( token == NULL || 
      (strnicmp(token, "COM", strlen("COM")) != 0 && 
       strnicmp(token, "tty", strlen("tty")) != 0) ) {
    printfSend(Socket, "The input is not :%s\n", token == NULL? "NULL":token);
    return;
  }

  char portName[20], *endptr;
  memset(portName, 0, sizeof portName);
  
#ifdef _WIN32
  if (strnicmp(token, "COM", strlen("COM")) == 0) {
    snprintf(portName, sizeof portName, "COM%d", (int)strtol(token + strlen("COM"), &endptr, 10));
  }
#else
  if (strnicmp(token, "tty", strlen("tty")) == 0) {
    snprintf(portName, sizeof portName, "%s", token);
  }
  else if (strnicmp(token, "COM", strlen("COM")) == 0) {
    // Windows格式的COM端口号转换为Linux格式
    int comNum = (int)strtol(token + strlen("COM"), &endptr, 10);
      snprintf(portName, sizeof portName, 
         comNum < 4? "ttyS%d":"ttyUSB%d", comNum - (comNum < 4? 0:4));
  }
#endif

  if( strcmp(portName, getComName()) == 0 ){
    printfSend(Socket, "the %s has been turned on\n", portName);
    return;
  }

  uint32_t baudRate = 921600;
  uint8_t dataBits = 8, stopBits = 1, parity = 0;
  token = strtok(NULL, DECOLLATOR);
  if (token) baudRate = atoi(token);

  token = strtok(NULL, DECOLLATOR);
  if (token) dataBits = atoi(token);

  token = strtok(NULL, DECOLLATOR);
  if (token) stopBits = atoi(token);

  token = strtok(NULL, DECOLLATOR);
  if (token) parity = atoi(token);

  if ( getComIsOpen() ){ 
    char *reason = getPrintf("Open New %s", portName);
    CloseComPort(reason);
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

static void broadcastSendHandleResult(socket_t *Socket, const char *info)
{ 
  uint16_t sendLen = strlen(info);
  if( getDiscoverySocket() == *Socket )
    printfSend(Socket, info, sendLen);
  printfSend(NULL, info, sendLen);
}