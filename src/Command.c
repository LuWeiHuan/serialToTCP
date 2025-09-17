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
#include "client.h"
#include "logPrint.h"
#include "Queue.h"

#include <stdio.h>
/*================== 本地数据类型   =========================================*/
/*================== 本地宏定义     =========================================*/
#define DECOLLATOR    ",\n"

/*================== 全局共享变量   =========================================*/
/*================== 本地常量声明   =========================================*/
/*================== 本地变量声明   =========================================*/
/*================== 本地函数声明   =========================================*/

/*================== 外部函数声明   =========================================*/
/*================== 外部变量声明   =========================================*/

/**
 * @brief 处理客户端发过来的指令
 * @param clientSocket  客户端套接字
 * @param clientIndex   客户端索引
 * @param command       命令字符串
 * @return
 * @attention
 */
void HandleClientCommand(SOCKET *clientSocket, uint8_t clientIndex, const char* command) 
{
    char *token;
    static char handleString[512];
    memset(handleString, 0, sizeof handleString);
    strcpy(handleString, command);


    if (strnicmp(command, "comlistVPID", strlen("comlistVPID")) == 0 || 
        strnicmp(command, "comlistID",   strlen("comlistID")) == 0  ) { 
      sendComPortsListToClient( clientSocket, true );
    }

    else if (strnicmp(command, "comlist", strlen("comlist")) == 0) { 
      sendComPortsListToClient( clientSocket, false );
    }

    else if (strnicmp(command, "runNewServer", strlen("runNewServer")) == 0) { 
      char path[MAX_PATH + 50];
      strcpy(path, "start \"\" \"");
      if (GetModuleFileName(NULL, path + strlen(path), MAX_PATH) == 0) { 
        printfSend(clientSocket, "Error: GetModuleFileName failed (%ld)\n",  GetLastError());
        return;
      }
      strcat(path, "\" ");
      token = strtok(handleString, DECOLLATOR);
      token = strtok(NULL, DECOLLATOR); // 传递参数
      if( token ){
        
        strcat(path, token);
      }
      
      system(path);
      SafePrintf("run New Server, Run Cmd: %s\n", path);
      printfSend(clientSocket, "run New Server, arg:%s\n", token?token:"NULL");
    }

    else if ( strnicmp(command, "PrintAllclientIP", strlen("PrintAllclientIP")) == 0) {
      getAllclientIPandIndexInfo(handleString, sizeof handleString);
      SafePrintf("All Client IP:\n%s\n", handleString);
      printfSend(clientSocket, "All Client IP:\n%s\n", handleString);
    }

    else if (strnicmp(command, "setCOMasyncSend", strlen("setCOMasyncSend")) == 0) {
      token = strtok(handleString, DECOLLATOR);
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

    else if (strnicmp(command, "setCOMasyncRecv", strlen("setCOMasyncRecv")) == 0) {
      token = strtok(handleString, DECOLLATOR);
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

    else if (strnicmp(command, "setRecvCOMdataTo", strlen("setRecvCOMdataTo")) == 0) {
      token = strtok(handleString, DECOLLATOR);
      token = strtok(NULL, DECOLLATOR);
      
      char clientStr[30];
      memset(clientStr, 0, sizeof clientStr);
      if( strnicmp(token, "my", strlen("my") ) == 0 ){
        runInfo.monopolizeSocket = clientSocket;
        runInfo.monopolizeIndex = clientIndex;
        snprintf(clientStr, sizeof clientStr,
           "(%d)%-16s", clientIndex, getClientIP(clientIndex));
      }
      else{
        strcpy(clientStr, "All");
        runInfo.monopolizeSocket = NULL;
      }
      printfSend(NULL, "set COM --> TCP %s client\n", clientStr); 
    }

    else if (strnicmp(command, "exit", strlen("exit")) == 0) {
      printfSend(NULL, "server ready exit\n" );
      exit(0);
    }

    else if (strnicmp(command, "serverPrintData", strlen("serverPrintData") ) == 0) {
        token = strtok(handleString, DECOLLATOR);
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

    else if (strnicmp(command, "system", strlen("system") ) == 0) {
        token = strtok(handleString, DECOLLATOR);
        token = strtok(NULL, DECOLLATOR); // 指令 
        int ret = system(token);
        printfSend(clientSocket, "execute system %s :%d\n", 
            ret == 0? "success": "failed", ret);
    }

    else if (strnicmp(command, "open", strlen("open")) == 0) {
        token = strtok(handleString, DECOLLATOR);
        token = strtok(NULL, DECOLLATOR); // 串口号

        if( token == NULL || strnicmp(token, "COM", strlen("COM") ) != 0 ) {
          printfSend(clientSocket, "The input is not :%s\n", token == NULL? "NULL":token);
          return;
        }

        char portName[10], *endptr;
        memset(portName, 0, sizeof portName);
        sprintf(portName, "COM%d", (int)strtol(token + strlen("COM"), &endptr, 10) );

        if( strcmp(portName, comPort.portName) == 0 ){ // 防止重复打开同一个串口浪费资源
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

        if ( comPort.isOpen ){
          char reason[50];
          memset(reason, 0, sizeof reason);
          snprintf(reason, sizeof reason, "Open New %s", comPort.portName);
          CloseComPort(reason);
        }
        
        printfSend(clientSocket, "opening %s...\n", portName); 
        int8_t ret = OpenComPort(portName, baudRate, dataBits, stopBits, parity); 
        DWORD error = (ret != 0)? GetLastError(): 0;

        memset(handleString, 0, sizeof handleString);
        sprintf(handleString, "open [%s,%d,%d,%d,%d] %s! (%d:%ld)\n", 
          portName,baudRate,dataBits,stopBits,parity,
          ret == 0 ? "success":"failed", ret, error);
        printfSend(NULL, "%s", handleString);
        SafePrintf("%s", handleString);
    }
    else
      printfSend(clientSocket, "Not Command:%s\n", command);
}
