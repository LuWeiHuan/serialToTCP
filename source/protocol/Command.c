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
#include "commonUtils.h"
#include "COM.h"
#include "clients.h"
#include "log.h"
#include "Queue.h"
#include "threadPool.h"
#include "ServerConnect.h"
#include "discovery.h"
#include "main.h"
#include "configSave.h"
#include "COMAutoReOpen.h"
#include "hostConnect.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef _WIN32
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#endif


/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
// 执行一条系统命令，想要执行需线先验证密码，默认密码在 mian.h 中定义
// 免验证密码即可执行的命令列表
static bool passwordVerify = false;
static const char* freeCommandsList[] = {
    "cls",
    "clear",
    // 后续可在此添加更多命令，例如：
    // "help",
    // "dir",
    // "ls",
    // "pwd",
    // "echo",
};

/*================== 本地变量声明    ========================================*/
/*================== 本地数据类型   =========================================*/
typedef void (*CmdHandlerFunc)(socket_t*, char*);

typedef struct {
    const char* cmdName;
    CmdHandlerFunc handler;
} CommandEntry;

typedef struct{
  char cmd[1024];
  socket_t *Socket;
  bool getResult;
}asyncExecuteSystemCommands_t;

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
static void cmdUpdatePassword(socket_t *, char*);
static void cmdVerifyPassword(socket_t*, char*);
static void cmdRunSystemCmd(socket_t*, char*);
static void cmdServerConnect(socket_t*, char*);
static void cmdOpenSerialCom(socket_t*, char*);
static void cmdDoNotConnectCOM2TCP(socket_t*, char*);
static void cmdSetLogPollCut(socket_t*, char*);
static void cmdKickAllClients(socket_t*, char*);
static void cmdsetCOMalignedNum(socket_t*, char*);
static void cmdAutoReOpenPort(socket_t*, char*);

/*================== 命令映射表     =========================================*/
static const CommandEntry cmdTable[] = {
  {"comlistVPID",                     cmdComlist},
  {"comlistID",                       cmdComlist},
  {"comlist",                         cmdComlist},
  {"runNewServer",                    cmdRunNewServer},
  {"PrintAllclientIP",                cmdPrintAllclientIP},
  {"setCOMasyncSend",                 cmdSetComAsyncSend},
  {"setCOMasyncRecv",                 cmdSetComAsyncRecv},
  {"setCOMalignedNum",                cmdsetCOMalignedNum},
  {"setCOMdata",                      cmdSetMonopolize},
  {"exit",                            cmdServerOverExit},
  {"serverPrintData",                 cmdDataPrintMode},
  {"SystemCommands",                  cmdRunSystemCmd},
  {"SystemCommandsResult",            cmdRunSystemCmd},
  {"ExecuteSystemCommandsResult",     cmdRunSystemCmd},
  {"SystemCommandsGetResult",         cmdRunSystemCmd},
  {"ExecuteSystemCommandsGetResult",  cmdRunSystemCmd},
  {"UpdatePassword",                  cmdUpdatePassword},
  {"VerifyPassword",                  cmdVerifyPassword},
  {"serverConnect",                   cmdServerConnect},
  {"OK! your index",                  cmdDoNotConnectCOM2TCP},
  {"Not Command",                     cmdDoNotConnectCOM2TCP},
  {"SetLogPollCut",                   cmdSetLogPollCut},
  {"KickAllClients",                  cmdKickAllClients},
  {"autoReOpenPort",                  cmdAutoReOpenPort},
  {"autoReOpenCOM",                   cmdAutoReOpenPort},
  {"open",                            cmdOpenSerialCom}
};

/**
 * @brief 处理客户端发过来的指令
 * @param Socket    客户端套接字，必须有！
 * @param command   命令字符串
 * @return
 * @attention
 */
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
  
  // 查找并执行命令
  for (uint8_t i = 0; i < sizeof cmdTable / sizeof cmdTable[0]; i++) 
    if (strnicmp(command, cmdTable[i].cmdName, strlen(cmdTable[i].cmdName)) == 0) {
        cmdTable[i].handler(Socket, handleCommand);
        return;
    }
  printfSend(Socket, "Not Command:%s\n", handleCommand);
}

// 让所有客户端下线
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
  //printfSend(Socket, "Sorry, it can't be achieved for the time being\n" );
}

// 收发日志是否滚动
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
  // 如果遇到小写id就改成大写ID
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
      exitInfo = getPrintf("Client [%-2d]IP: %s Ask For Server Ready Exit\n", 
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

// 运行一个新服务器程序
static void cmdRunNewServer(socket_t *Socket, char* commandData)
{
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR); // 传递参数

#ifdef _WIN32
  char path[MAX_PATH + 50], *fullCmd = path;
  strcpy(path, "start \"\" \"");
  if (GetModuleFileName(NULL, path + strlen(path), MAX_PATH) == 0) { 
    printfSend(Socket, "Error: Get Server File Name Path failed (%ld)\n",  GetLastError());
    return;
  }

  strcat(path, "\" ");

  if( token )
    strcat(path, token);
#else
  static char path[1024] = {0};
  if( path[0] == 0 ){
    ssize_t len = readlink("/proc/self/exe", path, sizeof path - 1);
    if (len == -1) {
      printfSend(Socket, "Error: Get Server File Name Path failed (%ld)\n",  GetLastError());
      return;
    }
    path[len] = '\0'; 
  }
    //getcwd(path, sizeof path);

  char fullCmd[2048];
  snprintf(fullCmd, sizeof fullCmd, "%s %s &", path, token ? token :"");
#endif

  int cmdret = system(fullCmd);
  SafePrintf("Run New Server Result:%d, Arg:%s, Run Cmd:%s\n", 
      cmdret, token ? token:"NULL", fullCmd);
  printfSend(Socket, "Run New Server Result:%d, Arg:%s, Run Cmd:%s\n", 
      cmdret, token ? token:"NULL", fullCmd);
}

// 设置客户端数据异步发给串口
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

// 设置收到串口数据异步发给客户端
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

// 设置独占信息
static void cmdSetMonopolize(socket_t *Socket, char* commandData)
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
static void cmdDataPrintMode(socket_t *Socket, char* commandData)
{ 
  (void)Socket;

  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR); // 显示模式
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



static bool isPasswordFreeCommand(const char* cmd) 
{
  if (cmd == NULL) 
    return false;
  while (isspace((uint8_t)*cmd)) cmd++;   // 跳过开头的空格
  
  for (uint8_t i = 0; i < sizeof(freeCommandsList) / sizeof(freeCommandsList[0]); i++) {
    const char* freeCmd = freeCommandsList[i];
    size_t freeCmdLen = strlen(freeCmd);
    
    // 比较命令名（不区分大小写）
    if (strnicmp(cmd, freeCmd, freeCmdLen) == 0) {
        // 确保命令后面是结束符、空格或参数分隔符
        char nextChar = cmd[freeCmdLen];
        if (nextChar == '\0' || isspace((uint8_t)nextChar))
            return true;
    }
  }
  return false;
}

static void trueExecuteSystemCommands(void *arg)
{
  //ThreadTask *task = ((ThreadPoolArgWrapper*)arg)->threadTask; 
  asyncExecuteSystemCommands_t *aesc = ((ThreadPoolArgWrapper*)arg)->arg;
  int ret;
  static char executeResult[2048];

  if( aesc->getResult == false )
    ret = system(aesc->cmd);
  else{
    memset(executeResult, 0, sizeof executeResult);// 清空输出缓冲区
    strcpy(executeResult, ", Result:\n");
    uint8_t len = strlen(executeResult);
    ret = (int)!executeCommand(aesc->cmd, executeResult + len, sizeof executeResult - len);
  }
  printfSend(aesc->Socket, "Execute [%s] Command %s(%d)%s\n", 
    aesc->cmd, ret == 0 ? "Success" : "Failed", ret, aesc->getResult? executeResult:".");

  aesc->Socket = NULL;
}

// 执行一条系统命令，想要执行需先验证密码，
// 但 freeCommandsList 列表里的命令可以免密码执行
static void cmdRunSystemCmd(socket_t *Socket, char* commandData)
{
  static asyncExecuteSystemCommands_t aesc = {.Socket = NULL};

  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
  
  if (token == NULL) {
      printfSend(Socket, "No command specified\n");
      return;
  }
  
  // 检查是否为免验证密码的命令
  bool isFreeCommand = isPasswordFreeCommand(token);
  
  // 需要密码验证的命令
  if (passwordVerify == false && isFreeCommand == false) {
      printfSend(Socket, "Please Verify Password\n");
      return;
  }

  if( aesc.Socket != NULL ){
    printfSend(Socket, "Please Wait Last Command Executing\n");
    return;
  }

  aesc.Socket = Socket;
  aesc.getResult = stristr( commandData, "Result") == NULL? false:true;
  memset(aesc.cmd, 0, sizeof aesc.cmd);
  memcpy(aesc.cmd, token, strlen(token) < sizeof aesc.cmd? strlen(token) : sizeof aesc.cmd - 1);

  static ThreadTask  asyncExecuteSystemCommandsTime;
  threadTaskInit(&asyncExecuteSystemCommandsTime, trueExecuteSystemCommands, &aesc, 0, 0);
  threadTtaskStart(gThreadPool, &asyncExecuteSystemCommandsTime);
}

static void cmdVerifyPassword(socket_t *Socket, char* commandData)
{
  #define MaxVerifyNum      30
  static uint8_t verifyNum = MaxVerifyNum;

  if( verifyNum <= 0 ){
    printfSend(Socket, "Password Verify Number Zero\n");
    return;
  }
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
  uint8_t steLen = token == NULL? 0 : strlen(token);
  if( steLen != sizeof saveInfo.passwordMD5 ){
    printfSend(Socket, "Password MD5 Value unequal %d/%d\n", 
      steLen, sizeof saveInfo.passwordMD5);
    return;
  }
  
  passwordVerify = strncmp(token, saveInfo.passwordMD5, sizeof saveInfo.passwordMD5) == 0 ? true:false;
  verifyNum = passwordVerify? MaxVerifyNum:verifyNum - 1;

  char *setInfo = getPrintf(", Verify Number %d/%d\n", verifyNum, MaxVerifyNum);
  printfSend(Socket, "Password Verify %s%s\n", 
    passwordVerify? "Success":"Failed", passwordVerify? " ":setInfo);
}

static void cmdUpdatePassword(socket_t *Socket, char* commandData)
{
  if( passwordVerify == false ){
    printfSend(Socket, "Please Verify Password\n");
    return;
  }

  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);

  uint8_t steLen = token == NULL? 0 : strlen(token);
  if( steLen != sizeof saveInfo.passwordMD5 ){
    printfSend(Socket, "Password MD5 Value unequal %d/%d\n", 
      steLen, sizeof saveInfo.passwordMD5);
    return;
  }

  if( isValidHexRange(token) == false ){
    printfSend(Socket, "Password MD5 Value is not a valid hexadecimal range.\n");
    return;
  }

  bool retCmp = strcmp(token, saveInfo.passwordMD5) == 0 ? true:false;
  strcpy(saveInfo.passwordMD5, token);
  if( retCmp == false )
    saveConfig();
  printfSend(Socket, "Update Password %s! Value:%s\n",
    retCmp? "OK":"Done", saveInfo.passwordMD5);
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
  
  char resolvedIP[ INET6_ADDRSTRLEN ] = {0}, residueTimeMsString[50] = {0};

  int getErr = 0; 
  int8_t resolveRet = resolveHostDomainName(hsot, resolvedIP, sizeof resolvedIP, &getErr, false);
  if( resolveRet == -1 )
    SafePrintf("Failed to resolve hsot name: %s, code:%d\n", hsot, getErr);
  
  if( resolveRet == -2 )
    SafePrintf("No valid IP address found for: %s, code:%d\n", hsot, getErr);
  
  snprintf(residueTimeMsString, sizeof residueTimeMsString,
    "Please Wait %d/%d ms", residueTimeMs, CONNECT_TIMEOUT_MS);

  int identifyRet = hostStringIdentify(hsot, false);

  socket_t *replySocket = (socket_t*)arg;
  if( replySocket )
    printfSend(replySocket, "Server [%s] [%s,%d] Connect%s %s\n", 
              identifyRet==3? hsot:"IP",
              identifyRet==3? resolvedIP:hsot, port,
              stateStrings[State], State==1? residueTimeMsString:" ");  
}

static void cmdServerConnect(socket_t *Socket, char* commandData) 
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
  token = strtok(NULL, DECOLLATOR);
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
static void cmdOpenSerialCom(socket_t *Socket, char* commandData) 
{
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);   // 串口号

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
  token = strtok(NULL, DECOLLATOR); // 波特率
  if (token) baudRate = atoi(token);

  token = strtok(NULL, DECOLLATOR); // 数据位
  if (token) dataBits = atoi(token);

  token = strtok(NULL, DECOLLATOR); // 停止位
  if (token) stopBits = atoi(token);

  token = strtok(NULL, DECOLLATOR); // 校验位
  if (token) parity = atoi(token);

  if ( getComIsOpen() ){ 
    char *reason = getPrintf("Open New %s", portName);
    CloseComPort(reason);
  }

  printfSend(Socket, "opening %s...\n", portName);
  int8_t ret = OpenComPort(portName, baudRate, dataBits, stopBits, parity);
  COM_AutoReOpen_SaveCurrentPort(ret == 0? portName :NULL, baudRate, dataBits, stopBits, parity);

  DWORD error = (ret != 0)? GetLastError(): 0;
  
  const char *comParameter = getPrintf( "open [%s,%d,%d,%d,%d] %s! (%d:%ld)\n", 
      portName,baudRate,dataBits,stopBits,parity,
      ret == 0 ? "Success":"Failed", ret, error);
  
  broadcastSendHandleResult(Socket, comParameter);
  SafePrintf("%s", comParameter);
}

// 开启自动打开串口功能
static void cmdAutoReOpenPort(socket_t *Socket, char* commandData)
{
  (void)Socket; 
  char *token = strtok(commandData, DECOLLATOR);
  token = strtok(NULL, DECOLLATOR);
  uint16_t num = token==NULL? 0:atoi(token);

  const char *setInfo = getPrintf("%s Auto ReOpen COM Port\n", num? "Open":"Close");
  broadcastSendHandleResult(Socket, setInfo);

  COM_AutoReOpen(num? true:false);
}

// 广播发送处理结果
static void broadcastSendHandleResult(socket_t *Socket, const char *info)
{ 
  uint16_t sendLen = strlen(info);
  if( getDiscoverySocket() == *Socket )
    printfSend(Socket, info, sendLen);
  printfSend(NULL, info, sendLen);
}
