/******************************************************************************
  * @file    文件 logPrint.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 安全的日志打印
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 本地宏定义     =========================================*/
//#define MAX_ASYNC_PRINTF_LEN 1024*10  // 异步队列发送

/*================== 头文件包含     =========================================*/
#include "log.h"
#include "platform.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

// 添加必要的头文件
#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #include <io.h>
    #include <sys/stat.h> 
    #define PATH_SEPARATOR '\\'
    #define stat _stat
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #define PATH_SEPARATOR '/'
#endif

#if MAX_ASYNC_PRINTF_LEN
#include "Queue.h"
#endif

/*================== 本地常量声明    ========================================*/
static mutex_type g_log_cs;
static long g_logFileSizeByte = 1024*1024*10; // 默认日志文件大小10MB

/*================== 本地变量声明    ========================================*/
#if MAX_ASYNC_PRINTF_LEN
static AsyncQueue_t AsyncPrintQueue;
static void AsyncPrintfCallBack(char *, uint32_t);
#endif
void DisableQuickEditMode(void);


// 获取当前时间字符串
static void getCurrentTimeString(char* buffer, size_t bufferSize) {
    time_t now = time(NULL);
    struct tm* timeinfo = localtime(&now);
    strftime(buffer, bufferSize, "%Y-%m-%d %H:%M:%S", timeinfo);
}

// 递归创建目录
static bool createDirectoryRecursive(const char* path) {
#ifdef _WIN32
    return _mkdir(path) == 0 || errno == EEXIST;
#else
    return mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
}

void SafePrintResourceInit(bool start)
{
  if( start ){
    InitializeCriticalSection_Wrapper(&g_log_cs);
    #if MAX_ASYNC_PRINTF_LEN 
    startAsyncQueue(&AsyncPrintQueue, 
        AsyncPrintfCallBack, 200, MAX_ASYNC_PRINTF_LEN, "Printf");
    #endif
    DisableQuickEditMode();
  }
  else{ 
    #if MAX_ASYNC_PRINTF_LEN
    FreeAsyncQueue(&AsyncPrintQueue);
    #endif
    DeleteCriticalSection_Wrapper(&g_log_cs);
  }
  
}

#if MAX_ASYNC_PRINTF_LEN
static void AsyncPrintfCallBack(char *data, uint32_t len)
{ 
  fwrite(data, 1, len, stdout);
  fflush(stdout);
}
#endif

int SafePrintf(const char* format, ...)
{
  EnterCriticalSection_Wrapper(&g_log_cs);
  
#if !MAX_ASYNC_PRINTF_LEN
  va_list args;
  va_start(args, format);
  int retLen = vprintf(format, args);
  va_end(args);
  fflush(stdout);
#else 
  static char stringBuff[MAX_ASYNC_PRINTF_LEN];

  va_list args; 
  va_start(args, format);
  int retLen = vsnprintf(stringBuff, sizeof stringBuff, format, args);
  va_end(args);

  if(retLen <= 0) {
    memset(stringBuff, 0, sizeof stringBuff);
    strcpy(stringBuff, "SafePrintf Format error");
    retLen = strlen(stringBuff);
  }

  if( false == AsyncPrintQueue.running || 
      false == AddDataToAsyncQueue(&AsyncPrintQueue, stringBuff, retLen) ) {
    fwrite(stringBuff, 1, retLen, stdout);
    fflush(stdout);
  }
#endif
  
  LeaveCriticalSection_Wrapper(&g_log_cs);
  return retLen;
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
void printHex(const uint8_t *pdata, uint16_t len, uint8_t numEnter, uint8_t endEnter)
{
  EnterCriticalSection_Wrapper(&g_log_cs);
  static char outputBuffer[4096];
  static const char *hexTable = "0123456789ABCDEF";
  
  char *ptr = outputBuffer;
  uint16_t bufferRemaining = sizeof outputBuffer;
  
  for(uint16_t i = 0; i < len; i++) {
    // 检查是否需要换行
    if(numEnter && i % numEnter == 0 && i != 0)
      if(bufferRemaining > 1) {
          *ptr++ = '\n';
          bufferRemaining--;
      }
    
    // 检查缓冲区是否足够存放当前字节的十六进制表示（3字符：XX+空格）
    if(bufferRemaining < 3) {
        // 缓冲区不足，先输出已缓存的内容
        *ptr = '\0'; 
        fwrite(outputBuffer, 1, ptr - outputBuffer, stdout);
        ptr = outputBuffer;
        bufferRemaining = sizeof outputBuffer;
    }

    // 将字节转换为十六进制
    *ptr++ = hexTable[pdata[i] >> 4];
    *ptr++ = hexTable[pdata[i] & 0x0F];
    *ptr++ = ' ';
    bufferRemaining -= 3;
  }
  
  // 输出缓冲区中剩余的内容
  if(ptr > outputBuffer) {
    *ptr = '\0';
    fwrite(outputBuffer, 1, ptr - outputBuffer, stdout);
  }
  
  // 输出结束换行
  for(uint8_t i = 0; i < endEnter; i++)
    putchar('\n');
  
  fflush(stdout);
  LeaveCriticalSection_Wrapper(&g_log_cs);
}

/**
 * @brief  格式化字符串并返回字符串空间
 * @param 
 *		@arg printf 格式
 * @retval 格式化后的字符串
 * 注意每个线程尽量不要嵌套超过6次容易字符串混淆
 */
char *getPrintf(const char *format, ...)
{
  // 使用线程局部存储，每个线程有自己的副本
  // 线程副本会让程序体积增加不少
  static __thread uint8_t buffIndex = 0;
  static __thread char stringBuff[6][1024];

  if( ++buffIndex >= sizeof stringBuff / sizeof stringBuff[0] )
    buffIndex = 0;
  
  va_list args; 
  va_start(args, format);
  int retLen = vsnprintf(stringBuff[buffIndex], sizeof stringBuff[buffIndex], format, args);
  va_end(args);

  if(retLen <= 0) {
    memset(stringBuff[buffIndex], 0, sizeof stringBuff[buffIndex]);
    strcpy(stringBuff[buffIndex], "get Printf Format error");
  }
  
  return stringBuff[buffIndex];
}

// 禁用快捷编辑模式，防止Win10以上系统点击控制台导致程序阻塞挂起
void DisableQuickEditMode(void) 
{
#ifdef _WIN32
  HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
    
  if (GetConsoleMode(hInput, &mode) == false) 
    return;
  // 清除快速编辑模式标志位
  mode &= ~ENABLE_QUICK_EDIT_MODE;
  // 启用窗口输入和鼠标输入（可选）
  mode |= ENABLE_EXTENDED_FLAGS;
  mode |= ENABLE_WINDOW_INPUT;
  mode |= ENABLE_MOUSE_INPUT;
  
  SetConsoleMode(hInput, mode); 
#endif
}


// 在 logPrint.c 中修改线程相关代码

/*================== 日志存储相关变量和函数 ========================================*/
static char g_logDir[256] = {0};
static char g_logFileName[128] = {0};
static char g_logFullPath[512] = {0};
static FILE* g_logFile = NULL;
static LogLevel_t g_logLevel = LOG_LEVEL_DEBUG;
static mutex_type g_log_file_cs;

// 新增变量 - 使用 platform.h 的类型
static uint8_t g_maxLogFiles = 5;
static uint32_t g_checkIntervalSec = 5;


// 日志级别字符串转换函数
static const char* getLogLevelString(LogLevel_t level) {
  static char unknownStr[10];
  static const char* levelStrings[] = {
      "DEBUG", "INFO", "WARN", "ERROR", };
  
  if(level >= 99)
    level = 99;
  
  if(level >= LOG_LEVEL_END){
    snprintf(unknownStr, sizeof unknownStr, "LVL%2d", level);
    return unknownStr;
  }

  return levelStrings[level]; 
}

// 获取文件大小
static long getFileSize(const char* filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

// 删除最旧的日志文件
static void deleteOldestLogFiles(void) {
#ifdef _WIN32
    // Windows 版本使用 FindFirstFile/FindNextFile
    WIN32_FIND_DATAA findFileData;
    HANDLE hFind;
    char searchPath[512];
    
    snprintf(searchPath, sizeof(searchPath), "%s%c%s*.log", g_logDir, PATH_SEPARATOR, g_logFileName);
    
    hFind = FindFirstFileA(searchPath, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) {
        snprintf(searchPath, sizeof(searchPath), "%s%c%s*.log", g_logDir, PATH_SEPARATOR, g_logFileName);
        hFind = FindFirstFileA(searchPath, &findFileData);
        if (hFind == INVALID_HANDLE_VALUE) {
            return;
        }
    }
    
    typedef struct {
        char filename[256];
        FILETIME ftWrite;
    } LogFileInfo;
    
    LogFileInfo logFiles[50];
    int fileCount = 0;
    
    do {
        if (strstr(findFileData.cFileName, g_logFileName) != NULL && 
            (strstr(findFileData.cFileName, ".log") != NULL || strstr(findFileData.cFileName, ".log") != NULL)) {
            
            char filepath[600];
            snprintf(filepath, sizeof(filepath), "%s%c%s", g_logDir, PATH_SEPARATOR, findFileData.cFileName);
            
            memcpy(logFiles[fileCount].filename, filepath, sizeof(logFiles[fileCount].filename) - 1);
            logFiles[fileCount].ftWrite = findFileData.ftLastWriteTime;
            fileCount++;
        }
    } while (FindNextFileA(hFind, &findFileData) != 0 && fileCount < 50);
    
    FindClose(hFind);
    
    if (fileCount > g_maxLogFiles) {
        // 按时间排序
        for (int i = 0; i < fileCount - 1; i++) {
            for (int j = 0; j < fileCount - i - 1; j++) {
                if (CompareFileTime(&logFiles[j].ftWrite, &logFiles[j + 1].ftWrite) > 0) {
                    LogFileInfo temp = logFiles[j];
                    logFiles[j] = logFiles[j + 1];
                    logFiles[j + 1] = temp;
                }
            }
        }
        
        // 删除最旧的文件
        int filesToDelete = fileCount - g_maxLogFiles;
        for (int i = 0; i < filesToDelete; i++) {
            remove(logFiles[i].filename);
            SafePrintf("删除旧日志: %s\n", logFiles[i].filename);
        }
    }
#else
    // Linux 版本使用 opendir/readdir
    DIR *dir;
    struct dirent *entry;
    struct stat fileStat;
    char filepath[512];
    
    dir = opendir(g_logDir);
    if (dir == NULL) {
        return;
    }
    
    typedef struct {
        char filename[256];
        time_t mtime;
    } LogFileInfo;
    
    LogFileInfo logFiles[50];
    int fileCount = 0;
    
    while ((entry = readdir(dir)) != NULL && fileCount < 50) {
        if (strstr(entry->d_name, g_logFileName) != NULL && 
            (strstr(entry->d_name, ".log") != NULL || strstr(entry->d_name, ".log") != NULL)) {
            
            snprintf(filepath, sizeof(filepath), "%s%c%s", g_logDir, PATH_SEPARATOR, entry->d_name);
            
            if (stat(filepath, &fileStat) == 0) {
                memcpy(logFiles[fileCount].filename, filepath, sizeof(logFiles[fileCount].filename) - 1);
                logFiles[fileCount].mtime = fileStat.st_mtime;
                fileCount++;
            }
        }
    }
    closedir(dir);
    
    if (fileCount > g_maxLogFiles) {
        // 按时间排序
        for (int i = 0; i < fileCount - 1; i++) {
            for (int j = 0; j < fileCount - i - 1; j++) {
                if (logFiles[j].mtime > logFiles[j + 1].mtime) {
                    LogFileInfo temp = logFiles[j];
                    logFiles[j] = logFiles[j + 1];
                    logFiles[j + 1] = temp;
                }
            }
        }
        
        // 删除最旧的文件
        int filesToDelete = fileCount - g_maxLogFiles;
        for (int i = 0; i < filesToDelete; i++) {
            remove(logFiles[i].filename);
            SafePrintf("删除旧日志: %s\n", logFiles[i].filename);
        }
    }
#endif
}

// 检查并滚动日志文件
void Time1SecCheckAndRotateLogFile(void) {
    static uint8_t SecCount = 0;
    if( ++SecCount < g_checkIntervalSec )
      return ;
    SecCount = 0;

    if(g_logFile == NULL)
        return ;
    
    long fileSize = getFileSize(g_logFullPath);
    if (fileSize < 0 || fileSize < g_logFileSizeByte)
        return ;
    
    EnterCriticalSection_Wrapper(&g_log_file_cs);
    
    fclose(g_logFile);
    g_logFile = NULL;
    
    char backupPath[512] = {0};
    char timeStr[32] = {0};
    time_t now = time(NULL);
    struct tm* timeinfo = localtime(&now);
    strftime(timeStr, sizeof(timeStr), "%Y%m%d_%H%M%S", timeinfo);
    
    snprintf(backupPath, sizeof(backupPath), "%s%c%s_%s.log", 
             g_logDir, PATH_SEPARATOR, g_logFileName, timeStr);
    
    if (rename(g_logFullPath, backupPath) == 0) {
        deleteOldestLogFiles();
        
        g_logFile = fopen(g_logFullPath, "a");
        if(g_logFile != NULL) {
            setvbuf(g_logFile, NULL, _IOLBF, 0);
            
            char newTimeStr[32] = {0};
            getCurrentTimeString(newTimeStr, sizeof(newTimeStr));
            fprintf(g_logFile, "=== Log rotated at %s ===\n", newTimeStr);
            fflush(g_logFile);
            
            SafePrintf("日志文件已滚动: %s -> %s\n", g_logFullPath, backupPath);
            
            LeaveCriticalSection_Wrapper(&g_log_file_cs);
            return ;
        }
    }
    
    g_logFile = fopen(g_logFullPath, "a");
    if(g_logFile != NULL)
        setvbuf(g_logFile, NULL, _IOLBF, 0);
    
    LeaveCriticalSection_Wrapper(&g_log_file_cs);
}



 



/*
  * @brief 日志存储初始化
  * @param logDir 日志目录
  * @param logFileName 日志文件名（不含路径和扩展名）
  * @param level 日志级别
  * @param maxFilesToKeep 最大日志文件数
  * @param checkIntervalSec 检查间隔（秒）
  * @param logFileSizeKB 日志文件大小阈值（KB），默认10MB，最小10KB
  * @return true 成功，false 失败
*/
bool logStorageInit(const char* logDir, const char* logFileName, LogLevel_t level, 
                    uint8_t maxFilesToKeep, uint8_t checkIntervalSec, uint16_t logFileSizeKB) {
    // 参数验证和设置

    if(logFileSizeKB > 9)  // 必须大于10KB
        g_logFileSizeByte = logFileSizeKB * 1024;
    
    g_checkIntervalSec = checkIntervalSec;
    g_maxLogFiles = (maxFilesToKeep < 1) ? 1 : maxFilesToKeep;

    // 初始化临界区
    InitializeCriticalSection_Wrapper(&g_log_file_cs);
    EnterCriticalSection_Wrapper(&g_log_file_cs);
    
    // 保存配置
    strncpy(g_logDir, logDir, sizeof(g_logDir) - 1);
    strncpy(g_logFileName, logFileName, sizeof(g_logFileName) - 1);
    g_logLevel = level;
    
    // 创建目录（如果需要）
    if(strcmp(logDir, "./") != 0 && strcmp(logDir, ".") != 0) {
        createDirectoryRecursive(logDir);
    }
 
    // 构建完整路径
    snprintf(g_logFullPath, sizeof(g_logFullPath), "%s%c%s.log", 
             logDir, PATH_SEPARATOR, logFileName);
    
    // 打开日志文件
    g_logFile = fopen(g_logFullPath, "a");
    if(g_logFile == NULL) {
        LeaveCriticalSection_Wrapper(&g_log_file_cs);
        return false;
    }
    
    setvbuf(g_logFile, NULL, _IOLBF, 0);
    
    // 写入初始信息
    char timeStr[32] = {0};
    getCurrentTimeString(timeStr, sizeof(timeStr));
    fprintf(g_logFile, "=== Log started at %s (maxFiles: %d, checkInterval: %d Sec) ===\n", 
            timeStr, g_maxLogFiles, g_checkIntervalSec);
    fflush(g_logFile);
    
    LeaveCriticalSection_Wrapper(&g_log_file_cs);
    
 

    // SafePrintf("日志系统初始化完成: 目录=%s, 文件=%s, 最大文件数=%d, 检查间隔=%dms\n",
    //            logDir, logFileName, g_maxLogFiles, g_checkIntervalSec);
    
    return true;
}

// 修改反初始化函数
void logStorageUninit(void) { 
    EnterCriticalSection_Wrapper(&g_log_file_cs);
    
    if(g_logFile != NULL) {
        char timeStr[32] = {0};
        getCurrentTimeString(timeStr, sizeof(timeStr));
        fprintf(g_logFile, "=== Log ended at %s ===\n\n", timeStr);
        fflush(g_logFile);
        fclose(g_logFile);
        g_logFile = NULL;
    }
    
    LeaveCriticalSection_Wrapper(&g_log_file_cs);
    DeleteCriticalSection_Wrapper(&g_log_file_cs);
    
    SafePrintf("日志系统已卸载\n");
}

// 简化日志写入函数
void logPrintFull(LogLevel_t level, const char* format, ...) { 
    if(level < g_logLevel || g_logFile == NULL) {
        return;
    }
 
    EnterCriticalSection_Wrapper(&g_log_file_cs);
    
    char timeStr[32] = {0};
    getCurrentTimeString(timeStr, sizeof timeStr);
    const char* levelStr = getLogLevelString(level);
    
    fprintf(g_logFile, "[%s %-5s] ", timeStr, levelStr);
    
    va_list args;
    va_start(args, format);
    vfprintf(g_logFile, format, args);
    va_end(args);
    
    fprintf(g_logFile, "\n");
    fflush(g_logFile);
    
    LeaveCriticalSection_Wrapper(&g_log_file_cs); 
}

// 简化 logPrint 函数
void logPrint(const char* format, ...) {
    if(g_logFile == NULL) {
        return;
    }
    
    EnterCriticalSection_Wrapper(&g_log_file_cs);
    
    va_list args;
    va_start(args, format);
    vfprintf(g_logFile, format, args);
    va_end(args);
    
    fprintf(g_logFile, "\n");
    fflush(g_logFile);
    
    LeaveCriticalSection_Wrapper(&g_log_file_cs);
}