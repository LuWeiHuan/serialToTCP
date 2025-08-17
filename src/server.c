
/*
实现将串口数据转到TCP收发的能力
环境Win平台，使用C语言编写一个服务端程序，接受任何网段连接该服务器

主要实现功能如下：
1. 服务端监听的串口号默认为9000，如果被占用自动加1，服务端使用一个宏来控制能接受多少个客户端连接。
2. 限制最大客户端连接数量，新的客户端连接后，踢掉最早连接的客户端，使用系统时间精确到ms的方法作为判断依据哪个是最早的，并告诉被踢下线的客户端他被踢下线了
3. 使用setsockopt函数 禁用Nagle算法。使用非阻塞式监听客户端连接，超时时间为1秒。
4. 用“crtlInfo:”字符串用于客户端控制服务器动作的头标识，结尾加“\n”，这些约定的内容用来作为控制信息，不发送给串口。
5. 客户端连接服务端后，服务端创建一个独立的线程接收客户端的数据，完成后向客户端发送“crtlInfo:OK! your indes x\n”
6. 获取Win系统下有效可用的串口列表，让客户端通过发送“crtlInfo:comliset”字符串后，服务端返回可用串口列表。
7. 比如串口列表里有 COM2和COM5，客户端通过发送 “crtlInfo:open,串口号,波特率,数据位,停止位,校验位\n” 来打开串口并设置相关参数，其中，串口号必填项，后面可以在不填入情况下，使用默认参数，默认波特率115200，数据位8位，停止位1位，无校验位。不管打开串口成功或失败，都将结果代码发送给客户端，格式为“crtlInfo: open [串口号,波特率,数据位,停止位,校验位] 结果 异常代码\n”
8. 可以的话用独立线程接收串口数据，接收到串口数据发给所有客户端（数据内容有不光有字符串，还有一般数据），任何客户端发来的数据直接发给串口。
9. 一个服务端只能打开一个串口。相应的，任何客户端也可以发送打开新的串口，但是要关闭之前打开的串口。
10. 串口可能出现热插拔或者异常关闭的问题，将这些信息发给所有客户端，格式为“crtlInfo:串口号异常关闭，结果代码\n”
11. 串口通信异步方式可能无法正常使用，暂且用同步方式。
12. 让程序支持在运行时通过传入参数，来修改指定端口号，比如输入 -p5000 就指定监听5000端口号，-p参数不区分大小写，规避前1024，返回参数为端口号。
如果该端口号被占用就自动加1，尝试10次。可能是启用IPv6和IPv4问题，第二次运行还是同一个端口号切不支持IPv6，只有第三次运行才会是新的端口号
13. USB设备插入或拔出通知所有客户端
最后给出使用MakeFile管理编译。
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <winsock2.h>
#include <windows.h>
#include <tchar.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#include <ws2tcpip.h>
#include <inttypes.h>
#include <time.h>

#define MAX_CLIENTS   3
#define DEFAULT_PORT  9000
#define BUFFER_SIZE   1024*10
#define CTRL_HEADER   "ctrlInfo:"
#define DECOLLATOR    ",\n"




typedef struct {
    SOCKET socket;
    HANDLE hThread;
    DWORD threadId;
    uint8_t index;
    __int64 connectTime;    // 连接时间（毫秒级时间戳）
} ClientInfo_t;

typedef struct {
    HANDLE hCom;
    BOOL isOpen;
    char portName[10];
    DCB dcb;
    HANDLE hThread;
    DWORD threadId;
} ComPortInfo_t;

typedef struct {
    uint8_t serverPrintData; // 0，不显示，1为字符串显示，2为Hex显示
    uint8_t clientCount;
    SOCKET *monopolizeSoclet; // 独占串口收到的数据
    int8_t  monopolizeIndex;
    uint16_t port;
    time_t startTime;
    uint32_t linkCount;
} runInfo_t;

runInfo_t  runInfo = {
  .serverPrintData = 0,
  .clientCount = 0,
  .monopolizeSoclet = NULL,
  .monopolizeIndex = 0,
  .port = DEFAULT_PORT,
  .startTime = 0,
  .linkCount = 0,
};

ClientInfo_t clients[MAX_CLIENTS];
ComPortInfo_t comPort = { INVALID_HANDLE_VALUE, FALSE, "", {0}, NULL, 0 };
CRITICAL_SECTION csClient, csComPort;
SOCKET serverSocket = INVALID_SOCKET;
HANDLE hComThread = NULL;
CRITICAL_SECTION g_log_cs;


int SendToAllClients(const char* message, int len);
void CloseClient(uint8_t index, char *func);
void HandleClientCommand(SOCKET clientSocket, uint8_t clientIndex, const char* command);
void ListComPorts(SOCKET *clientSocket);
int8_t OpenComPort(const char* portName, uint32_t baudRate, uint8_t dataBits, uint8_t stopBits, uint8_t parity);
void CloseComPort();
DWORD WINAPI ComRecvDataThread(LPVOID lpParam);
int printfSend(SOCKET *Socket, const char *fmt, ...);
void printf_hex8(const uint8_t *pdata, uint16_t len, uint8_t numEnter, uint8_t endEnter);
char *getCurrentTime(void) ;
void updataConsoleTitle(char *threadName, DWORD theradID);
__int64 GetCurrentTimeMillis(void);
char *getSendRecvDirectionStr(char *direct, uint8_t index);
void StopDeviceChangeMonitor(void);
void StartDeviceChangeMonitor(void);
int ParsePortParameter(int argc, char const* argv[], int defaultPort);
int SafePrintf(const char* format, ...) __attribute__((format(printf, 1, 2)));

void HandleClientCommand(SOCKET clientSocket, uint8_t clientIndex, const char* command) 
{
    char *token, temp[100];
    strcpy(temp, command);

    if (strncmp(command, "comlist", strlen("comlist")) == 0) {
      ListComPorts(&clientSocket);
    }
    else if (strncmp(command, "setRecvCOMdataTo", strlen("setRecvCOMdataTo")) == 0) {
      token = strtok(temp, DECOLLATOR);
      token = strtok(NULL, DECOLLATOR);
      
      char clientStr[5];
      memset(clientStr, 0, sizeof clientStr);
      if( strncmp(token, "my", strlen("my") ) == 0 ){
        runInfo.monopolizeSoclet = &clients[ clientIndex ].socket;
        runInfo.monopolizeIndex = clientIndex;
        sprintf(clientStr, "%d", clientIndex);
      }
      else{
        strcpy(clientStr, "All");
        runInfo.monopolizeSoclet = NULL;
      }
      printfSend(NULL, "%sset COM --> TCP %s client\n", CTRL_HEADER, clientStr); 
    }
    else if (strncmp(command, "exit", strlen("exit")) == 0) {
      printfSend(&clientSocket, "%sserver ready exit\n", CTRL_HEADER );
      exit(0);
    }
    else if (strncmp(command, "serverPrintData", strlen("serverPrintData") ) == 0) {
        token = strtok(temp, DECOLLATOR);
        token = strtok(NULL, DECOLLATOR); // 显示模式
        runInfo.serverPrintData = 0;
        if( strncmp(token, "NULL", strlen("NULL") ) == 0 )
          runInfo.serverPrintData = 0;
        if( strncmp(token, "ASCII", strlen("ASCII")) == 0 )
          runInfo.serverPrintData = 1;
        if( strncmp(token, "HEX", strlen("HEX")) == 0 )
          runInfo.serverPrintData = 2;
        printfSend(NULL, "%sserver Print Data: %d %s \n", 
          CTRL_HEADER, runInfo.serverPrintData, token);
    }
    else if (strncmp(command, "system", strlen("system") ) == 0) {
        // 解析命令
        token = strtok(temp, DECOLLATOR);
        token = strtok(NULL, DECOLLATOR); // 指令 
        int ret = system(token);
        printfSend(&clientSocket, "%sexecute system %s :%d\n", 
            CTRL_HEADER, ret == 0? "success": "failed", ret);
    }
    else if (strncmp(command, "open", strlen("open")) == 0) {
        // 解析命令
        token = strtok(temp, DECOLLATOR);
        token = strtok(NULL, DECOLLATOR); // 串口号

        if( token == NULL || strncasecmp(token, "COM", strlen("COM") ) != 0 ) {
          printfSend(&clientSocket, "%sThe input is not COM\n", CTRL_HEADER);
          return;
        }

        char portName[10];
        memset(portName, 0, sizeof portName);
        strcpy(portName, "COM");  // 串口号
        for(uint8_t i = strlen(portName); i < strlen(token); i++)
          portName[i] = ( '0' <= token[i] && token[i] <= '9' ) ? token[i] : 0;

        if( strcmp(portName, comPort.portName) == 0 ){ // 防止重复打开同一个串口浪费资源
          printfSend(&clientSocket, "%sthe %s has been turned on\n", CTRL_HEADER, portName);
          return;
        }

        uint32_t baudRate = 115200;
        uint8_t dataBits = 8, stopBits = 1, parity = 0;
        token = strtok(NULL, DECOLLATOR); // 波特率
        if (token) baudRate = atoi(token);
        
        token = strtok(NULL, DECOLLATOR); // 数据位
        if (token) dataBits = atoi(token);
        
        token = strtok(NULL, DECOLLATOR); // 停止位
        if (token) stopBits = atoi(token);
        
        token = strtok(NULL, DECOLLATOR); // 校验位
        if (token) parity = atoi(token);

        CloseComPort();
        DWORD error = 0;
        int8_t ret = OpenComPort(portName, baudRate, dataBits, stopBits, parity);
        if( ret != 0 )
          error = GetLastError();

        printfSend(NULL, "%sopen [%s,%d,%d,%d,%d] %s %d %ld\n", CTRL_HEADER, 
          portName,baudRate,dataBits,stopBits,parity,
          ret == 0 ? "success":"failed", ret, error );
        SafePrintf("open [%s,%d,%d,%d,%d] %s %d %ld\n", 
          portName,baudRate,dataBits,stopBits,parity,
            ret == 0 ? "success":"failed", ret, error);
    }
}


void ListComPorts(SOCKET *clientSocket) 
{
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
      printfSend(clientSocket, "%sFailed to get COM port list\n", CTRL_HEADER); 
      return;
    }

    bool first = true;  
    char response[1024] = CTRL_HEADER "COM Ports: ";

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); i++) { 
        BYTE buffer[256];
        DWORD dataType, bufferSize = sizeof buffer;
        WINBOOL ret = SetupDiGetDeviceRegistryPropertyA(hDevInfo, &deviceInfoData, 
            SPDRP_FRIENDLYNAME, &dataType, buffer, bufferSize, &bufferSize);
        if ( ret == FALSE ) 
          continue;

        char* portName = strstr((char*)buffer, "COM");
        if (portName) {
            char* end = strchr(portName, ')');
            if (end) *end = '\0';
            if (!first) 
                strcat(response, ", ");
            strcat(response, portName);
            first = false;
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);

    if (first)
        strcat(response, "No COM ports found"); 
 
    printfSend(clientSocket, "%s\n", response);
}

int8_t OpenComPort(const char* portName, uint32_t baudRate, 
  uint8_t dataBits, uint8_t stopBits, uint8_t parity)
{
    char fullPortName[20];
    sprintf(fullPortName, "\\\\.\\%s", portName);

    EnterCriticalSection(&csComPort);
    if ( comPort.isOpen )
        CloseComPort();

    comPort.hCom = CreateFileA(fullPortName, 
                              GENERIC_READ | GENERIC_WRITE,
                              0,
                              NULL,
                              OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, // 异步模式，同步模式写0
                              NULL);

    if (comPort.hCom == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&csComPort);
        return -1;
    }

    // 设置串口参数
    memset(&comPort.dcb, 0, sizeof(DCB));
    comPort.dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(comPort.hCom, &comPort.dcb)) {
        CloseHandle(comPort.hCom);
        comPort.hCom = INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&csComPort);
        return -2;
    }

    // 确保 DCB 正确配置
    comPort.dcb.BaudRate = baudRate;          // 波特率（如 9600, 115200）
    comPort.dcb.ByteSize = (BYTE)dataBits;    // 数据位（5,6,7,8）
    comPort.dcb.StopBits = stopBits == 1 ? ONESTOPBIT : TWOSTOPBITS;  // 停止位（1 或 2）
    comPort.dcb.Parity = (BYTE)parity;        // 校验位（0=NONE, 1=ODD, 2=EVEN, 3=MARK, 4=SPACE）
    
    // 必须设置的标志位
    comPort.dcb.fBinary = TRUE;               // 必须为 TRUE（Windows 串口仅支持二进制模式）
    comPort.dcb.fOutxCtsFlow = FALSE;         // 禁用 CTS 流控
    comPort.dcb.fOutxDsrFlow = FALSE;         // 禁用 DSR 流控
    comPort.dcb.fDtrControl = DTR_CONTROL_ENABLE;  // DTR 信号控制
    comPort.dcb.fRtsControl = RTS_CONTROL_ENABLE;  // RTS 信号控制
    comPort.dcb.fOutX = FALSE;                // 禁用 XON/XOFF 输出流控
    comPort.dcb.fInX = FALSE;                 // 禁用 XON/XOFF 输入流控
    comPort.dcb.fErrorChar = FALSE;           // 禁用错误替换字符
    comPort.dcb.fNull = FALSE;                // 禁止丢弃 NULL 字节
    comPort.dcb.fAbortOnError = FALSE;        // 发生错误时不终止读写操作

    if (!SetCommState(comPort.hCom, &comPort.dcb)) {
        CloseHandle(comPort.hCom);
        comPort.hCom = INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&csComPort);
        return -3;
    }

    // 设置超时
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(comPort.hCom, &timeouts);

    //strcpy(comPort.portName, portName, sizeof(comPort.portName) - 1);
    memset(comPort.portName, 0, sizeof comPort.portName);
    strcpy(comPort.portName, portName);
    comPort.isOpen = TRUE;
    
    // 创建串口读取线程
    comPort.hThread = CreateThread(NULL, 0, ComRecvDataThread, NULL, 0, &comPort.threadId);
    if (comPort.hThread == NULL) {
        CloseHandle(comPort.hCom);
        comPort.hCom = INVALID_HANDLE_VALUE;
        comPort.isOpen = FALSE;
        LeaveCriticalSection(&csComPort);
        return -4;
    }

    LeaveCriticalSection(&csComPort);
    return 0;
}

void CloseComPort(void) 
{
  if (comPort.isOpen == FALSE) 
    return;

  comPort.isOpen = FALSE;
  WINBOOL closeComRet = FALSE, closeThreadRet = FALSE ;

  EnterCriticalSection(&csComPort);
  if (comPort.hThread) {
    // 等待线程退出
    WaitForSingleObject(comPort.hThread, 1000);
    closeThreadRet = CloseHandle(comPort.hThread);
    comPort.hThread = NULL;
  }
  
  if (comPort.hCom != INVALID_HANDLE_VALUE) {
      closeComRet = CloseHandle(comPort.hCom);
      comPort.hCom = INVALID_HANDLE_VALUE;
  }

  printfSend(NULL, "%sclosed %s %s, thread exit %s.\n", CTRL_HEADER, comPort.portName, 
     closeComRet == FALSE? "failed":"success", closeThreadRet == FALSE? "failed":"success");
  memset(comPort.portName, 0, sizeof comPort.portName);
  
  LeaveCriticalSection(&csComPort);
}

DWORD WINAPI ComRecvDataThread(LPVOID lpParam) {
    if(lpParam){}
    char comRecvBuffer[BUFFER_SIZE];
    DWORD bytesRead;
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    WINBOOL readRet;
    DWORD lastUpdateTime = 0, currentTime = 0;
    const DWORD updateInterval = 1500; // 1.5秒更新一次

    while ( comPort.isOpen ) {
        // 重置重叠结构
        memset(&overlapped, 0, sizeof(OVERLAPPED));
        overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        
        // 发起异步读取
        readRet = ReadFile(comPort.hCom, comRecvBuffer, (sizeof comRecvBuffer) - 1, &bytesRead, &overlapped);
        
        if (!readRet) {
            DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                // 等待读取完成或超时
                DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 1000);
                if (waitResult == WAIT_TIMEOUT) {
                    // 超时处理 - 按间隔更新状态
                    updataConsoleTitle("COM: Waiting", GetCurrentThreadId());
                    CloseHandle(overlapped.hEvent);
                    continue;
                }
                else if (waitResult == WAIT_OBJECT_0) {
                    // 读取完成
                    if (!GetOverlappedResult(comPort.hCom, &overlapped, &bytesRead, FALSE)) {
                        error = GetLastError();
                        if (error != ERROR_OPERATION_ABORTED) {
                            printfSend(NULL, "%s%s error %ld\n", CTRL_HEADER, comPort.portName, error);
                            CloseComPort();
                            break;
                        }
                    }
                }
            }
            else if (error != ERROR_OPERATION_ABORTED) { 
              printfSend(NULL, "%s%s error %ld\n", CTRL_HEADER, comPort.portName, error);
              CloseComPort();
              break;
            }
        }

        // 处理接收到的数据或空读取
        if (bytesRead == 0) {
            CloseHandle(overlapped.hEvent);
            
            // 按间隔更新状态
            currentTime = GetTickCount();
            if (currentTime - lastUpdateTime >= updateInterval) {
                updataConsoleTitle("COM", GetCurrentThreadId());
                lastUpdateTime = currentTime;
            }
            continue;
        }
 
        // 处理接收到的数据
        int sendRet = 0;
        if (runInfo.monopolizeSoclet != NULL) {
            // 发送给独立客户端
            sendRet = send(*runInfo.monopolizeSoclet, comRecvBuffer, bytesRead, 0);
            if (sendRet <= 0) {
                SafePrintf("send monopolize clients failed ! code: %d\n", sendRet);
                runInfo.monopolizeSoclet = NULL;
                sendRet = SendToAllClients(comRecvBuffer, bytesRead);
            }
        }
        else 
            sendRet = SendToAllClients(comRecvBuffer, bytesRead);
        

        // 打印日志
        char *Direct = getSendRecvDirectionStr("[COM --> TCP]", 0);
        char *timeStr = getCurrentTime();
        timeStr[strlen(timeStr)] = ' ';
        static uint64_t sendCount = 0;

        SafePrintf("%s%6I64d [%s]  %-6d/%-6ld Byte (%s : %ld)%s\n", 
            timeStr, ++sendCount, Direct, sendRet, bytesRead, 
            sendRet == (int)bytesRead ? "OK":"Fail", bytesRead - sendRet,
            runInfo.serverPrintData != 0? " data:":" ");
            
        if (runInfo.serverPrintData != 0) { 
            if (runInfo.serverPrintData == 1)
                puts(comRecvBuffer);
            if (runInfo.serverPrintData == 2)
                printf_hex8((uint8_t*)comRecvBuffer, bytesRead, 40, 2);
        }

        CloseHandle(overlapped.hEvent);
    }

    CloseHandle(overlapped.hEvent); 
    return 0;
}

int SendToAllClients(const char* message, int len) 
{
    int ret = 0;
    EnterCriticalSection(&csClient);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket != INVALID_SOCKET) 
          ret = send(clients[i].socket, message, len, 0);
    }
    LeaveCriticalSection(&csClient);
    return ret;
}

DWORD WINAPI ClientRecvDataThread(LPVOID lpParam) 
{
    if (lpParam == NULL) { 
        SafePrintf("client thread not Client info introduction\n");
        return -1;
    }

    ClientInfo_t *clientInfo = (ClientInfo_t*)lpParam;
    char tcpRecvBuffer[BUFFER_SIZE];
    int bytesReceived = 0, ret;
    fd_set readSet;
    struct timeval timeout;
    uint64_t sendCount = 0;
    OVERLAPPED writeOverlapped = {0};

    // 设置socket为非阻塞模式
    u_long mode = 1; // 1表示非阻塞，0表示阻塞
    if (ioctlsocket(clientInfo->socket, FIONBIO, &mode) != 0) {
        SafePrintf("Set non-blocking failed for client %d, error: %d\n", 
               clientInfo->index, WSAGetLastError());
        CloseClient(clientInfo->index, "设置非阻塞失败");
        return -1;
    }

    char threadNameStr[20];

    // 发送连接成功消息  
    printfSend(&clientInfo->socket, "%sOK! your index %d\n", CTRL_HEADER, clientInfo->index);

    while (true) {
        // 检查客户端socket是否仍然有效
        if (clientInfo->socket == INVALID_SOCKET)
            break;

        FD_ZERO(&readSet);
        FD_SET(clientInfo->socket, &readSet);

        // 设置超时时间为100毫秒
        timeout.tv_sec = 1;
        timeout.tv_usec = 0; // 100毫秒

        ret = select(0, &readSet, NULL, NULL, &timeout);
        
        if (ret == SOCKET_ERROR) {
            SafePrintf("select error for client %d, error: %d\n",
                   clientInfo->index, WSAGetLastError());
            break;
        }
        else if (ret == 0) {
            // 超时，没有数据可读，继续循环
            memset(threadNameStr, 0, sizeof threadNameStr );
            sprintf(threadNameStr, "client %d ", clientInfo->index);
            updataConsoleTitle(threadNameStr, GetCurrentThreadId());
            continue;
        }

        // 有数据可读
        bytesReceived = recv(clientInfo->socket, tcpRecvBuffer, sizeof(tcpRecvBuffer) - 1, 0);
        
        if (bytesReceived == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                // 非阻塞模式下没有数据是正常情况
                continue;
            }
            else {
                // 其他错误，断开连接
                SafePrintf("recv error for client %d, error: %d\n", 
                       clientInfo->index, error);
                break;
            }
        }
        else if (bytesReceived == 0) {
            // 客户端正常关闭连接
            SafePrintf("client %d gracefully disconnected\n", clientInfo->index);
            break;
        }

        // 正常接收到数据
        tcpRecvBuffer[bytesReceived] = '\0';

        // 检查是否是控制命令
        if (strncmp(tcpRecvBuffer, CTRL_HEADER, strlen(CTRL_HEADER)) == 0) {
            HandleClientCommand(clientInfo->socket, clientInfo->index, 
                              tcpRecvBuffer + strlen(CTRL_HEADER));
            continue;
        }
        
        // 判断串口是否已经打开
        if (comPort.isOpen == FALSE) {
            printfSend(&clientInfo->socket, "%sCOM not open !\n", CTRL_HEADER);
            continue;
        }

        // 普通数据，发送到串口 
        DWORD bytesWritten, error = 0;
        EnterCriticalSection(&csComPort);

        writeOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL); 
        WINBOOL WriteRet = WriteFile(comPort.hCom, tcpRecvBuffer, bytesReceived, &bytesWritten, &writeOverlapped);
        if (!WriteRet) {
            error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                // 等待写入完成
                if (!GetOverlappedResult(comPort.hCom, &writeOverlapped, &bytesWritten, TRUE)) {
                    // 这里还是真正的错误
                    error = GetLastError();
                    printfSend(&clientInfo->socket, "%sCOM write failed: %d\n", CTRL_HEADER, error);
                }
                else{
                  error = 0;
                  WriteRet = TRUE;
                }
            } 
            else {
                printfSend(&clientInfo->socket, "%sCOM write error: %d\n", CTRL_HEADER, error);
            }
        }
        CloseHandle(writeOverlapped.hEvent);

        LeaveCriticalSection(&csComPort);
        
        if( error == 22 ) // 设备可能已经拔出
          CloseComPort(); 

        char *Direct = getSendRecvDirectionStr("[TCP --> COM]", clientInfo->index);
        char *timeStr = getCurrentTime();
        timeStr[strlen(timeStr)] = ' ';
 
        SafePrintf("%s%6I64d [%s]  %-6ld/%-6d Byte (%s : %ld)%s\n",  timeStr, ++sendCount, Direct,
               bytesWritten, bytesReceived, WriteRet ? "OK" : "Fail", error,
               runInfo.serverPrintData != 0 ? " data:" : " ");
               
        if (runInfo.serverPrintData != 0) {
            if (runInfo.serverPrintData == 1) {
                SafePrintf("%s", tcpRecvBuffer);
            }
            if (runInfo.serverPrintData == 2) {
                printf_hex8((uint8_t*)tcpRecvBuffer, bytesReceived, 40, 2);
            }
        }
    }

    SafePrintf("client %d disconnected, last recv code: %d\n", 
           clientInfo->index, bytesReceived);
 
    CloseClient(clientInfo->index, "线程关闭");
 
    return 0;
}

void addClient(uint8_t index, SOCKET socket)
{
    // 添加新客户端并记录精确连接时间
    clients[index].index = index;
    clients[index].socket = socket;
    clients[index].connectTime = GetCurrentTimeMillis();  // 记录精确到毫秒的连接时间
    clients[index].hThread = CreateThread(NULL, 0, ClientRecvDataThread, 
      &clients[index], 0, &clients[index].threadId);
    
    if( clients[index].hThread != NULL ){
      runInfo.clientCount++;
      runInfo.linkCount++;
      SafePrintf("The %d(index %d) Client connected at %I64d ms, Total clients: %d\n", 
        runInfo.clientCount, clients[index].index, clients[index].connectTime, runInfo.linkCount);
    }
}


void CloseClient(uint8_t index, char *reason) 
{
    if (index >= MAX_CLIENTS || clients[index].socket == INVALID_SOCKET) {
        SafePrintf("Invalid client index %d or socket already closed\n", index);
        return;
    }

    EnterCriticalSection(&csClient); 

    // 关闭套接字
    closesocket(clients[index].socket);
    clients[index].socket = INVALID_SOCKET;

    // 关闭线程
    if (clients[index].hThread) {
        WaitForSingleObject(clients[index].hThread, 1000);
        CloseHandle(clients[index].hThread);
        clients[index].hThread = NULL;
    }

    if (runInfo.clientCount > 0)
        runInfo.clientCount--;

    SafePrintf("Closed client %d, reason: %s\n", index, reason);
    LeaveCriticalSection(&csClient);
}


int FindAvailablePort(int startPort) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return -1;
    }

    int port = startPort;
    while (port < startPort + 100) {
        SOCKET testSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (testSocket == INVALID_SOCKET) {
            WSACleanup();
            return -1;
        }

        struct sockaddr_in service;
        service.sin_family = AF_INET;
        service.sin_addr.s_addr = INADDR_ANY;
        service.sin_port = htons(port);

        if (bind(testSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
            closesocket(testSocket);
            port++;
        } else {
            closesocket(testSocket);
            WSACleanup();
            return port;
        }
    }

    WSACleanup();
    return -1;
}

/*=============================================================================
 功   能：以16进制打印输出单字节数组
 参   数：pdata			-->字节数组
					len				-->数组长度
					numEnter 	-->显示多少个字节换行，0则不换行
					endEnter	-->打印结束后进行多少次换行
 返   回：无
 描   述：无
=============================================================================*/
void printf_hex8(const uint8_t *pdata, uint16_t len, uint8_t numEnter, uint8_t endEnter)
{
  EnterCriticalSection(&g_log_cs);
 
	uint16_t i;
	for(i = 0; i< len; i++){
		if(numEnter && i%numEnter == 0 && i!=0)
			printf("\n");
		printf("%02X ", pdata[i]);
	}
	while(endEnter--)
		printf("\n");
  LeaveCriticalSection(&g_log_cs);
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
  EnterCriticalSection(&g_log_cs);
  
	static char char_buff[1024]; // 字符串缓冲区
	memset(char_buff, 0, sizeof char_buff);
 
  // args为定义的一个指向可变参数的变量，va_list以及下边要用到的
  // va_start,va_end都是是在定义可变参数函数中必须要用到宏，在stdarg.h头文件中定义
	va_list args; 
  va_start(args, fmt);
  int retLen = vsprintf(char_buff, fmt, args);
  va_end(args); // 初始化args的函数，使其指向可变参数的第一个参数，fmt是可变参数的前一个参数
  LeaveCriticalSection(&g_log_cs);

  if( Socket == NULL )
    return SendToAllClients(char_buff, retLen);
  else
    return send(*Socket, char_buff, retLen, 0);
}

int SafePrintf(const char* format, ...)
{
    EnterCriticalSection(&g_log_cs);
    
    va_list args;
    va_start(args, format);
    int ret = vprintf(format, args);
    va_end(args);
    
    LeaveCriticalSection(&g_log_cs);
    return ret;
}

char *getCurrentTime(void) 
{
  static char timeStr[40];
  memset(timeStr, 0, sizeof timeStr);
  SYSTEMTIME st;
  GetLocalTime(&st);  // 获取本地时间

  // 格式化为 "YYYY-MM-DD HH:MM:SS"
  sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
          st.wYear, st.wMonth, st.wDay,
          st.wHour, st.wMinute, st.wSecond);
  return timeStr;
}

void print_build_info(void) 
{
    printf("\n========================================\n");
    printf("  Program    : %s\n", "串口转TCP服务端");
    printf("  Version    : %s\n", "1.0.0");
    printf("  Build Date : %s %s\n", __DATE__, __TIME__);
    printf("  Compiler   : GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
    printf("========================================\n\n");
}

int main(int argc, char const *argv[])
{
    print_build_info();

    if( argc || argv){} 
    time(&runInfo.startTime);  // 获取当前时间（从 1970-01-01 00:00:00 开始的秒数）
 
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        SafePrintf("WSAStartup failed: %d\n", iResult);
        return 1;
    }

    InitializeCriticalSection(&g_log_cs);
    InitializeCriticalSection(&csClient);
    InitializeCriticalSection(&csComPort);

    // 初始化客户端数组
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket = INVALID_SOCKET;
        clients[i].hThread = NULL;
    }

    // 解析来自程序传递的端口号
    int port = ParsePortParameter(argc, argv, DEFAULT_PORT);

    // 查找可用端口
    port = FindAvailablePort(port);
    if (port == -1) {
        SafePrintf("No available port found\n");
        WSACleanup();
        return 1;
    }

    // 创建服务器套接字
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        SafePrintf("Error at socket(): %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // 禁用Nagle算法
    char nagleStatus = 1;
    int result = setsockopt(serverSocket, //socket的文件描述符
                            IPPROTO_TCP,
                            TCP_NODELAY,
                            &nagleStatus, 
                            sizeof(int));    // 1 - on, 0 - off
    if (result < 0)
      SafePrintf("disable Nagle failed : %d\n", result);

    // 绑定套接字
    struct sockaddr_in service;
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = INADDR_ANY;
    service.sin_port = htons(port);
    runInfo.port = port;

    if (bind(serverSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
        SafePrintf("bind failed with error: %d\n", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // 监听
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        SafePrintf("listen failed with error: %d\n", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // 启动设备变化监听
    StartDeviceChangeMonitor();
 
    SafePrintf("Server started on port %d\n", port);

    fd_set readSet;
    struct timeval timeout;
    int selRet;
    SOCKET clientSocket;

    while (true) {
        FD_ZERO(&readSet);
        FD_SET(serverSocket, &readSet);

        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        selRet = select(0, &readSet, NULL, NULL, &timeout);
        if (selRet == 0) { 
            updataConsoleTitle("Main", GetCurrentThreadId());
            continue;
        }
        else if (selRet == SOCKET_ERROR) {
            SafePrintf("select failed, error=%d\n", WSAGetLastError());
            break;
        } 

        if (!FD_ISSET(serverSocket, &readSet)) 
            continue;

        clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            SafePrintf("accept failed, error=%d\n", WSAGetLastError());
            continue;
        }

        EnterCriticalSection(&csClient);

        // 查找空闲位置或最早的客户端
        int8_t newCountIndex = -1;
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (clients[i].socket == INVALID_SOCKET) {
                newCountIndex = i;
                break;
            }
        
        // 如果没有空闲位置，踢掉最早的客户端
        if (newCountIndex == -1) {
            uint8_t oldestIndex = 0;
            __int64 oldestTime = clients[0].connectTime;

            // 找出最早连接的客户端
            for (uint8_t i = 1; i < MAX_CLIENTS; i++)
                if (clients[i].connectTime < oldestTime) {
                    oldestTime = clients[i].connectTime;
                    oldestIndex = i;
                }
            
            // 通知被踢的客户端
            printfSend(&clients[oldestIndex].socket, 
                "%sYou are kicked due to server full! Your index %d, connected at %I64d ms\n", 
                CTRL_HEADER, oldestIndex, oldestTime);
            
            SafePrintf("Kicked oldest client index %d (connected at %I64d ms)\n", 
                oldestIndex, oldestTime);
            
            // 关闭最早的客户端
            CloseClient(oldestIndex, "Server full, kicking oldest client");
            newCountIndex = oldestIndex;
        }

        // 添加新客户端
        addClient(newCountIndex, clientSocket);
        LeaveCriticalSection(&csClient);
    }

  // 清理
  for (uint8_t i = 0; i < MAX_CLIENTS; i++)
    CloseClient( i, "清理");
  CloseComPort();
  closesocket(serverSocket);
  DeleteCriticalSection(&csClient);
  DeleteCriticalSection(&csComPort);
  DeleteCriticalSection(&g_log_cs);
  WSACleanup();
  StopDeviceChangeMonitor();  
  return 0;
}

void updataConsoleTitle(char *threadName, DWORD theradID)
{ 
  char title[100];
  memset(title, 0, sizeof title);
  time_t currentTime;
  time(&currentTime); 
  currentTime -= runInfo.startTime;
  //currentTime += 60*60*24 - 6;

  time_t sec = currentTime % 60;
  time_t min = currentTime / 60 % 60;
  time_t hour = currentTime / 60 / 60 % 24;
  time_t day = currentTime / 60 / 60 / 24;
 

  sprintf(title,"串口转TCP     服务端口号：%d   "
    "已运行%I64u天：%02I64u:%02I64u:%02I64u   客户端：%d/%d   %s%s   线程%ld：%s", 
       runInfo.port, day, hour, min,sec, runInfo.clientCount, MAX_CLIENTS,
       comPort.isOpen? "打开串口：":" ", comPort.portName, theradID, threadName =! NULL? threadName:" "); 
  SetConsoleTitleA( title );
}

// 获取当前时间戳（毫秒）
__int64 GetCurrentTimeMillis(void) 
{
    struct _timeb timebuffer;
    _ftime_s(&timebuffer);
    return (__int64)timebuffer.time * 1000 + timebuffer.millitm;
}


/*
获取收发方向字符串
 direct 参数如下是如下字符串
   [COM --> TCP]
   [TCP --> COM]

  index 客户端索引号
*/
char *getSendRecvDirectionStr(char *direct, uint8_t index)
{
  char *endptr;  // 用于检测未转换的字符 
  uint8_t comNum = strtol(&comPort.portName[3], &endptr, 10);

  static char retStr[20];
  memset(retStr, 0, sizeof retStr);
  strcpy(retStr, "[    -->    ]");

  if( strcmp(direct, "[TCP --> COM]") == 0 ){
    memset(retStr, 0, sizeof retStr);
    sprintf(retStr, "TCP%-3d--> COM%-3d" , index, comNum);
  }

  if( strcmp(direct, "[COM --> TCP]") == 0 ){
    memset(retStr, 0, sizeof retStr);

    if( runInfo.monopolizeSoclet != NULL ) // 独占串口数据
      sprintf(retStr, "COM%-3d--> TCP%-3d" , comNum, runInfo.monopolizeIndex);
    else
      sprintf(retStr, "COM%-3d--> TCP   " , comNum);
  }

  return retStr;
}




#define MIN_USER_PORT   1024
#define MAX_PORT        65535

// 解析命令行参数获取端口号
// 参数: argc - 参数个数, argv - 参数数组, defaultPort - 默认端口号
// 返回值: 解析成功的端口号，如果无效则返回-1
int ParsePortParameter(int argc, char const* argv[], int defaultPort) 
{
    for (int i = 1; i < argc; i++) {
        // 检查参数是否以-p或-P开头
        if ((argv[i][0] == '-' || argv[i][0] == '/') && 
            tolower(argv[i][1]) == 'p' && 
            argv[i][2] != '\0') {
            
            // 获取端口号部分
            char const * portStr = &argv[i][2];
            char* endPtr;
            long port = strtol(portStr, &endPtr, 10);
            
            // 验证转换是否成功
            if (*endPtr != '\0') {
                fprintf(stderr, "错误: 端口号 '%s' 包含非数字字符\n", portStr);
                return -1;
            }
            
            // 检查端口范围
            if (port <= MIN_USER_PORT) {
                fprintf(stderr, "错误: 端口号必须大于 %d (当前: %ld)\n", MIN_USER_PORT, port);
                return -1;
            }
            
            if (port > MAX_PORT) {
                fprintf(stderr, "错误: 端口号不能超过 %d (当前: %ld)\n", MAX_PORT, port);
                return -1;
            }
            
            return (int)port;
        }
        // 支持格式: -p 5000 (带空格)
        else if ((argv[i][0] == '-' || argv[i][0] == '/') && 
                 tolower(argv[i][1]) == 'p' && 
                 argv[i][2] == '\0' && 
                 i + 1 < argc) {
            
            char const* portStr = argv[i+1];
            char* endPtr;
            long port = strtol(portStr, &endPtr, 10);
            
            if (*endPtr != '\0') {
                fprintf(stderr, "错误: 端口号 '%s' 包含非数字字符\n", portStr);
                return -1;
            }
            
            if (port <= MIN_USER_PORT) {
                fprintf(stderr, "错误: 端口号必须大于 %d (当前: %ld)\n", MIN_USER_PORT, port);
                return -1;
            }
            
            if (port > MAX_PORT) {
                fprintf(stderr, "错误: 端口号不能超过 %d (当前: %ld)\n", MAX_PORT, port);
                return -1;
            }
            
            return (int)port;
        }
    }
    
    // 没有指定-p参数，返回默认端口
    return defaultPort;
}







#include <dbt.h>       // 设备通知相关定义
#include <winuser.h>   // 窗口消息相关

// 全局变量
static volatile BOOL g_bDeviceChangeThreadRunning = FALSE;
HANDLE g_hDeviceChangeThread = NULL;

// 设备变化通知线程
DWORD WINAPI DeviceChangeMonitorThread(LPVOID lpParam)
{
    if( lpParam == NULL ){}

    // 创建隐藏窗口接收消息
    HWND hWnd = CreateWindowEx(0, "STATIC", "DeviceMonitor", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);

    // 设置设备接口通知
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter = {0};
    NotificationFilter.dbcc_size = sizeof NotificationFilter;
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_COMPORT;

    HDEVNOTIFY hDevNotify = RegisterDeviceNotification(hWnd, &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (hDevNotify == NULL) {
        SafePrintf("RegisterDeviceNotification failed: %ld\n", GetLastError());
        DestroyWindow(hWnd);
        return 1;
    }

    g_bDeviceChangeThreadRunning = TRUE;
    MSG msg;
    while ( g_bDeviceChangeThreadRunning && GetMessage(&msg, hWnd, 0, 0) ) {
      updataConsoleTitle("DCM",  GetCurrentThreadId());

      // 打开 word 后 任何地方按下 crtl+c crtl+V 等快捷键操作这里的消息就会变的很多*/
      SafePrintf("Device change detected, wParam:%I64d, lParam:%I64d, message:%d, theradID:%ld\n", 
        msg.wParam, msg.lParam, msg.message, GetCurrentThreadId());
      
      // 以下参数是设备插拔或最明显的变化
      if( msg.wParam == 0 && msg.lParam == 0 && msg.message == 49926 ){
        printfSend(NULL, "%sDevice change detected (%I64d:%I64d)\n", 
          CTRL_HEADER, msg.wParam, msg.message); 
        ListComPorts(NULL);
      }

      #if 0
        if (msg.message == WM_DEVICECHANGE) {
            switch (msg.wParam) {
                case DBT_DEVICEARRIVAL:         // 设备插入
                case DBT_DEVICEREMOVECOMPLETE:  // 设备拔出 
                    // 通知所有客户端串口列表变化
                    break;
            }
        }
        #endif
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理
    UnregisterDeviceNotification(hDevNotify);
    DestroyWindow(hWnd);
    return 0;
}

// 启动设备监听线程
void StartDeviceChangeMonitor(void)
{
  // 在main函数开始处添加
  WNDCLASS wc = {0};
  wc.lpfnWndProc = DefWindowProc;
  wc.hInstance = GetModuleHandle(NULL);
  wc.lpszClassName = "DeviceMonitor";
  RegisterClass(&wc);

  if (g_hDeviceChangeThread == NULL)
    g_hDeviceChangeThread = CreateThread(NULL, 0, DeviceChangeMonitorThread, NULL, 0, NULL);
}

// 停止设备监听线程
void StopDeviceChangeMonitor(void)
{
    g_bDeviceChangeThreadRunning = FALSE;
    if (g_hDeviceChangeThread) {
        WaitForSingleObject(g_hDeviceChangeThread, 1000);
        CloseHandle(g_hDeviceChangeThread);
        g_hDeviceChangeThread = NULL;
    }
}

