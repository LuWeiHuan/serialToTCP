/******************************************************************************
  * @file    文件 COM.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "COM.h"
#include "main.h"
#include "public.h"
#include "logPrint.h"
#include "clients.h"
#include "TrafficStats.h"
#include "Queue.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <setupapi.h>
#include <devguid.h>
#include <tchar.h>
#else
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/socket.h>
#include <ctype.h>
#endif

/*================== 本地数据类型     =========================================*/
typedef struct {
#ifdef _WIN32
    DCB dcb;           // Windows专用
    void *hCom;
#else
    int hCom;
#endif// !_WIN32
    bool isOpen;
    char portName[20];
    thread_t hThread;
    threadID_t threadId;
    uint64_t sendCount;
} ComPortInfo_t;

/*================== 本地宏定义     =========================================*/
/*================== 本地常量声明    ========================================*/
static mutex_type csComPort;
static AsyncQueue_t asyncSendQueue = {0}, asyncRecvQueue = {0};
static ComPortInfo_t comPort = { 
  .hCom = INVALID_HANDLE_VALUE, 
  #ifdef _WIN32
  .dcb = {0},
  #endif
  .isOpen = false, 
  .portName = "NULL", 
  .hThread=  (thread_t)0,
  .threadId = 0,
  .sendCount = 0
};

/*================== 本地变量声明    ========================================*/
/*================== 全局共享变量    ========================================*/
ComPortInfo_t const * const ComPort = &comPort;

/*================== 本地函数声明    ========================================*/
static threadRet WINAPI ComRecvDataThread(void *param);
static void trueHandleReceivedData(const uint8_t *data, uint32_t len);
static void get_COM_VID_PID_REV(const char* portName, char *retVID, char *retPID, char *retREV);

bool getComIsOpen(void)
{
  return comPort.isOpen;
}

const char * getComName(void)
{
  return comPort.portName;
}

void ComPortResourceInit(bool start) 
{
  if( start ){ 
    InitializeCriticalSection_Wrapper(&csComPort);
    COM_UseAsyncRecv(100);
  }
  else {
    COM_UseAsyncRecv(0);
    CloseComPort("clear exit");
    DeleteCriticalSection_Wrapper(&csComPort);
  }
}

void CloseComPort(const char *reason) 
{
  EnterCriticalSection_Wrapper(&csComPort);
  if (comPort.isOpen == false) {
    SafePrintf("%s is Close， reason:%s\n", comPort.portName, reason? reason:"unknown");
    LeaveCriticalSection_Wrapper(&csComPort);
    return;
  }

  bool closeComRet = false;
  comPort.isOpen = false;
  if (comPort.hCom != INVALID_HANDLE_VALUE) {
#ifdef _WIN32
    closeComRet = CloseHandle(comPort.hCom);
#else
    closeComRet = (close(comPort.hCom) == 0);
#endif
    comPort.hCom = INVALID_HANDLE_VALUE;
  }
  

  char *hThreadCloseInfo = "External Call";
  bool isSelfCall = (comPort.threadId == GetCurrentThreadId_Wrapper());
  if (comPort.hThread && isSelfCall == false) {
    DWORD waitResult = WaitForSingleObject_Wrapper(comPort.hThread, 1000);
    if (waitResult == WAIT_TIMEOUT) {
      SafePrintf("Close Com Port Read Thread Wait Timeout\n");
    }
    bool closeThreadRet = CloseHandle(comPort.hThread);
    hThreadCloseInfo = getPrintf("thread exit %s, Wait %ld", 
        closeThreadRet == false? "failed":"success", waitResult);
  }
  
  const char *closedInfo = getPrintf("closed %s %s, %s. reason: %s\n", 
      comPort.portName, closeComRet == false? "failed":"success", 
      hThreadCloseInfo, reason? reason:"unknown");

  printfSend(NULL, "%s", closedInfo);
  SafePrintf("%s", closedInfo);

  comPort.hThread = (thread_t)0;
  memset(comPort.portName, 0, sizeof comPort.portName);
  LeaveCriticalSection_Wrapper(&csComPort);
}



#ifdef _WIN32
// Windows 串口实现
const char *getComPortList(bool VPID) 
{
  EnterCriticalSection_Wrapper(&csComPort);
  HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
  if (hDevInfo == INVALID_HANDLE_VALUE) 
    return "COM Ports: GUID_DEVCLASS_PORTS NULL";
      
  bool exist = false;  
  static char response[2048];
  memset(response, 0, sizeof response); 

  strcpy(response, VPID? "COM Ports:\n": "COM Ports: ");

  SP_DEVINFO_DATA deviceInfoData;
  deviceInfoData.cbSize = sizeof deviceInfoData;
  BYTE buffer[256];

  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); i++) {
    memset(buffer, 0, sizeof buffer);
    DWORD dataType, bufferSize = sizeof buffer;

    bool ret = SetupDiGetDeviceRegistryPropertyA(hDevInfo, &deviceInfoData, 
        SPDRP_FRIENDLYNAME, &dataType, buffer, bufferSize, &bufferSize);
        
    if (ret == false) 
        continue;
    
    char* start = strrchr((char*)buffer, '(');
    if (start == NULL)
        continue;
        
    char* end = strchr(start, ')');
    if (end == NULL)
        continue;
    
    *end = '\0';
    
    char* portName = start + 1;
    if (strncmp(portName, "COM", 3) != 0) {
        *end = ')';
        continue;
    }
    
    if (strlen(portName) > 3 && isdigit(portName[3])) {
      if (exist) 
          strcat(response, VPID? ",\n":", ");
      
      static char retID[3][5], IDstring[50];
      if( VPID ){
        memset(retID, 0, sizeof retID);
        memset(IDstring, 0, sizeof IDstring);
        get_COM_VID_PID_REV(portName, retID[0], retID[1], retID[2]);
        snprintf(IDstring, sizeof IDstring, "%-6s [VID_%-4s PID_%-4s REV_%-4s]", 
          portName, retID[0], retID[1], retID[2]);
      }

      strcat(response, VPID? IDstring:portName);
        exist = true;
    }

    *end = ')';
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);

  if (exist == false)
    strcat(response, "No COM ports found");

  LeaveCriticalSection_Wrapper(&csComPort);
  return response;
}

int8_t OpenComPort(const char* portName, uint32_t baudRate, 
        uint8_t dataBits, uint8_t stopBits, uint8_t parity)
{ 
  EnterCriticalSection_Wrapper(&csComPort);

  char fullPortName[20];
  memset(fullPortName, 0, sizeof fullPortName);
  snprintf(fullPortName, sizeof fullPortName, "\\\\.\\%s", portName);

  comPort.hCom = CreateFileA(fullPortName, 
    GENERIC_READ | GENERIC_WRITE,
    0,
    NULL,
    OPEN_EXISTING,
    FILE_FLAG_OVERLAPPED,
    NULL);

  if (comPort.hCom == INVALID_HANDLE_VALUE) {
    LeaveCriticalSection_Wrapper(&csComPort);
    return -1;
  }

  memset(&comPort.dcb, 0, sizeof comPort.dcb);
  comPort.dcb.DCBlength = sizeof comPort.dcb;
  if (!GetCommState(comPort.hCom, &comPort.dcb)) {
    CloseHandle(comPort.hCom);
    comPort.hCom = INVALID_HANDLE_VALUE;
    LeaveCriticalSection_Wrapper(&csComPort);
    return -2;
  }

  comPort.dcb.BaudRate = baudRate;
  comPort.dcb.ByteSize = (BYTE)dataBits;
  comPort.dcb.StopBits = stopBits == 1 ? ONESTOPBIT : TWOSTOPBITS;
  comPort.dcb.Parity = (BYTE)parity;
  
  comPort.dcb.fBinary = true;
  comPort.dcb.fOutxCtsFlow = false;
  comPort.dcb.fOutxDsrFlow = false;
  comPort.dcb.fDtrControl = DTR_CONTROL_ENABLE;
  comPort.dcb.fRtsControl = RTS_CONTROL_ENABLE;
  comPort.dcb.fOutX = false;
  comPort.dcb.fInX = false;
  comPort.dcb.fErrorChar = false;
  comPort.dcb.fNull = false;
  comPort.dcb.fAbortOnError = false;

  if (!SetCommState(comPort.hCom, &comPort.dcb)) {
    CloseHandle(comPort.hCom);
    comPort.hCom = INVALID_HANDLE_VALUE;
    LeaveCriticalSection_Wrapper(&csComPort);
    return -3;
  }

  COMMTIMEOUTS timeouts = {0};
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.ReadTotalTimeoutConstant = 0;
  timeouts.WriteTotalTimeoutMultiplier = 10;
  timeouts.WriteTotalTimeoutConstant = 1000;
  SetCommTimeouts(comPort.hCom, &timeouts);

  memset(comPort.portName, 0, sizeof comPort.portName);
  strcpy(comPort.portName, portName);
  comPort.isOpen = true;

  comPort.hThread = threadCreate(&comPort.threadId, ComRecvDataThread, &comPort);
  if (comPort.hThread == (thread_t)0) {
    CloseHandle(comPort.hCom);
    comPort.hCom = INVALID_HANDLE_VALUE;
    comPort.isOpen = false;
    LeaveCriticalSection_Wrapper(&csComPort);
    return -4;
  }

  LeaveCriticalSection_Wrapper(&csComPort);
  return 0;
}

#else

// Linux 串口实现
const char *getComPortList(bool VPID) 
{
    static char response[4096];
    size_t pos = 0;
    
    // 初始化响应
    const char *header = VPID ? "COM Ports:\n" : "COM Ports: ";
    strcpy(response, header);
    pos = strlen(header);
    
    DIR *dir;
    struct dirent *entry;
    bool exist = false;
    
    dir = opendir("/dev");
    if (!dir) return "无法访问 /dev 目录";
    
    while ((entry = readdir(dir)) != NULL) {
        if (  strncmp(entry->d_name, "ttyS", 4) == 0 &&
          '0' <= entry->d_name[4] && entry->d_name[4] <= '9' )
          continue;

        if (strncmp(entry->d_name, "ttyS", 4) == 0 || 
            strncmp(entry->d_name, "ttyUSB", 6) == 0 ||
            strncmp(entry->d_name, "ttyACM", 6) == 0) {
            
            // 设备有效性检查
            char fullPath[32];
            snprintf(fullPath, sizeof(fullPath), "/dev/%.10s", entry->d_name);
            struct stat st;
            if (stat(fullPath, &st) != 0 || !S_ISCHR(st.st_mode)) {
                continue;
            }
            
            // 添加分隔符
            if (exist) {
                const char *separator = VPID ? ",\n" : ", ";
                size_t sep_len = strlen(separator);
                if (pos + sep_len < sizeof(response)) {
                    strcat(response + pos, separator);
                    pos += sep_len;
                }
            }
            
            // 添加设备信息
            if (VPID) {
                char vid[5] = "N/A", pid[5] = "N/A", rev[5] = "N/A";
                
                get_COM_VID_PID_REV(entry->d_name, vid, pid, rev);
                
                char info[285];
                snprintf(info, sizeof(info), "%-12s [VID_%-4s PID_%-4s REV_%-4s]", 
                        entry->d_name, vid, pid, rev);
                
                size_t info_len = strlen(info);
                if (pos + info_len < sizeof(response)) {
                    strcat(response + pos, info);
                    pos += info_len;
                }
            } else {
                size_t name_len = strlen(entry->d_name);
                if (pos + name_len < sizeof(response)) {
                    strcat(response + pos, entry->d_name);
                    pos += name_len;
                }
            }
            
            exist = true;
        }
    }
    
    closedir(dir);
    
    if (!exist) {
        if (pos + strlen("No COM ports found") < sizeof(response)) {
            strcat(response + pos, "No COM ports found");
        }
    }
    
    return response;
}

int8_t OpenComPort(const char* portName, uint32_t baudRate, 
        uint8_t dataBits, uint8_t stopBits, uint8_t parity)
{
    char fullPortName[32];
    snprintf(fullPortName, sizeof(fullPortName), "/dev/%s", portName);
    
    comPort.hCom = open(fullPortName, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (comPort.hCom < 0) {
        SafePrintf("Failed to open %s: %s\n", fullPortName, strerror(errno));
        return -1;
    }
    
    struct termios options;
    tcgetattr(comPort.hCom, &options);
    
    // 设置波特率
    speed_t speed;
    switch (baudRate) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        case 460800: speed = B460800; break;
        case 500000: speed = B500000; break;
        case 576000: speed = B576000; break;
        case 921600: speed = B921600; break;
        default: speed = B115200; break;
    }
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
    
    // 设置数据位
    options.c_cflag &= ~CSIZE;
    switch (dataBits) {
        case 5: options.c_cflag |= CS5; break;
        case 6: options.c_cflag |= CS6; break;
        case 7: options.c_cflag |= CS7; break;
        case 8: options.c_cflag |= CS8; break;
        default: options.c_cflag |= CS8; break;
    }
    
    // 设置停止位
    if (stopBits == 2) {
        options.c_cflag |= CSTOPB;
    } else {
        options.c_cflag &= ~CSTOPB;
    }
    
    // 设置校验位
    switch (parity) {
        case 1: // 奇校验
            options.c_cflag |= PARENB;
            options.c_cflag |= PARODD;
            break;
        case 2: // 偶校验
            options.c_cflag |= PARENB;
            options.c_cflag &= ~PARODD;
            break;
        default: // 无校验
            options.c_cflag &= ~PARENB;
            break;
    }
    
    // 基本设置
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_iflag = IGNPAR;
    options.c_oflag = 0;
    options.c_lflag = 0;
    
    options.c_cc[VTIME] = 0;
    options.c_cc[VMIN] = 0;
    
    tcflush(comPort.hCom, TCIFLUSH);
    
    if (tcsetattr(comPort.hCom, TCSANOW, &options) != 0) {
        close(comPort.hCom);
        return -3;
    }
    
     
    strcpy(comPort.portName, portName);
    comPort.isOpen = true;
    
    comPort.hThread = threadCreate(&comPort.threadId, ComRecvDataThread, &comPort);
    if (comPort.hThread == (thread_t)0) {
        close(comPort.hCom); 
        comPort.isOpen = false;
        return -4;
    }
    
    return 0;
}
#endif

static void handleReceivedData(const char *comRecvBuffer, DWORD len)
{
  if( comRecvBuffer == NULL || len == 0 )
    return;
    
  if( AddDataToAsyncQueue(&asyncRecvQueue, (uint8_t*)comRecvBuffer, len) == false )   
    trueHandleReceivedData((uint8_t*)comRecvBuffer, len); 
}

static threadRet WINAPI ComRecvDataThread(void *param)
{
    (void)param;
    static char comRecvBuffer[RECV_BUFFER_SIZE];
    DWORD bytesRead = 0, totalBytesRead = 0;
    uint8_t zeroNumMax = 30, zeroNum = 0, timeoutNum = 0;
    bool recvIsAligned = false;
    const char *ThreadExitReason = "Thread Exit, NULL";
    
#ifdef _WIN32
    OVERLAPPED overlapped = {0};
    bool readRet;
    DWORD lastUpdateTime = 0, currentTickCount = 0;
    const DWORD updateInterval = 1500;
    
    while ( comPort.isOpen ) {
        if( bytesRead == 0 || totalBytesRead == 0 )
            Sleep(1);
        CloseHandle(overlapped.hEvent);

        memset(&overlapped, 0, sizeof overlapped);
        overlapped.hEvent = CreateEvent(NULL, true, false, NULL);
        
        DWORD remainingSpace = (sizeof comRecvBuffer) - 1 - totalBytesRead;
        if (zeroNum > zeroNumMax || timeoutNum > 10 || remainingSpace == 0) {  
            SafePrintf("COM Repeatedly Read 4KB [Zero:%s(%d), timeout:%-2d, Full:%s] Recv:%ld(4KB:%0.1f)\n", 
                totalBytesRead % 4096 == 0? "YES":"NO", zeroNumMax, 
                timeoutNum, remainingSpace == 0? "YES": "NO ",
                totalBytesRead, totalBytesRead / 4096.0);
            handleReceivedData(comRecvBuffer, totalBytesRead);
            totalBytesRead = zeroNum = timeoutNum = 0; 
            remainingSpace = (sizeof comRecvBuffer) - 1; 
        }
        
      // 发起异步读取
      readRet = ReadFile(comPort.hCom, comRecvBuffer + totalBytesRead, 
          remainingSpace, &bytesRead, &overlapped);
      if (!readRet) {
        DWORD error = GetLastError();
        if (error == ERROR_IO_PENDING) {  // 等待读取完成或超时 
          DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 1000);
          timeoutNum = waitResult == WAIT_TIMEOUT ? timeoutNum+1:0;
          if (waitResult == WAIT_TIMEOUT) {
            updataConsoleTitle("COM: Timeout");
            continue;
          }
          else if (waitResult == WAIT_OBJECT_0) { // 读取完成 
            if (!GetOverlappedResult(comPort.hCom, &overlapped, &bytesRead, false)) {
              error = GetLastError();
              if (error != ERROR_OPERATION_ABORTED) { 
                ThreadExitReason = getPrintf("Thread Exit[B], %s read error:%ld", comPort.portName, error);
                break;
              }
            }
          }
        }
        else if (error != ERROR_OPERATION_ABORTED) { 
          ThreadExitReason = getPrintf("Thread Exit[A], %s read error:%ld", comPort.portName, error); 
          break;
        }
      }

      if (bytesRead == 0) { // 处理接收到的数据如果是空读取就重新读
        currentTickCount = GetTickCount();   // 按间隔更新线程状态
        if (currentTickCount - lastUpdateTime >= updateInterval) {
          updataConsoleTitle(comPort.portName);
          lastUpdateTime = currentTickCount;
        }
        zeroNum = recvIsAligned && totalBytesRead? zeroNum + 1:0; 
        continue;
      }

      recvIsAligned = bytesRead % 1024 == 0? true:false; 
      if((recvIsAligned && zeroNumMax >= 30) || zeroNumMax - 2 < zeroNum )
        zeroNumMax = zeroNum + 2;
      
      // if( recvIsAligned && zeroNum)
      //   SafePrintf("COM zero Num:%-3d/%-3d, bytesRead:%ld total:%ld\n", 
      //     zeroNum, zeroNumMax, bytesRead, totalBytesRead + bytesRead);

      totalBytesRead += bytesRead;  // 更新总字节数

      if( runInfo.COMrecv4Knum < totalBytesRead ){
          zeroNumMax = 30;
          SafePrintf("\n\n");
      }

      // 处理串口接收到的数据
      if( (recvIsAligned == false && totalBytesRead) || 
          runInfo.COMrecv4Knum < totalBytesRead ) {
        handleReceivedData(comRecvBuffer, totalBytesRead);
        totalBytesRead = 0;
      }
      zeroNum = timeoutNum = 0;
    }

#else  
    fd_set readSet;
    struct timeval timeout;
    ssize_t len = 0;
    
    while (comPort.isOpen) { 
        FD_ZERO(&readSet);
        FD_SET(comPort.hCom, &readSet);
        
        // 设置超时：如果是对齐数据，使用较短超时；否则使用正常超时
        if (recvIsAligned && totalBytesRead > 0) {
            timeout.tv_sec = 0;
            timeout.tv_usec = 5000; // 5ms短超时，用于快速检测是否有更多对齐数据
        } 
        else {
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
        }

        int ret = select(comPort.hCom + 1, &readSet, NULL, NULL, &timeout);
        if (ret == -1) {
            if (errno == EINTR) 
                continue;
            ThreadExitReason = getPrintf("Select error: %s", strerror(errno));
            break;
        }
        
        if (ret == 0) { // 超时处理：检查是否需要发送积攒的数据
            timeoutNum++;
            if (totalBytesRead > 0 && (timeoutNum > 2 || zeroNum > zeroNumMax)) {
                SafePrintf("COM Timeout Send [Zero:%d/%d, timeout:%d] Recv:%ld(1KB:%.1f)\n", 
                    zeroNum, zeroNumMax, timeoutNum, totalBytesRead, totalBytesRead / 4096.0);
                handleReceivedData(comRecvBuffer, totalBytesRead);
                totalBytesRead = zeroNum = timeoutNum = recvIsAligned = false;
            }
            continue;
        }
        
        ret = FD_ISSET(comPort.hCom, &readSet);
        if (!ret) {
            SafePrintf("FD_ISSET %d\n", ret);
            continue;
        }
        
        // 计算剩余空间
        size_t remainingSpace = sizeof(comRecvBuffer) - 1 - totalBytesRead;
        if (remainingSpace == 0) {
            // 缓冲区满，立即发送
            SafePrintf("COM Buffer Full, Sending %ld bytes\n", totalBytesRead);
            handleReceivedData(comRecvBuffer, totalBytesRead);
            totalBytesRead = zeroNum = timeoutNum = 0;
            remainingSpace = sizeof(comRecvBuffer) - 1;
        }
        
        // 读取数据
        len = read(comPort.hCom, comRecvBuffer + totalBytesRead, remainingSpace);
        if (len > 0) { 
            bytesRead = len;      // 更新统计信息
            recvIsAligned = ((bytesRead % 128 == 0) || (bytesRead % 1024 == 0) || (bytesRead % 4095 == 0));
            
            // 动态调整 zeroNumMax
            if( (recvIsAligned && zeroNumMax >= 30) || zeroNumMax - 2 < zeroNum) 
                zeroNumMax = zeroNum + 2;
            
            totalBytesRead += bytesRead;
            timeoutNum = zeroNum = 0; // 收到数据，重置零计数
            
            // 检查是否需要发送数据
            bool shouldSend = false;
 
            // 情况1: 非对齐数据，立即发送
            if (recvIsAligned == false && totalBytesRead) {
                shouldSend = true;
            }
            // 情况2: 达到调试阈值
            else if (runInfo.COMrecv4Knum < totalBytesRead) {
                zeroNumMax = 30;
                shouldSend = true;
                SafePrintf("\n\n");
            }
            // 情况3: 对齐数据但需要检查积攒条件
            else if (recvIsAligned && zeroNum > zeroNumMax) { 
                shouldSend = true;  // 如果连续多次收到4KB数据，积攒几次后发送 
            }

            if (shouldSend) { 
                handleReceivedData(comRecvBuffer, totalBytesRead);
                totalBytesRead = recvIsAligned = false;
            } 
        } 
        else if (len == 0) { // EOF - 设备断开
            ThreadExitReason = getPrintf("Device disconnected (EOF): %s", strerror(errno));
            break;
        } 
        else {  // 读取错误 
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞读取返回，增加零计数
                if (++zeroNum > zeroNumMax && totalBytesRead > 0) {
                    SafePrintf("COM Max Zero Count Reached, Sending %ld bytes\n", totalBytesRead);
                    handleReceivedData(comRecvBuffer, totalBytesRead);
                    totalBytesRead = zeroNum = recvIsAligned = false;
                }
            } 
            else { // 其他错误
                ThreadExitReason = getPrintf("Read error: %s", strerror(errno));
                break;
            } 
        }
    }
 
#endif 
    CloseComPort(ThreadExitReason);
#ifdef _WIN32
    CloseHandle(overlapped.hEvent);
#endif
    if (totalBytesRead > 0)
        handleReceivedData(comRecvBuffer, totalBytesRead);
        
    return (threadRet)0;
}

// 处理串口发过来的数据
static void trueHandleReceivedData(const uint8_t *comRecvBuffer, uint32_t len)
{
  if( comRecvBuffer == NULL || len == 0 ){
    return;
  }
  
#ifdef __TRAFFIC_STATS_H_
    trafficStats.com.totalBytesReceived += len;
#endif
    
    int sendRet = 0;
    do {
        if( getClientNum() == 0 )
            break;

        if (runInfo.monopolizeComRecvIndex == NULL ){
            sendRet = sendDataToClients(NULL, (char*)comRecvBuffer, len);
            break;
        }
        
        const socket_t *socket = getClientSocket( *runInfo.monopolizeComRecvIndex );
        if( socket == NULL || *socket == INVALID_SOCKET_VALUE ){
            runInfo.monopolizeComRecvIndex = NULL;
            continue;
        }
          
        sendRet = sendDataToClients(socket, (char*)comRecvBuffer, len);
        if (sendRet > 0 ) break;

        bool Exist = getClientIndex(socket, NULL);
        if( Exist )
            break;
        runInfo.monopolizeComRecvIndex = NULL;
        continue;
    } while (0);
    
    char *Direct = getSendRecvDirectionStr("[COM --> TCP]", 0);
    char *timeStr = getCurrentTimeStringSec();
    
    int ClientNum = getClientNum();
    int lenSum = runInfo.monopolizeComRecvIndex ? len : len * ClientNum;
    int oneLen = ClientNum == 0 ? 0 : sendRet / ClientNum;
    
    if( runInfo.monopolizeComRecvIndex )
        oneLen = sendRet;

    char isEnter = ( ( runInfo.serverPrintData == 0 || runInfo.serverPrintData == 3) && 
          ( ClientNum == 0 || runInfo.COMrecvPoll == false)) ? '\r':'\n';

    SafePrintf( "%-21s%10" PRIu64 " [%s]  %-6d/%-6u Byte (%s : %d)%s %c",
        timeStr, ++comPort.sendCount, Direct, oneLen, len,
        (sendRet == lenSum)? "OK":"Fail", lenSum - sendRet,
        runInfo.serverPrintData? " data:":" ", isEnter);
    
    if (runInfo.serverPrintData == 0) 
        return;
    
    if (runInfo.serverPrintData == 1)
        SafePrintf("%s", comRecvBuffer);
    if (runInfo.serverPrintData == 2)
        printHex((uint8_t*)comRecvBuffer, len, 40, 2);
}

// 串口阻塞形发数据
static uint32_t ComPortTrueSendData(uint8_t const *tcpRecvBuffer, int bytesReceived, uint32_t *retError)
{
    EnterCriticalSection_Wrapper(&csComPort);
    DWORD bytesWritten = 0;
    DWORD error = 0;
    
#ifdef _WIN32
    OVERLAPPED writeOverlapped = {0};
    writeOverlapped.hEvent = CreateEvent(NULL, true, false, NULL); 
    bool WriteRet = WriteFile(comPort.hCom, tcpRecvBuffer, 
        bytesReceived, &bytesWritten, &writeOverlapped);

    if (!WriteRet && (error = GetLastError()) == ERROR_IO_PENDING)
        error = GetOverlappedResult(comPort.hCom, &writeOverlapped, &bytesWritten, true)? 0:GetLastError();

    CloseHandle(writeOverlapped.hEvent); 
#else
    ssize_t n = write(comPort.hCom, tcpRecvBuffer, bytesReceived);
    if (n >= 0) {
        bytesWritten = n;
    } else {
        bytesWritten = 0;
        error = errno;
    }
#endif
    
    LeaveCriticalSection_Wrapper(&csComPort);
    
    if (error != 0) {
        char *reason = getPrintf("Serial Port Write Error: %lu, closing port\n", error);
        CloseComPort(reason);
    }
    
    if (retError) *retError = error;
    return (uint32_t)bytesWritten;
}

int ComPortSendData(const uint8_t *tcpRecvBuffer, int bytesReceived, uint32_t *retError) 
{
    if (!comPort.isOpen) {
        SafePrintf("COM not open, discarding data\n");
        return false;
    }
    
    if( asyncSendQueue.running &&
        AddDataToAsyncQueue(&asyncSendQueue, tcpRecvBuffer, bytesReceived) ) {
        if (retError)
            *retError = 0;
        return bytesReceived;
    }
    
    return ComPortTrueSendData((uint8_t*)tcpRecvBuffer, bytesReceived, retError);
}

static void COMAsyncSendQueueCallBack(uint8_t *data, uint32_t len)
{
    if (comPort.isOpen == false) 
        return;

    uint32_t error = 0;
    uint32_t bytesWritten = ComPortTrueSendData(data, len, &error);
    
    if (bytesWritten != len) 
        SafePrintf("COM Async send error: written %u/%u bytes, error: %u\n", 
                  bytesWritten, len, error);
}

bool COM_UseAsyncSend(uint16_t num)
{
    if( num < MIN_QUEUE_SIZE ){
        FreeAsyncQueue(&asyncSendQueue);
        return num == 0 ? true : false;
    }
        
    return startAsyncQueue(&asyncSendQueue, 
        COMAsyncSendQueueCallBack, num, RECV_BUFFER_SIZE, "COM Send");
}

static void COMAsyncRecvQueueCallBack(uint8_t *data, uint32_t len)
{
    trueHandleReceivedData(data, len);
}

bool COM_UseAsyncRecv(uint16_t num)
{
    if( num < MIN_QUEUE_SIZE ){
        FreeAsyncQueue(&asyncRecvQueue);
        return num == 0 ? true : false;
    }
        
    return startAsyncQueue(&asyncRecvQueue, 
        COMAsyncRecvQueueCallBack, num, RECV_BUFFER_SIZE, "COM Recv");
}


#ifndef _WIN32 
#include <sys/stat.h>
#include <stdlib.h>
#include <limits.h>

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#endif

/**
 * @brief Linux下获取USB串口设备的VID/PID/REV信息（兼容udev和sysfs方式）
 * @param portName 串口设备名（如 "ttyUSB0", "ttyACM0"）
 * @param retVID 返回的VID字符串（需要至少5字节空间）
 * @param retPID 返回的PID字符串（需要至少5字节空间） 
 * @param retREV 返回的REV字符串（需要至少5字节空间）
 */
static void get_COM_VID_PID_REV(const char* portName, char *retVID, char *retPID, char *retREV)
{
    if (!portName || strlen(portName) == 0)
        return;
    
    // 初始化返回值
    if (retVID) strcpy(retVID, "N/A");
    if (retPID) strcpy(retPID, "N/A");
    if (retREV) strcpy(retREV, "N/A");

    // 首先尝试使用udev方式（如果可用）
    #ifdef HAVE_LIBUDEV
    struct udev *udev = udev_new();
    if (udev) {
        struct udev_enumerate *enumerate = udev_enumerate_new(udev);
        if (enumerate) {
            udev_enumerate_add_match_subsystem(enumerate, "tty");
            udev_enumerate_scan_devices(enumerate);
            
            struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
            struct udev_list_entry *entry;
            
            udev_list_entry_foreach(entry, devices) {
                const char *path = udev_list_entry_get_name(entry);
                struct udev_device *device = udev_device_new_from_syspath(udev, path);
                
                if (!device) continue;
                
                const char *devnode = udev_device_get_devnode(device);
                if (!devnode || !strstr(devnode, portName)) {
                    udev_device_unref(device);
                    continue;
                }
                
                // 找到匹配的设备，获取父USB设备
                struct udev_device *parent = udev_device_get_parent_with_subsystem_devtype(
                    device, "usb", "usb_device");
                
                if (!parent) {
                    udev_device_unref(device);
                    continue;
                }
                
                // 获取VID
                const char *vid = udev_device_get_sysattr_value(parent, "idVendor");
                if (vid && retVID) {
                    memcpy(retVID, vid, 4);
                    retVID[4] = '\0';
                }
                
                // 获取PID
                const char *pid = udev_device_get_sysattr_value(parent, "idProduct");
                if (pid && retPID) {
                    memcpy(retPID, pid, 4);
                    retPID[4] = '\0';
                }
                
                // 获取REV
                const char *rev = udev_device_get_sysattr_value(parent, "bcdDevice");
                if (rev && retREV) {
                    memcpy(retREV, rev, 4);
                    retREV[4] = '\0';
                }
                
                udev_device_unref(device);
                
                // 转换为大写
                if (retVID && strcmp(retVID, "N/A") != 0) {
                    for (char *p = retVID; *p; p++) *p = toupper(*p);
                }
                if (retPID && strcmp(retPID, "N/A") != 0) {
                    for (char *p = retPID; *p; p++) *p = toupper(*p);
                }
                if (retREV && strcmp(retREV, "N/A") != 0) {
                    for (char *p = retREV; *p; p++) *p = toupper(*p);
                }
                
                udev_enumerate_unref(enumerate);
                udev_unref(udev);
                return; // 成功通过udev获取，直接返回
            }
            udev_enumerate_unref(enumerate);
        }
        udev_unref(udev);
    }
    #endif // HAVE_LIBUDEV
     
    /*********** 测试命令 *********************
    udevadm info --export-db | grep -A 20 "SUBSYSTEM=usb" | grep -A 20 "DEVTYPE=usb_device"
    ********************************************/
    // 如果udev不可用或获取失败，尝试使用sysfs方式  
    char command[256];    // 构建udevadm命令
    snprintf(command, sizeof command, 
             "udevadm info -q property -n /dev/%s 2>/dev/null", 
             portName);
    
    // 执行命令并读取输出
    FILE *fp = popen(command, "r");
    if (!fp) 
        return;
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0'; // 移除换行符
        
        // 解析VID
        if (strncmp(line, "ID_VENDOR_ID=", 13) == 0 && retVID) {
            memcpy(retVID, line + 13, 4);
            retVID[4] = '\0';
            for (char *p = retVID; *p; p++) *p = toupper(*p);
        }
        // 解析PID
        else if (strncmp(line, "ID_MODEL_ID=", 12) == 0 && retPID) {
            memcpy(retPID, line + 12, 4);
            retPID[4] = '\0';
            for (char *p = retPID; *p; p++) *p = toupper(*p);
        }
        // 解析REV
        else if (strncmp(line, "ID_REVISION=", 12) == 0 && retREV) {
            memcpy(retREV, line + 12, 4);
            retREV[4] = '\0';
            for (char *p = retREV; *p; p++) *p = toupper(*p);
        }
    }
    
    pclose(fp);
}

#else

// 获取设备属性
static LPTSTR GetDeviceProperty(HDEVINFO hDevInfo, PSP_DEVINFO_DATA pDevInfoData, DWORD Property)
{
  static TCHAR buffer[1024];
  DWORD nSize = 0, dataType = 0;
  memset(buffer, 0, sizeof buffer);
  // 第一次调用获取所需缓冲区大小
  if (!SetupDiGetDeviceRegistryProperty(hDevInfo, pDevInfoData, Property, NULL, NULL, 0, &nSize))
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
      return NULL;
  
  // 检查是否需要缓冲区超出静态数组大小
  if (nSize > sizeof buffer)
    return NULL;

  // 第二次调用获取实际数据
  if (!SetupDiGetDeviceRegistryProperty(hDevInfo, pDevInfoData, 
    Property, &dataType, (PBYTE)buffer, sizeof buffer, NULL))
      return NULL;
  
  return buffer;
}

// 获取COM串口设备VID和PID，VID和PID长度大概在5个字符，可以给多一点
static void get_COM_VID_PID_REV(const char* portName, char *retVID, char *retPID, char *retREV)
{
  if( portName == NULL )
    return;

  // 获取所有端口设备信息
  HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
  if (hDevInfo == INVALID_HANDLE_VALUE) {
      SafePrintf("SetupDiGetClassDevs failed. Error: %ld\n", GetLastError());
      return;
  }

  SP_DEVINFO_DATA devInfoData;
  devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
  // 枚举所有端口设备 
  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
    // 获取设备友好名称，并 检查是否是指定的串口
    LPTSTR DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
    if (DeviceInfo == NULL || _tcsstr(DeviceInfo, portName) == NULL )
      continue;
    
    #if 0
    SafePrintf("Found port: %s\n", DeviceInfo);
    // 获取设备描述
    DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_DEVICEDESC);
    if (DeviceInfo != NULL) 
        SafePrintf("Device Description: %s\n", DeviceInfo); 

    // 获取制造商信息
    DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_MFG);
    if (DeviceInfo != NULL) 
        SafePrintf("Manufacturer: %s\n", DeviceInfo);

    // 获取硬件ID
    DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_HARDWAREID);
    if (DeviceInfo != NULL) 
        SafePrintf("Hardware ID: %s\n", DeviceInfo);
    SafePrintf("\n");

    for (uint8_t j = 0; j < SPDRP_MAXIMUM_PROPERTY; j++) { 
      DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, j);
      if (DeviceInfo != NULL) 
          SafePrintf("DeviceInfo 0x%02X: %s\n", j, DeviceInfo);
    }
    SafePrintf("\n");
    #endif

    // 获取硬件ID
    DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_HARDWAREID);
    if (DeviceInfo != NULL) {   // 从硬件ID中提取VID和PID 
        TCHAR* vidPos = _tcsstr(DeviceInfo, _T("VID_"));
        TCHAR* pidPos = _tcsstr(DeviceInfo, _T("PID_"));
        TCHAR* revPos = _tcsstr(DeviceInfo, _T("REV_"));
        if( retVID )
          memcpy(retVID, vidPos? vidPos + 4 :"NULL", 4);
        if( retPID )
          memcpy(retPID, pidPos? pidPos + 4 :"NULL", 4);
        if( retREV )
          memcpy(retREV, revPos? revPos + 4 :"NULL", 4);
    }
    
    break;
  }

  if (GetLastError() != NO_ERROR && GetLastError() != ERROR_NO_MORE_ITEMS)
      SafePrintf("SetupDiEnumDeviceInfo failed. Error: %ld\n", GetLastError());

  SetupDiDestroyDeviceInfoList(hDevInfo);
}

#endif