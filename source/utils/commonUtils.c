/******************************************************************************
  * @file    文件 commonUtils.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 小工具类函数
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "commonUtils.h"
#include "main.h"
#include "COM.h"
#include "clients.h"
#include "configSave.h"
#include "log.h"
#include "TrafficStats.h"
#include "COMAutoReOpen.h"

#include <stdio.h>
#include <time.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <wchar.h>
#include <windows.h>
#include <shlwapi.h>
#else
#include <unistd.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <ctype.h>
#endif

/*================== 本地变量    ========================================*/
static time_t startTimeSec;               // 程序启动时间


time_t getCurrentTimeSec(void)
{
    return time(NULL) - startTimeSec;
}

/**
 * 获取系统已经运行的秒数（跨平台）
 * @return 系统已运行秒数，失败返回 0
 */
uint64_t getSystemUptimeSeconds(void) {
#ifdef _WIN32
    // Windows 平台：使用 GetTickCount()
    DWORD ms = GetTickCount();
    // 注意：GetTickCount 在 49.7 天后会归零
    // 需要更精确可改用 GetTickCount64()
    return ms / 1000;
#elif defined(__linux__)
    // Linux 平台：读取 /proc/uptime
    FILE *fp = fopen("/proc/uptime", "r");
    if (fp == NULL) 
        return 0;
    
    double uptime_sec;
    if (fscanf(fp, "%lf", &uptime_sec) != 1) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return (time_t)uptime_sec;
#else
    // 其他平台（如 macOS 等）不支持
    return 0;
#endif
}

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
        startTimeSec = time(NULL);  // 获取当前时间（从 1970-01-01 00:00:00 开始的秒数）
    }
    
    uint64_t currentTimeMs = timebuffer.time + timebuffer.millitm; 
#else
    struct timespec ts; 
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t currentTimeMs = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    
    if (initialTimeMs == 0) {
        initialTimeMs = currentTimeMs;
        startTimeSec = time(NULL);
    } 
#endif

  return currentTimeMs - initialTimeMs;
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
    if( currentTimeSec % 6 < 3 )
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
    GetLocalTime(&st);  // 获取本地时间

    // 格式化为 "YYYY-MM-DD HH:MM:SS"
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
    char localIPs[25][INET6_ADDRSTRLEN];  
    memset(localIPs, 0, sizeof localIPs); 
    getAllLocalIPs(localIPs, &ipCount, 25, false);
    
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


// 获取本机所有IP地址 - 支持IPv6
void getAllLocalIPs(char localIPs[][INET6_ADDRSTRLEN], uint8_t *ipCount, uint8_t maxIPs, bool includeIPv6)
{
    *ipCount = 0;
    
#ifdef _WIN32
    // Windows 使用 GetAdaptersAddresses
    // 这里简化处理，使用 getaddrinfo 方式
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        return;
    }
    
    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = includeIPv6 ? AF_UNSPEC : AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(hostname, NULL, &hints, &result) != 0) {
        return;
    }
    
    for (struct addrinfo* ptr = result; ptr && *ipCount < maxIPs; ptr = ptr->ai_next) {
        void* addr = NULL;
        int family = ptr->ai_family;
        
        if (family == AF_INET) {
            struct sockaddr_in* ipv4 = (struct sockaddr_in*)ptr->ai_addr;
            addr = &(ipv4->sin_addr);
        } else if (family == AF_INET6 && includeIPv6) {
            struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)ptr->ai_addr;
            addr = &(ipv6->sin6_addr);
        } else {
            continue;
        }
        
        if (addr) {
            inet_ntop(family, addr, localIPs[*ipCount], INET6_ADDRSTRLEN);
            (*ipCount)++;
        }
    }
    freeaddrinfo(result);
    
#else
    // Linux 使用 getifaddrs
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        return;
    }
    
    for (ifa = ifaddr; ifa && *ipCount < maxIPs; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
        
        int family = ifa->ifa_addr->sa_family;
        void* addr = NULL;
        
        if (family == AF_INET) {
            struct sockaddr_in* ipv4 = (struct sockaddr_in*)ifa->ifa_addr;
            addr = &(ipv4->sin_addr);
        } else if (family == AF_INET6 && includeIPv6) {
            struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)ifa->ifa_addr;
            addr = &(ipv6->sin6_addr);
        } else {
            continue;
        }
        
        // 跳过回环地址
        if (family == AF_INET) {
            uint32_t ip = ntohl(*(uint32_t*)addr);
            if ((ip & 0xFF000000) == 0x7F000000)
                continue;
        } else if (family == AF_INET6) {
            // 跳过 IPv6 回环 ::1
            uint8_t* bytes = (uint8_t*)addr;
            bool isLoopback = true;
            for (int i = 0; i < 15; i++) {
                if (bytes[i] != 0) { isLoopback = false; break; }
            }
            if (isLoopback && bytes[15] == 1)
                continue;
        }
        
        if (addr) {
            inet_ntop(family, addr, localIPs[*ipCount], INET6_ADDRSTRLEN);
            (*ipCount)++;
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
    
    #if 0
    if( strcmp(direct, "[TCP --> COM]") == 0 ){
      memset(retStr, 0, sizeof retStr);
      snprintf(retStr, sizeof retStr, "TCP%-3d--> COM%-3d" , index, comNum);
    }

    if( strcmp(direct, "[COM --> TCP]") == 0 ){
      memset(retStr, 0, sizeof retStr);

      if( runInfo.monopolizeComRecvIndex != NULL ) // 独占串口数据
        snprintf(retStr, sizeof retStr, "COM%-3d--> TCP%-3d", 
          comNum, *runInfo.monopolizeComRecvIndex);
      else
        snprintf(retStr, sizeof retStr, "COM%-3d--> TCP%3d",
          comNum, getClientNum() );
    }
    #else

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
    #endif
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




// systemd 设置的环境变量
bool is_running_as_service() 
{
  bool isInit = false;
  static bool ret;
  if (isInit) 
    return ret;

  const char *invocation_id = getenv("INVOCATION_ID");
  const char *journal_stream = getenv("JOURNAL_STREAM");
  ret = (invocation_id != NULL || journal_stream != NULL)? true:false;
  isInit = true;
  return ret;
}




// 执行命令并获取输出结果。执行成功返回真，执行失败返回假
bool executeCommand(const char* cmd, char* result, size_t resultSize) 
{
  if( cmd == NULL || result == NULL || resultSize <= 0)
    return false;
  char buffer[1024];

  // 重定向标准输出和错误输出
  char full_cmd[2048];
  snprintf(full_cmd, sizeof(full_cmd), "%s 2>&1", cmd);
  
  FILE* fp = popen(full_cmd, "r");
  if (fp == NULL) {
      snprintf(result, resultSize, "Error: Failed to execute command");
      return false;
  }
  
  // 读取所有输出
  size_t total_len = 0;
  while (fgets(buffer, sizeof(buffer), fp) != NULL) {
      size_t chunk_len = strlen(buffer);
      
      // 检查缓冲区是否足够
      if (total_len + chunk_len + 1 >= resultSize) {
          // 缓冲区不足，截断并添加提示
          snprintf(result + total_len, resultSize - total_len, 
                    "\n... (output truncated)");
          break;
      }
      
      strcpy(result + total_len, buffer);
      total_len += chunk_len;
  }
  
  pclose(fp);
  
  // 如果没有输出
  if (total_len == 0) 
      snprintf(result, resultSize, "(no output)");
  return total_len? true:false; 
}


// 忽略大小写的字符串匹配
char* stristr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char*)haystack;

#if defined(_WIN32) || defined(_WIN64)
    /* Windows 平台：使用 StrStrIA */
    return StrStrIA(haystack, needle);
    
#elif defined(__linux__) || defined(__unix__)
    /* Linux 平台：使用 strcasestr */
    return strcasestr(haystack, needle);
    
#else
    /* 通用实现（其他平台） */
    size_t needle_len = strlen(needle);
    size_t haystack_len = strlen(haystack);
    
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        size_t j;
        for (j = 0; j < needle_len; j++) {
            if (tolower((unsigned char)haystack[i + j]) != 
                tolower((unsigned char)needle[j]))
                break;
        }
        if (j == needle_len)
            return (char*)&haystack[i];
    }
    return NULL;
#endif
}

/**
 * 验证字符串是否为合法的十六进制范围
 * @param str 待验证的字符串
 * @return true 表示合法，false 表示非法
 */
bool isValidHexRange(const char *str) 
{
  if (str == NULL || *str == '\0')
    return false;
  bool isRange = false;
  for (const char *p = str; *p; p++){
    if( *p == '-' || *p == ',' || *p == ' ' )
      continue;
    isRange = true;
    if ( !isxdigit((uint8_t)*p) ) 
      return false;  // 包含非十六进制字符
  }
    
  return isRange;
}
