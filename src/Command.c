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
static void cmdExit(SOCKET *, char*);
static void cmdComlist(SOCKET *, char*);
static void cmdRunNewServer(SOCKET *, char*);
static void cmdSetComAsyncSend(SOCKET *, char*);
static void cmdSetComAsyncRecv(SOCKET *, char*);
static void cmdPrintAllclientIP(SOCKET *, char*);
static void cmdSetMonopolize(SOCKET *, char*);
static void cmdDataPrintMode(SOCKET *, char*);
static void cmdRunSystemCmd(SOCKET *, char*);
static void cmdServerConnect(SOCKET *, char*);
static void cmdOpenSerialCom(SOCKET *, char*);

/*================== 命令映射表     =========================================*/
static const CommandEntry cmdTable[] = {
  {"comlistVPID",           cmdComlist},
  {"comlistID",             cmdComlist},
  {"comlist",               cmdComlist},
  {"runNewServer",          cmdRunNewServer},
  {"PrintAllclientIP",      cmdPrintAllclientIP},
  {"setCOMasyncSend",       cmdSetComAsyncSend},
  {"setCOMasyncRecv",       cmdSetComAsyncRecv},
  {"setRecvCOMdataTo",      cmdSetMonopolize}, // 旧命令，待删除
  {"setCOMdata",            cmdSetMonopolize},
  {"exit",                  cmdExit},
  {"serverPrintData",       cmdDataPrintMode},
  {"SystemCommands",        cmdRunSystemCmd},
  {"ExecuteSystemCommands", cmdRunSystemCmd},
  {"serverConnect",         cmdServerConnect},
  {"open",                  cmdOpenSerialCom}
};

/*================== 外部函数声明   =========================================*/
/*================== 外部变量声明   =========================================*/

/**
 * @brief 处理客户端发过来的指令
 * @param clientSocket  TCP客户端套接字
 * @param command       命令字符串
 * @return
 * @attention
 */
void HandleClientCommand(SOCKET *clientSocket, char* command) 
{ 
  // 查找并执行命令
  for (uint8_t i = 0; i < sizeof cmdTable / sizeof cmdTable[0]; i++) 
    if (strnicmp(command, cmdTable[i].cmdName, strlen(cmdTable[i].cmdName)) == 0) {
        cmdTable[i].handler(clientSocket, command);
        return;
    }

  // 未找到命令
  printfSend(clientSocket, "Not Command:%s\n", command);
}

static void cmdComlist(SOCKET *clientSocket, char* commandData)
{ 
  sendComPortsListToClient( clientSocket, strstr(commandData, "ID") ? true:false ); 
}

static void cmdExit(SOCKET *clientSocket, char* commandData)
{ 
  (void)clientSocket; (void)commandData; 
  printfSend(NULL, "server ready exit\n" );
  SafePrintf("server ready exit\n" );
  exit(0);
}

static void cmdPrintAllclientIP(SOCKET *clientSocket, char* commandData)
{
  (void)clientSocket; (void)commandData;
  static char handleString[512];
  memset(handleString, 0, sizeof handleString);
  getAllClientIPandIndexInfo(handleString, sizeof handleString);
  SafePrintf("All Client index IP:\n%s\n", handleString);
  printfSend(clientSocket, "All Client index IP\n%s\n", handleString);
}

// 运行一个新服务器程序
static void cmdRunNewServer(SOCKET *clientSocket, char* commandData)
{
  char path[MAX_PATH + 50];
  strcpy(path, "start \"\" \"");
  if (GetModuleFileName(NULL, path + strlen(path), MAX_PATH) == 0) { 
    printfSend(clientSocket, "Error: Get Server File Name Path failed (%ld)\n",  GetLastError());
    return;
  }
  strcat(path, "\" ");
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR); // 传递参数
  if( token )
    strcat(path, token);

  system(path);
  SafePrintf("run New Server, Run Cmd: %s\n", path);
  printfSend(clientSocket, "run New Server, arg:%s\n", token?token:"NULL");
}

// 设置客户端数据异步发给串口
static void cmdSetComAsyncSend(SOCKET *clientSocket, char* commandData)
{
  (void)clientSocket;
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
  uint16_t num = token==NULL? 0:atoi(token);
  
  if( num == 0 ){
    COM_UseAsyncSend(0);
    printfSend(NULL, "set COM send sync\n");
    return;
  }

  BOOL ret = COM_UseAsyncSend(num);
  printfSend(NULL, "set COM send Async %s set Queue num %d/%d ~ %d\n", 
    ret? "OK!":"Fail! scope !", num, MIN_QUEUE_SIZE, MAX_QUEUE_SIZE);
}

// 设置收到串口数据异步发给客户端
static void cmdSetComAsyncRecv(SOCKET *clientSocket, char* commandData)
{
  (void)clientSocket;
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
  uint16_t num = token==NULL? 0:atoi(token);
  
  if( num == 0 ){
    COM_UseAsyncRecv(0);
    printfSend(NULL, "set COM Recv sync\n");
    return;
  }

  BOOL ret = COM_UseAsyncRecv(num);
  printfSend(NULL, "set COM Recv Async %s set Queue num %d/%d ~ %d\n", 
    ret? "OK!":"Fail! scope !", num, MIN_QUEUE_SIZE, MAX_QUEUE_SIZE);
}

// 设置独占信息
static void cmdSetMonopolize(SOCKET *clientSocket, char* commandData)
{
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR); //Recv Send  my or All
  
  #if 1 // 旧命令修改成新命令区域代码
  if( strnicmp(token, "my", strlen("my") ) == 0 || 
      strnicmp(token, "All", strlen("All") ) == 0 ){
      char monopolize[5];
      memset(monopolize, 0, sizeof monopolize);
      strcpy(monopolize, token);
      sprintf(token, "Recv,%s ", monopolize);
      token = strtok(token, DECOLLATOR); // 重新指定一下
  }
  #endif

  char clientStr[30];
  memset(clientStr, 0, sizeof clientStr);

  if( strnicmp(token, "Recv", strlen("Recv") ) == 0 ){
    token = strtok(NULL, DECOLLATOR); // my or All 

    if( strnicmp(token, "my", strlen("my") ) == 0 ){
      static uint16_t monopolizeComRecvIndex = 0;
      bool retIndexSta = getClientIndex(clientSocket, &monopolizeComRecvIndex);
      runInfo.monopolizeComRecvIndex = retIndexSta? &monopolizeComRecvIndex:NULL;
      if( retIndexSta )
        snprintf(clientStr, sizeof clientStr,
          "[%-2d]:IP%s", monopolizeComRecvIndex, getClientIP(monopolizeComRecvIndex));
      else
        strcpy(clientStr, "[NO client] All");
    }
    else{
      strcpy(clientStr, "All");
      runInfo.monopolizeComRecvIndex = NULL;
    }
    printfSend(NULL, "set COM --> TCP %s client\n", clientStr);
  }
  else if( strnicmp(token, "Send", strlen("Send") ) == 0){
    token = strtok(NULL, DECOLLATOR); // my or All 

    if( strnicmp(token, "my", strlen("my") ) == 0 ){
      static uint16_t monopolizeComSendIndex = 0;
      bool retIndexSta = getClientIndex(clientSocket, &monopolizeComSendIndex);
      runInfo.monopolizeComSendIndex = retIndexSta? &monopolizeComSendIndex:NULL;
      if( retIndexSta )
        snprintf(clientStr, sizeof clientStr,
          "[%-2d]:IP%s", monopolizeComSendIndex, getClientIP(monopolizeComSendIndex));
      else
        strcpy(clientStr, "[NO client] All");
    }
    else{
      strcpy(clientStr, "All");
      runInfo.monopolizeComSendIndex = NULL;
    }
    printfSend(NULL, "set TCP %s client --> COM \n", clientStr);

  }
  else
    printfSend(clientSocket, "Set Monopolize No Recv or send\n"); 
}

// 数据打印模式
static void cmdDataPrintMode(SOCKET *clientSocket, char* commandData)
{ (void)clientSocket;
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
  printfSend(NULL, "server Print Data: %d %s \n", 
      runInfo.serverPrintData, token);
}

// 执行一条系统命令
static void cmdRunSystemCmd(SOCKET *clientSocket, char* commandData)
{
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR); // 指令 
  int ret = system(token);
  printfSend(clientSocket, "execute system %s :%d\n", 
      ret == 0? "success": "failed", ret);
}


typedef struct { 
  
} connectServerInfoStart_t;

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
static void cmdServerConnect(SOCKET *clientSocket, char* commandData) 
{ 
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR); // 远端服务器地址（域名或IP） 或查询连接状态和断开连接
  if (token == NULL) {
    if( clientSocket )
      printfSend(clientSocket, "Usage: serverConnect,<hostname|ip>[,port]\n");
    return;
  }

  if (strnicmp(token, "disconnect", strlen("disconnect")) == 0) {
    DisconnectingServer();
    printfSend(clientSocket, "Disconnected from server\n");
    return;
  }
  char  serverAddress[256]; 
  memset(serverAddress, 0, sizeof serverAddress);
  strcpy(serverAddress, token); 
  token = strtok(NULL, DECOLLATOR); // 端口号 
  uint16_t port = atoi( token? token: "1000");
  if ( port == 0) {
    if( clientSocket )
      printfSend(clientSocket, "Invalid port number: %s\n", token);
    return;
  }
  
  // 先测试域名解析
  if( clientSocket )
    printfSend(clientSocket, "Connecting to %s:%d ...\n", serverAddress, port);
  ConnectToServer(serverAddress, port, connectServerResultCoback, clientSocket);
}


// 打开串口命令
static void cmdOpenSerialCom(SOCKET *clientSocket, char* commandData) 
{ 
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR); // 串口号

  if( token == NULL || strnicmp(token, "COM", strlen("COM") ) != 0 ) {
    printfSend(clientSocket, "The input is not :%s\n", token == NULL? "NULL":token);
    return;
  }

  char portName[10], *endptr;
  memset(portName, 0, sizeof portName);
  snprintf(portName, sizeof portName, "COM%d", (int)strtol(token + strlen("COM"), &endptr, 10) );

  if( strcmp(portName, ComPort->portName) == 0 ){ // 防止重复打开同一个串口浪费资源
    printfSend(clientSocket, "the %s has been turned on\n", portName);
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

  printfSend(clientSocket, "opening %s...\n", portName); 
  int8_t ret = OpenComPort(portName, baudRate, dataBits, stopBits, parity); 
  DWORD error = (ret != 0)? GetLastError(): 0;

  char *comParameter = getPrintf( "open [%s,%d,%d,%d,%d] %s! (%d:%ld)\n", 
      portName,baudRate,dataBits,stopBits,parity,
      ret == 0 ? "success":"failed", ret, error);
  printfSend(NULL, "%s", comParameter);
  SafePrintf("%s", comParameter);
}
