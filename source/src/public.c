/******************************************************************************
  * @file    文件 public.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 公共资源
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/


#include "public.h"
#include "main.h"
#include "COM.h"
#include "TrafficStats.h"
#include "clients.h"

#include <stdio.h>
#include <time.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <wchar.h>
#include <windows.h>
#else
#include <unistd.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <stdlib.h>
#include <arpa/inet.h>
#endif



/*================== 全局共享变量    ========================================*/
runInfo_t  runInfo = {
  .startTimeSec = 0,
  .monopolizeComRecvIndex = NULL,
  .monopolizeComSendIndex = NULL,
};

// 获取从运行到现在的间戳（毫秒）程序运行要调用一次
// 这个函数会由于 mian 函数之前执行
__attribute__((constructor)) uint64_t getRuningTimeMs(void) 
{
  static uint64_t initialTimeMs = 0;
  
#ifdef _WIN32
    struct _timeb timebuffer; 
    _ftime_s(&timebuffer);
    timebuffer.time *= 1000;
 
    if( initialTimeMs == 0 ){
        initialTimeMs = timebuffer.time + timebuffer.millitm;
        runInfo.startTimeSec = time(NULL);
    }
    
    uint64_t currentTimeMs = timebuffer.time + timebuffer.millitm; 
#else
    struct timespec ts; 
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t currentTimeMs = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    
    if (initialTimeMs == 0) {
        initialTimeMs = currentTimeMs;
        runInfo.startTimeSec = time(NULL);
    } 
#endif

  return currentTimeMs - initialTimeMs;
}

time_t getCurrentTimeSec(void)
{
    return time(NULL) - runInfo.startTimeSec;
}

// 更新标题栏内容
void updataConsoleTitle(const char *threadName)
{
    time_t currentTimeSec = getCurrentTimeSec();
    uint16_t day = currentTimeSec / 86400;
    uint8_t hour = currentTimeSec / 3600 % 24;
    uint8_t min  = currentTimeSec / 60 % 60;
    uint8_t sec  = currentTimeSec % 60; 
    char title[150];
    memset(title, 0, sizeof title); 

    DWORD theradID = GetCurrentThreadId_Wrapper();
#ifdef __TRAFFIC_STATS_H_
    if( trafficStats.run && currentTimeSec % 6 < 3 )
        snprintf(title, sizeof title, "串口转TCP     串口:↑ %s  ↓ %s   网络：↑ %s  ↓ %s    线程%ld：%s",
            trafficStats.com.recvRate, trafficStats.com.sendRate,
            trafficStats.net.sendRate, trafficStats.net.recvRate,
            theradID, threadName != NULL ? threadName : "No thread Name");
    else
#endif
        snprintf(title, sizeof title, "串口转TCP     服务端口号：%d   "
            "已运行%d天：%02d:%02d:%02d  客户端：%d/%d  线程%ld：%s",
            getMainServerPort(), day, hour, min, sec, getClientNum(), getMaxClient(), 
            theradID, threadName != NULL ? threadName : "No thread Name");

#ifdef _WIN32    
    SetConsoleTitleA( title );
#else

#if !defined(__aarch64__) && !defined(__arm__)
    // 获取终端类型
    char* term = getenv("TERM");
    // 检查终端是否支持标题设置（排除不支持的情况）
    if (term != NULL) {
        // 这些终端通常支持标题设置
        if (strstr(term, "xterm") != NULL ||
            strstr(term, "rxvt") != NULL ||
            strstr(term, "screen") != NULL ||
            strstr(term, "tmux") != NULL) {
            
            printf("\033]0;%s\007", title);
            fflush(stdout);
        }
        // 对于串口终端、linux终端、vt系列等，不设置标题
    }
#endif

 

#endif
}

char *getCurrentTimeStringSec(void) 
{
    static char timeStr[40];
    memset(timeStr, 0, sizeof timeStr);
    
#ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(timeStr, sizeof timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond);
#else
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(timeStr, sizeof timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);
#endif
    return timeStr;
}

void printBuildInfo(void) 
{ 
    uint8_t ipCount;
    char localIPs[25][20];  
    memset(localIPs, 0, sizeof localIPs); 
    GetAllLocalIPs(localIPs, &ipCount, 25);
    
    printf("========================================\n");
    printf("  Program    : %s\n", "串口转TCP服务端");
#ifndef CLOSE_EXCEPTION_MONITOR
    printf("  Version    : %s  Debug\n", VERSIONS);
#else
    printf("  Version    : %s  Release\n", VERSIONS);
#endif

    printf("  Build Date : %s %s\n", __DATE__, __TIME__);
#ifdef __GNUC__
    printf("  Compiler   : GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#endif
    
#ifdef _WIN32
    char getversions[5] = "NULL";
    getWindowsVersionSimple(getversions);
    printf("  Platform   : Windows %s\n", getversions);
#else
    printf("  Platform   : Linux\n");
#endif
    printf("  UUID       : %s\n", GetSystemUniqueIdentifier()); 
    printf("  host Name  : %s\n", getComputerFullName());
    for (uint8_t i = 0; i < ipCount; i++) 
        printf("  IP addr  %d : %s\n", i+1, localIPs[i]); 
    printf("========================================\n\n");
}

// 获取所有本地IP地址
void GetAllLocalIPs(char ips[][20], uint8_t *count, uint8_t num)
{
    if( count == NULL )
      return;

    *count = 0;

#ifdef _WIN32
    char hostname[256];
    if (gethostname(hostname, sizeof hostname) == SOCKET_ERROR)
        return;

    struct hostent* hostinfo = gethostbyname(hostname);
    if (hostinfo == NULL) 
        return;
    
    struct in_addr addr;
    for (uint8_t i = 0; hostinfo->h_addr_list[i] != NULL && *count < num; i++) {
      memcpy(&addr, hostinfo->h_addr_list[i], sizeof(struct in_addr));
      if (strcmp(inet_ntoa(addr), "127.0.0.1") == 0) 
        continue;
      strncpy(ips[*count], inet_ntoa(addr), 16);
      (*count)++;
    }
#else
    struct ifaddrs *ifaddr, *ifa;
    
    if (getifaddrs(&ifaddr) == -1) 
        return;
    
    for (ifa = ifaddr; ifa != NULL && *count < num; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        
        if (ifa->ifa_addr->sa_family == AF_INET) { // IPv4
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            char *ip = inet_ntoa(sa->sin_addr);
            
            if (strcmp(ip, "127.0.0.1") != 0) {
                strncpy(ips[*count], ip, 16);
                (*count)++;
            }
        }
    }
    
    freeifaddrs(ifaddr);
#endif
}

/*
获取收发方向字符串
 direct 参数如下是如下字符串
   [COM --> TCP]
   [TCP --> COM]

  index 客户端索引号
*/
char *getSendRecvDirectionStr(const char *direct, uint8_t index)
{
    char *endptr;
    uint8_t comNum = 0;
    const char *portName = getComName();
    if ( portName[0] != '\0') {
        if (strncmp(portName, "COM", 3) == 0) {
            comNum = strtol(portName + 3, &endptr, 10);
        } else if (strncmp(portName, "tty", 3) == 0) {
            // Linux 串口名如 ttyS0, ttyUSB0
            if (strncmp(portName, "ttyS", 4) == 0) {
                comNum = strtol(portName + 4, &endptr, 10);
            } else if (strncmp(portName, "ttyUSB", 6) == 0) {
                comNum = strtol(portName + 6, &endptr, 10);
            }
        }
    }

    static char retStr[50];
    memset(retStr, 0, sizeof retStr);
    
    if( strcmp(direct, "[TCP --> COM]") == 0 ){
        snprintf(retStr, sizeof retStr, "%-2d:%-16s==> COM%-3d", index, getClientIP(index), comNum);
    }
    else if( strcmp(direct, "[COM --> TCP]") == 0 ){
        static char clientString[32] = {0};
        memset(clientString, 0, sizeof clientString);
        if (runInfo.monopolizeComRecvIndex != NULL) 
            snprintf(clientString, sizeof clientString, "%-2d %s", 
                *runInfo.monopolizeComRecvIndex, getClientIP(*runInfo.monopolizeComRecvIndex));
        else if (getClientNum() == 0) 
            snprintf(clientString, sizeof clientString, " No client");
        else 
            snprintf(clientString, sizeof clientString, "All client %d", getClientNum());

        snprintf(retStr, sizeof(retStr), "COM%-3d==> %-19s", comNum, clientString);
    }
    else {
        strcpy(retStr, direct);
    }
    
    return retStr;
}

/**
 * 获取计算机全名（DNS全名）
 * 返回值：计算机名字符串
 */
const char *getComputerFullName(void) 
{
    static char computerName[256];
    memset(computerName, 0, sizeof computerName);

#ifdef _WIN32
    DWORD nameLen = 0;

    // 第一次调用获取所需缓冲区大小
    BOOL result = GetComputerNameEx(ComputerNameDnsFullyQualified, NULL, &nameLen);
    if (result == false && GetLastError() != ERROR_MORE_DATA){ 
        snprintf(computerName, sizeof computerName, "Unknown-Win");
        return computerName;
    }
    
    // 第二次调用获取实际名称
    result = GetComputerNameEx(ComputerNameDnsFullyQualified, computerName, &nameLen);
    if (result == false) { 
        snprintf(computerName, sizeof computerName, "Unknown-Win-Error");
        return computerName;
    }

    return computerName;
#else
    if (gethostname(computerName, sizeof computerName - 1) == 0)
        return computerName; 
    return "Unknown-Linux";
#endif
}



#ifdef _WIN32
#include <wchar.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

// Windows 控制台字体设置
bool SetConsoleFontSize(int width, int height) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_FONT_INFOEX fontInfo = {0};
    
    fontInfo.cbSize = sizeof fontInfo;
    fontInfo.dwFontSize.X = width;   // 字体宽度
    fontInfo.dwFontSize.Y = height;  // 字体高度
    fontInfo.FontFamily = FF_DONTCARE;
    fontInfo.FontWeight = FW_NORMAL;
    wcscpy(fontInfo.FaceName, L"Consolas"); // 字体名称
    
    return SetCurrentConsoleFontEx(hConsole, false, &fontInfo);
}

// 启用Windows 10 VT模式（支持ANSI转义序列）
bool EnableVTMode(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE)
        return false;
    
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode))
        return false;
    
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, dwMode))
        return false;
    
    return true;
}
#endif

#define defind CLOSE_EXCEPTION_MONITOR 0
// 这里是进行程序异常退出捕获测试的位置，用于程序自我错误定位测试
void ErrorCodeTest(void)
{
#if !defined(CLOSE_EXCEPTION_MONITOR) && 0
  uint32_t TimeMs = getRuningTimeMs() / 1000; 
  printf("开始错误代码测试，当前时间戳：%d sec\n", TimeMs);
  //Sleep(2000);
  if( TimeMs % 2 == 0 ) {
    int *ptr = NULL;
    *ptr = 42;  // 这里会导致段错误
  }
  else {
    for( int8_t i = -2; i < 2; i++)
      printf("开始异常除法运算 8 / %d = %d\n", i, 8/i);
  } 
#endif
}


// 获取系统唯一识别特征信息
const char* GetSystemUniqueIdentifier(void)
{
    static char systemUniqueID[128] = {0};  // 存储系统唯一标识符
    static bool initialized = false;
    
    if (initialized) 
        return systemUniqueID;

    memset(systemUniqueID, 0, sizeof systemUniqueID);
    
#ifdef _WIN32
    /* Windows 平台实现 */
    
    // 方法1: 尝试获取机器GUID (更可靠)
    HKEY hKey;
    DWORD dwType = REG_SZ;
    char buffer[128] = {0};
    DWORD bufferSize = sizeof(buffer);
    
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
                     "SOFTWARE\\Microsoft\\Cryptography", 
                     0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        initialized = RegQueryValueExA(hKey, "MachineGuid", NULL, &dwType, 
                           (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS;
        RegCloseKey(hKey); 
        if ( initialized ) {
            for(uint8_t j=0,i=0; i < strlen(buffer) && i< sizeof(systemUniqueID) - 1; i++ )
              if(buffer[i] != '-')
                systemUniqueID[j++] = buffer[i]; 
            return systemUniqueID;
        }
    }
    
#else
    /* Linux/Unix 平台实现 */
    
    // 方法1: 读取机器ID (适用于大多数Linux系统)
    FILE* fp = fopen("/etc/machine-id", "r");
    if (fp != NULL) {
        if (fgets(systemUniqueID, sizeof(systemUniqueID) - 1, fp) != NULL) {
            // 移除换行符
            systemUniqueID[strcspn(systemUniqueID, "\n")] = 0;
            fclose(fp);
            initialized = true;
            return systemUniqueID;
        }
        fclose(fp);
    }
    
    // 方法2: 读取产品UUID (适用于有DMI的系统)
    fp = fopen("/sys/class/dmi/id/product_uuid", "r");
    if (fp != NULL) {
        if (fgets(systemUniqueID, sizeof(systemUniqueID) - 1, fp) != NULL) {
            systemUniqueID[strcspn(systemUniqueID, "\n")] = 0;
            fclose(fp);
            initialized = true;
            return systemUniqueID;
        }
        fclose(fp);
    }
#endif
    
    // 如果所有方法都失败，使用默认标识符
    strcpy(systemUniqueID, "UnknownSystem");
    initialized = true;
    return systemUniqueID;
}

#ifdef _WIN32
#include <versionhelpers.h>  // 需要包含这个头文件
#endif

// 简单粗暴的Win版本获取
uint8_t getWindowsVersionSimple(char *retStr) 
{
  #ifdef _WIN32
  if (IsWindows10OrGreater()) {
      if (retStr) strcpy(retStr, "10+");
      return 10;
  } else if (IsWindows8Point1OrGreater()) {
      if (retStr) strcpy(retStr, "8.1+");
      return 9;
  } else if (IsWindows8OrGreater()) {
      if (retStr) strcpy(retStr, "8+");
      return 8;
  } else if (IsWindows7OrGreater()) {
      if (retStr) strcpy(retStr, "7+");
      return 7;
  } else if (IsWindowsVistaOrGreater()) {
      if (retStr) strcpy(retStr, "Vista+");
      return 6;
  } else {
      if (retStr) strcpy(retStr, "XP-");
      return 5;
  }
  #else
  if (retStr) strcpy(retStr, "linux");
  return 10;
  #endif
}
