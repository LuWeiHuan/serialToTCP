/******************************************************************************
  * @file    文件 COM.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 串口操作业务功能
  ******************************************************************************
  * @attention 注意
  *
  * 
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "COM.h"
#include "COMinfo.h"
#include "COMAutoReOpen.h"
#include "main.h"
#include "commonUtils.h"
#include "log.h"
#include "clients.h"
#include "TrafficStats.h"
#include "Queue.h"
#include "configSave.h"

#include <stdio.h>
#include <string.h>

#ifdef __linux
#include <errno.h>
#include <unistd.h> 
#include <fcntl.h>
#include <termios.h>
#include <sys/socket.h>
#endif

/*================== 本地数据类型     =========================================*/
typedef struct {
#ifdef _WIN32
    DCB dcb;           // Windows专用
    void *hCom;
#else
    int hCom;
    long speedMbps;
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
static void HandleReceivedData(const uint8_t *data, uint32_t len);
static uint32_t handleAlignReceivedData(const char *newData, uint32_t bytesRead);


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

  loadAutoReOpenCOMConfig();  
}

void CloseComPort(const char *reason) 
{ 
  if (comPort.isOpen == false) {
    SafePrintf("%s is Close. reason:%s\n", comPort.portName, reason? reason:"unknown");
    LeaveCriticalSection_Wrapper(&csComPort);
    return;
  }
  
  EnterCriticalSection_Wrapper(&csComPort);
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
  
  // 促使接收串口数据Thread Exit
  char *hThreadCloseInfo = "External Call";
  bool isSelfCall = (comPort.threadId == GetCurrentThreadId_Wrapper());
  if (comPort.hThread && isSelfCall == false) { // 其它地方关闭，非线程关闭
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
// 成功返回0，失败返回负数
int8_t OpenComPort(const char* portName, uint32_t baudRate, 
        uint8_t dataBits, uint8_t stopBits, uint8_t parity)
{
  EnterCriticalSection_Wrapper(&csComPort);

  bool retExists = COM_PortExists(portName);
  if( retExists == false ){
    LeaveCriticalSection_Wrapper(&csComPort);
    return -1;
  }
  
  char fullPortName[20];
  memset(fullPortName, 0, sizeof fullPortName);
  snprintf(fullPortName, sizeof fullPortName, "\\\\.\\%s", portName);

  comPort.hCom = CreateFileA(fullPortName, 
    GENERIC_READ | GENERIC_WRITE,
    0,
    NULL,
    OPEN_EXISTING,
    FILE_FLAG_OVERLAPPED, // FILE_FLAG_OVERLAPPED 异步模式，同步模式写0
    NULL);

  if (comPort.hCom == INVALID_HANDLE_VALUE) {
    LeaveCriticalSection_Wrapper(&csComPort);
    return -2;
  }

  memset(&comPort.dcb, 0, sizeof comPort.dcb);
  comPort.dcb.DCBlength = sizeof comPort.dcb;
  if (!GetCommState(comPort.hCom, &comPort.dcb)) {
    CloseHandle(comPort.hCom);
    comPort.hCom = INVALID_HANDLE_VALUE;
    LeaveCriticalSection_Wrapper(&csComPort);
    return -3;
  }

  comPort.dcb.BaudRate = baudRate;        // 波特率（如 9600, 115200）
  comPort.dcb.ByteSize = (BYTE)dataBits;  // 数据位（5,6,7,8）
  comPort.dcb.StopBits = stopBits == 1 ? ONESTOPBIT : TWOSTOPBITS; // 停止位（1 或 2）
  comPort.dcb.Parity = (BYTE)parity;      // 校验位（0=NONE, 1=ODD, 2=EVEN, 3=MARK, 4=SPACE）
  
  // 必须设置的标志位
  comPort.dcb.fBinary = true;             // 必须为 TRUE（Win 串口仅支持二进制模式）
  comPort.dcb.fOutxCtsFlow = false;       // 禁用 CTS 流控
  comPort.dcb.fOutxDsrFlow = false;       // 禁用 DSR 流控
  comPort.dcb.fDtrControl = DTR_CONTROL_ENABLE; // DTR 信号控制
  comPort.dcb.fRtsControl = RTS_CONTROL_ENABLE; // RTS 信号控制
  comPort.dcb.fOutX = false;              // 禁用 XON/XOFF 输出流控
  comPort.dcb.fInX = false;               // 禁用 XON/XOFF 输入流控
  comPort.dcb.fErrorChar = false;         // 禁用错误替换字符
  comPort.dcb.fNull = false;              // 禁止丢弃 NULL 字节
  comPort.dcb.fAbortOnError = false;      // 发生错误时不终止读写操作

  if (!SetCommState(comPort.hCom, &comPort.dcb)) {
    CloseHandle(comPort.hCom);
    comPort.hCom = INVALID_HANDLE_VALUE;
    LeaveCriticalSection_Wrapper(&csComPort);
    return -4;
  }

  // 设置超时
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

  // 创建串口读取线程
  comPort.hThread = threadCreate(&comPort.threadId, ComRecvDataThread, &comPort);
  if (comPort.hThread == (thread_t)0) {
    CloseHandle(comPort.hCom);
    comPort.hCom = INVALID_HANDLE_VALUE;
    comPort.isOpen = false;
    LeaveCriticalSection_Wrapper(&csComPort);
    return -5;
  }
  
  LeaveCriticalSection_Wrapper(&csComPort);
  return 0;
}

#else
int8_t OpenComPort(const char* portName, uint32_t baudRate, 
        uint8_t dataBits, uint8_t stopBits, uint8_t parity)
{
    bool retExists = COM_PortExists(portName);
    if( retExists == false )
      return -1;
    

    char fullPortName[32];
    snprintf(fullPortName, sizeof(fullPortName), "/dev/%s", portName);
    
    comPort.hCom = open(fullPortName, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (comPort.hCom < 0) {
        SafePrintf("Failed to open %s: %s\n", fullPortName, strerror(errno));
        return -2;
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
        default: speed = B921600; break;
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


/*================== 串口接收线程 ============================================*/
static threadRet WINAPI ComRecvDataThread(void *param)
{
  (void)param;
  static char comRecvBuffer[1024*20];
  const char *ThreadExitReason = "Thread Exit";

  #ifdef __linux 
  comPort.speedMbps = get_usb_speed_mbps(comPort.portName);
  SafePrintf("Linux USB %s Speed of : %ld Mbps\n", 
      comPort.portName, comPort.speedMbps);
  #endif

  // 清空积攒的对齐数据
  uint32_t totalBytes = handleAlignReceivedData((char*)0xFFFFFFFF, 0xFFFFFFFF);

#ifdef _WIN32
  DWORD bytesRead = 0;
  OVERLAPPED overlapped = {0};
  bool readRet;
  DWORD lastUpdateTime = 0, currentTickCount = 0;
  const DWORD updateInterval = 1500;

  while (comPort.isOpen) {
    if (bytesRead == 0 || totalBytes == 0) 
        Sleep(1);  // 1ms也能使CPU占用降低，不然CPU占用不小
    
    CloseHandle(overlapped.hEvent);

    // 重置重叠结构
    memset(&overlapped, 0, sizeof overlapped);
    overlapped.hEvent = CreateEvent(NULL, true, false, NULL);
    
    // 发起异步读取
    readRet = ReadFile(comPort.hCom, comRecvBuffer, 
                      sizeof(comRecvBuffer) - 1, &bytesRead, &overlapped);
    if (!readRet) {
      DWORD error = GetLastError();
      if (error == ERROR_IO_PENDING) {  // 等待读取完成或超时
        DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 1000); 
        totalBytes = handleAlignReceivedData(NULL, 0);
        
        if (waitResult == WAIT_TIMEOUT) {
            updataConsoleTitle("COM: Timeout");
            continue;
        } 
        else if (waitResult == WAIT_OBJECT_0) {   // 读取完成
          if (!GetOverlappedResult(comPort.hCom, &overlapped, &bytesRead, false)) {
            error = GetLastError();
            if (error != ERROR_OPERATION_ABORTED) {
              ThreadExitReason = getPrintf("Thread Exit[B], %s read error:%ld", 
                  comPort.portName, error);
              break;
            }
          }
        }
      } 
      else if (error != ERROR_OPERATION_ABORTED) {
        ThreadExitReason = getPrintf("Thread Exit[A], %s read error:%ld", 
            comPort.portName, error);
        break;
      }
    }

    if (bytesRead == 0) {   // 处理接收到的数据如果是空读取就重新读
        totalBytes = handleAlignReceivedData(NULL, 0); // 零读取处理
        
        // 按间隔更新线程状态
        currentTickCount = GetTickCount();  // 按间隔更新线程状态
        if (currentTickCount - lastUpdateTime >= updateInterval) {
            updataConsoleTitle(comPort.portName);
            lastUpdateTime = currentTickCount;
        }
        continue;
    }

    // 处理接收到的数据 
    totalBytes = handleAlignReceivedData(comRecvBuffer, bytesRead);
    bytesRead = 0;
  }

#else
  fd_set readSet;
  struct timeval timeout;
  
  while ( true ) { 
    if( comPort.isOpen == false ){
      ThreadExitReason = getPrintf("COM closed");
      break;
    }

    FD_ZERO(&readSet);
    FD_SET(comPort.hCom, &readSet);
    
    // 动态设置超时：如果有积攒的对齐数据，使用较短超时
    timeout.tv_sec = 0; 
    // 50us 短超时，用于快速检测，200ms 正常超时
    timeout.tv_usec = totalBytes ?50 : 200*1000;
    
    int selectRet = select(comPort.hCom + 1, &readSet, NULL, NULL, &timeout);
    if (selectRet == 0) { // 超时处理 
        totalBytes = handleAlignReceivedData(NULL, 0);
        continue;
    } 
    else if (selectRet == -1) {
        if (errno == EINTR) 
          continue;
        ThreadExitReason = getPrintf("Select error: %s", strerror(errno));
        break;
    } 
    else {
      // select返回大于0，表示有文件描述符就绪
      int fdRet = FD_ISSET(comPort.hCom, &readSet);
      if (!fdRet) { 
          totalBytes = handleAlignReceivedData(NULL, 0);
          SafePrintf("COM FD_ISSET ERROR:%d\n", fdRet);
          continue;
      }
    }
    
    // 读取数据
    ssize_t bytesRead = read(comPort.hCom, comRecvBuffer, sizeof comRecvBuffer - 1);
    if (bytesRead > 0) {
        totalBytes = handleAlignReceivedData(comRecvBuffer, bytesRead);
    }
    else if (bytesRead == 0) {    // EOF - 设备断开 
      ThreadExitReason = getPrintf("Device disconnected (EOF): %s", strerror(errno));
      break;
    }
    else { // 读取错误
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        totalBytes = handleAlignReceivedData(NULL, 0);
      } 
      else {
        ThreadExitReason = getPrintf("Read error: %s", strerror(errno));
        break;
      }
    }
  }

#endif
  
  // 线程退出前发送所有积攒的数据
  for(uint8_t i=0; i<10 && handleAlignReceivedData(NULL, 0); i++);
  
  CloseComPort(ThreadExitReason);
#ifdef _WIN32
  CloseHandle(overlapped.hEvent);
#endif
      
  return (threadRet)0;
}

static void handleReceivedDataAuotAsync(const char *comRecvBuffer, uint32_t len)
{
  if( comRecvBuffer == NULL || len == 0 )
    return;
  
  if( AddDataToAsyncQueue(&asyncRecvQueue, (uint8_t*)comRecvBuffer, len) == false )   
    HandleReceivedData((uint8_t*)comRecvBuffer, len);
}

/**
 * @brief 处理接收到的数据，包含数据对齐逻辑
 * @param newData 新接收到的数据
 * @param length  新数据长度
 * @param lineNumber  传入行号可用于调试，空的则是清空积攒数据
 * @return 已经积攒的数据
 * @attention 当 newData 和 bytesRead 都是 0xFFFFFFFF 的时候清空积攒对齐的数据并退出
 * 当下函数对于处理全是对齐数据包的效果，误打误撞得实现的好像还不错。
 * 串接收数据对齐接收问题，实现业务逻辑有以下几点：
    1. 不是对齐数据包立即发送出去，如果之间已经积攒了对其数据连同对其数据一起发送出去，这两个功能最容易实现。
    2. 是对齐数据包积攒起来，超过缓冲区和限定最大积攒发送出去，这两个功能也最容易实现。
    3. 对只有齐数据包的情况且，这种就是全是对齐数据包，每个数据包之间会出现“空转”无数据调用本函数，也是本函数最主要且最大难点。
       每个环境空转次数都不一样，目前观察到下面几个环境的空转次数如下：
       Win7 空转5~6次还算稳定；
       Win10 空转2~3次，甚至因为性能问题，空转指数基本就1~2次；
       Linux 空转最大能做到25左右，也可以比较稳定空转5次。
    4. 对齐数据判断条件只有两个，一是 总数据 ÷ 128 余数为0，即可判定为对齐数据包，总数据 = 积攒对齐 + 新长度。
        二是对于Linux系统会出现4095这种奇怪数据包，也判定为对齐数据包。
    5. 已经积攒了数据的情况下，Linux要修改struct timeval缩短接收超时，
        以便增加“空转”次数，用来判断是否发送已经积攒的数据包，这个由外部实现，不在不本函数内实现范围内。
    6. 针对单包对齐数据包的发送延时问题，暂时没有很好的解决方法，
       只能通过增加“空转”次数来尽量减少延时，如果全是单包建议关闭对齐发送功能。
    发散性思路实现全是对齐数据包处理方法：
    1. 有一种音乐律动效果，是根据音量大小柱子会变搞或变低，有一条像素会逐渐递减。
       想过用空转增加柱子高低，那一条像素逐渐接近底部的时候把所有积攒数据发送出去。不知道好不好实现。
    2. 记录每一个对齐数据包接收的时间戳，计算时间差，如果时间差超过某个值就发送出去。
       这个方法实现起来比较复杂，而且时间差值和性能上调度上也不好把握。
 */
static uint32_t handleAlignReceivedData(const char *newData, uint32_t length)
{  
  if( saveInfo.COMalignedRecv4K == 0){ // 禁用限定阈值就直接发送
    handleReceivedDataAuotAsync(newData, length);
    return 0;
  }
  
  static uint32_t totalBytes = 0;             // 已积攒的总字节数
  static uint8_t LinuxRecv4095Num = 0;        // Linux系统会出现4095这种奇怪对齐
  static char totalBuffer[RECV_BUFFER_SIZE];  // 积攒对齐缓冲区
  static const uint32_t buffMax = sizeof totalBuffer - 10000;

  if ( newData == (char*)0xFFFFFFFF && length == 0xFFFFFFFF){
    memset(totalBuffer, 0, sizeof totalBuffer);
    totalBytes = LinuxRecv4095Num = 0;
    return 0;
  }
  
  // 自动获取连续空读次数阈值，方法不一定好用，只是保留着当备用
  static bool infiniteZero = false;   // 是否进入无限空读状态
  static uint8_t zeroNumCount = 0, autoZeroMax = 10, cmpZeroMax = 0;
  if(infiniteZero == false)
    autoZeroMax = ( length && zeroNumCount > 1)? zeroNumCount : autoZeroMax;
  zeroNumCount = length==0? zeroNumCount + 1 :0;
  if( zeroNumCount >= 10 ){
    zeroNumCount = 10;
    infiniteZero = true;
  }
  if( length )
    infiniteZero = false;
  // if (infiniteZero == false )
  //   SafePrintf("COM Zero %d/%d, length：%-10d\n", zeroNumCount, autoZeroMax, length);

 uint32_t sumBytes = totalBytes + length;
  bool condition[3];
  static uint8_t zeroNumMax = 3;      // 这个固定3~4的方法表现还怪好的，很奇怪
  static uint8_t autoZeroCount = 0;   // 连续零读取计数
  autoZeroCount = (totalBytes && length==0 ? autoZeroCount+1 : 0);
  condition[0] = autoZeroCount >= zeroNumMax;         // 积攒数据且多次空读 
  condition[1] = saveInfo.COMalignedRecv4K < sumBytes; // 超过积攒阈值，发送数据
  condition[2] = totalBytes > buffMax;                // 缓冲区即将满，必须发送
  
  if( condition[1] || condition[2] )      // 超过阈值发送数据，空读次数降低要求
      autoZeroMax = 3;
  
  // 如果有积攒的数据且长时间无新数据，强制发送
  if ( condition[0] || condition[1] || condition[2]){

    char Recv4095NumSrting[30] = " ";
    #ifdef __linux
    if( LinuxRecv4095Num )
      snprintf(Recv4095NumSrting, sizeof Recv4095NumSrting,
          ", Linux Recv 4095 Num:%-5d", LinuxRecv4095Num );
    #endif
    SafePrintf("%sOM Aligned Recv%s%s: %-6u/%-6u/%-6u Bytes, %s:%d/%d/%-2d%s%-33s\n", 
        condition[1]? "\nC":"C", condition[2]? " Fill":" ",
        condition[1]? " Restrict":" ", totalBytes, saveInfo.COMalignedRecv4K, buffMax,  
        condition[0]? "ZERO":"Zero", autoZeroCount, zeroNumMax, autoZeroMax,
        cmpZeroMax != zeroNumCount? " NEW":" ", Recv4095NumSrting);
    handleReceivedDataAuotAsync(totalBuffer, totalBytes);
    totalBytes = LinuxRecv4095Num = 0;
    if( cmpZeroMax != zeroNumCount )
      cmpZeroMax = zeroNumCount;
  }

  // 如果没有新数据，只返回当前积攒的数据量 *****************************************
  if (newData == NULL || length == 0)
      return totalBytes;
  
  // 立即积攒数据，对于对齐数据，继续积攒等待更多数据或超时
  memcpy(totalBuffer + totalBytes, newData, length);
  totalBytes += length;
 
#ifdef __linux
  if( length == 4095 ){ // 情况很少见，但也有
    LinuxRecv4095Num++;
    sumBytes += LinuxRecv4095Num;
  }
  // 低速USB，降低对齐要求
  uint16_t aligningValue = (comPort.speedMbps < 480 ) ? 128:1024;
#else
  static const uint16_t aligningValue = 4096;
#endif

 // 非对齐数据且有积攒数据，立即发送
 if (sumBytes % aligningValue != 0 && totalBytes ) {
    handleReceivedDataAuotAsync(totalBuffer, totalBytes);
    totalBytes = LinuxRecv4095Num = 0; 
  } 
  return totalBytes;
}



/**
 * @brief 处理串口接收到的数据并转发给 TCP 客户端
 *
 * 功能说明：
 * - 若 `comRecvBuffer` 为 NULL 或 `len` 为 0，函数立即返回（不做处理）。
 * - 在启用流量统计时更新收到字节数统计。
 * - 若当前没有客户端连接则直接返回，不进行发送。
 * - 支持“独占接收”模式（由 `runInfo.monopolizeComRecvIndex` 指定）：
 *   - 若无独占索引，向所有已连接客户端广播该数据；
 *   - 若有独占索引，尝试仅向该客户端发送；若该客户端不存在或已断开，则取消独占并回退为广播。
 * - 根据 `saveInfo.serverPrintData` 控制是否在控制台打印原始数据或十六进制内容；
 * 同时打印发送摘要信息（时间戳、方向、每客户端发送长度/总长度、发送成功或失败等）。
 *
 * 注意事项：
 * - 本函数不进行阻塞式等待客户端确认，`sendDataToClients` 返回已写入的字节数，可能小于期望发送总字节数；
 * - 函数会读取/修改若干全局状态（例如 `comPort.sendCount`、`runInfo`、`saveInfo`），
 * 调用时应在串口接收线程上下文或保证并发安全；
 * - 函数不会关闭串口；发生发送失败时上层会根据需要处理客户端连接状态。
 *
 * @param comRecvBuffer 指向收到的数据缓冲区（只读），可以为 NULL。
 * @param len           缓冲区有效字节长度（字节数）。
 */
static void HandleReceivedData(const uint8_t *comRecvBuffer, uint32_t len)
{
  if( comRecvBuffer == NULL || len == 0 )
    return;

#ifdef __TRAFFIC_STATS_H_
  trafficStats.com.totalBytesReceived += len;
#endif
    
    int sendRet = 0;
    do {
        if( getClientNum() == 0 ) // 没有客户端不发送数据 
            break;

        if (runInfo.monopolizeComRecvIndex == NULL ){ // 没有独占就广播
            sendRet = sendDataToClients(NULL, (char*)comRecvBuffer, len);
            break;
        }
        
        // 获取独占客户端套接字，获取不到就剔除独占广播发送
        const socket_t *socket = getClientSocket( *runInfo.monopolizeComRecvIndex );
        if( socket == NULL || *socket == INVALID_SOCKET_VALUE ){
            runInfo.monopolizeComRecvIndex = NULL;
            continue; // 广播这条数据
        }
          
        sendRet = sendDataToClients(socket, (char*)comRecvBuffer, len);
        if (sendRet > 0 ) break;

        // 如果发送失败 且 检查独占客户端不存在的情况下，取消独占并改为广播发送。
        bool Exist = getClientIndex(socket, NULL);
        // SafePrintf("Monopolize Client Send Fail, Ready Switching To All Clients."
        //         "sendRet:%d/%ld Exist:%s\n", sendRet, len, Exist? "YES":"NO");
        if( Exist )
            break;
        runInfo.monopolizeComRecvIndex = NULL;  // 标记独占客户端已经下线
        continue; // 广播这条数据
    } while (0);
    
    char *Direct = getSendRecvDirectionStr("[COM --> TCP]", 0);
    char *timeStr = getCurrentTimeStringSec();
    
    int ClientNum = getClientNum();
    int lenSum = runInfo.monopolizeComRecvIndex ? len : len * ClientNum;
    int oneLen = ClientNum == 0 ? 0 : sendRet / ClientNum;  // 除数为0时，程序会出错误
    
    if( runInfo.monopolizeComRecvIndex )
        oneLen = sendRet;

    char isEnter = ( ( saveInfo.serverPrintData == 0 || saveInfo.serverPrintData == 3) && 
          ( ClientNum == 0 || saveInfo.COMrecvPoll == false)) ? '\r':'\n';

    SafePrintf( "%-21s%10" PRIu64 " [%s]  %-6d/%-6u Byte (%s : %d)%s %c",
        timeStr, ++comPort.sendCount, Direct, oneLen, len,
        (sendRet == lenSum)? "OK":"Fail", lenSum - sendRet,
        saveInfo.serverPrintData? " data:":" ", isEnter);
    
    if (saveInfo.serverPrintData == 0) 
        return;
    
    if (saveInfo.serverPrintData == 1)
        SafePrintf("%s", comRecvBuffer);
    if (saveInfo.serverPrintData == 2)
        printHex((uint8_t*)comRecvBuffer, len, 40, 2);
}

// 串口阻塞形发数据
static uint32_t ComPortTrueSendData(uint8_t const *tcpRecvBuffer, int bytesReceived, uint32_t *retError)
{
    EnterCriticalSection_Wrapper(&csComPort);
    DWORD bytesWritten = 0, error = 0;

    // 处理异步写入
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
    bytesWritten = (n >= 0)? n:0;
    error = errno; 
#endif
    
    LeaveCriticalSection_Wrapper(&csComPort);
    
    #ifdef __WIN32
    if (error == ERROR_BAD_COMMAND || // 当串口拔掉后错误值是22
        error == ERROR_OPERATION_ABORTED || 
        error == ERROR_INVALID_HANDLE) { 
    #else
    if (error != 0) {
    #endif
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
    
    // 使用异步队列发送数据
    if( asyncSendQueue.running &&
        AddDataToAsyncQueue(&asyncSendQueue, tcpRecvBuffer, bytesReceived) ) {
        if (retError) // 返回成功添加，实际发送由线程处理
            *retError = 0;
        return bytesReceived;
    }
    
    return ComPortTrueSendData((uint8_t*)tcpRecvBuffer, bytesReceived, retError);
}

static void COMAsyncSendQueueCallBack(uint8_t *data, uint32_t len)
{
    if (comPort.isOpen == false) 
        return;

    // 发送数据到串口
    uint32_t error = 0;
    uint32_t bytesWritten = ComPortTrueSendData(data, len, &error);
    
    if (bytesWritten != len) 
        SafePrintf("COM Async send error: written %u/%u bytes, error: %u\n", 
                  bytesWritten, len, error);
}

// 启用异步发送数据到COM口，传入0代表关闭异步发送，大于10代表启动异步发送
bool COM_UseAsyncSend(uint16_t num)
{
    if( num < MIN_QUEUE_SIZE ){
        FreeAsyncQueue(&asyncSendQueue);
        return num == 0 ? true : false;
    }
    
    return startAsyncQueue(&asyncSendQueue, 
        COMAsyncSendQueueCallBack, num, RECV_BUFFER_SIZE, "COM Send");
}

// 启用异步接收数据到COM口，传入0代表关闭异步发送，大于10代表启动异步发送
static void COMAsyncRecvQueueCallBack(uint8_t *data, uint32_t len)
{
    HandleReceivedData(data, len);
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
