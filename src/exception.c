/******************************************************************************
  * @file    文件 exception.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 程序异常退出检测
  ******************************************************************************
  * @attention 注意 编译需要 -lDbghelp 支持
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "exception.h"

#include <stdbool.h>

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef CLOSE_EXCEPTION_MONITOR
  #define ENABLE_EXCEPTION_MONITOR 1
#else
  #define ENABLE_EXCEPTION_MONITOR 0
#endif


// 条件编译：只有在启用监控时才包含 dbghelp
#if ENABLE_EXCEPTION_MONITOR
#include <dbghelp.h>
#include <psapi.h>
#endif

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量   =========================================*/
static char g_LogPath[MAX_PATH] = "process_exceptions.log";
static char g_LastExceptionInfo[8192] = {0};

#if ENABLE_EXCEPTION_MONITOR
static BOOL g_SymInitialized = FALSE;
#endif

/*================== 本地函数声明   =========================================*/

// 写入日志文件
static void outLogToFile(const char *log)
{ 
  FILE* logFile = fopen(g_LogPath, "a");
  if (logFile == NULL) 
     return; 
  fprintf(logFile, "%s\n", log); 
  fclose(logFile); 
}

static const char *getLocalTime(void)
{
  time_t now = time(NULL);
  struct tm* timeinfo = localtime(&now);
  static char timeStr[64];
  memset(timeStr, 0, sizeof timeStr);
  strftime(timeStr, sizeof timeStr, "%Y-%m-%d %H:%M:%S", timeinfo);
  return timeStr;
}

static const char *getExeName(void)
{
  static char processName[MAX_PATH];
  memset(processName, 0, sizeof processName);
  GetModuleFileNameA(NULL, processName, MAX_PATH);
  char* exeName = strrchr(processName, '\\'); 
  return exeName? exeName+1 : processName;
}

#if ENABLE_EXCEPTION_MONITOR
/******************************************************************************
 * @brief 初始化符号系统（完整模式）
 * @return 初始化是否成功
 ******************************************************************************/
static BOOL InitializeSymbolsSimple(void)
{
  HANDLE hProcess = GetCurrentProcess();
  
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  
  if (SymInitialize(hProcess, NULL, TRUE)) { 
    #if 0 // 强制加载当前模块的符号
    char moduleName[MAX_PATH];
    if (GetModuleFileNameA(NULL, moduleName, MAX_PATH)) {
        DWORD64 baseAddr = SymLoadModuleEx(hProcess, NULL, moduleName, NULL, 0, 0, NULL, 0);
        if (baseAddr == 0)
            printf("警告: 无法加载模块符号，错误: %lu\n", GetLastError());
    }
    #endif
    g_SymInitialized = TRUE;
    return TRUE;
  }
  
  printf("符号系统初始化失败，错误: %lu\n", GetLastError());
  return FALSE;
}

/******************************************************************************
 * @brief 直接获取符号信息
 * @param address 内存地址
 * @param result 结果缓冲区
 * @param resultSize 缓冲区大小
 ******************************************************************************/
static void GetSymbolInfoDirect(DWORD64 address, char* result, size_t resultSize)
{
  HANDLE hProcess = GetCurrentProcess();
  
  if (!g_SymInitialized) {
    snprintf(result, resultSize, "0x%I64X [符号未初始化]", address);
    return;
  }
  
  char buffer[sizeof(SYMBOL_INFO) + 256 * sizeof(char)];
  PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
  pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  pSymbol->MaxNameLen = 256;
  
  DWORD64 displacement = 0;
  
  if (SymFromAddr(hProcess, address, &displacement, pSymbol)) {
    IMAGEHLP_LINE64 line;
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD lineDisplacement = 0;
    
    if (SymGetLineFromAddr64(hProcess, address, &lineDisplacement, &line))
      snprintf(result, resultSize, "0x%-15I64X %s [%s:%ld]", 
              address, pSymbol->Name, line.FileName, line.LineNumber);
    else
      snprintf(result, resultSize, "0x%-15I64X %s+0x%I64X", 
              address, pSymbol->Name, displacement);
  } 
  else {  // 尝试获取模块信息 
    HMODULE hModule = NULL;
    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, 
                          (LPCSTR)address, &hModule)) {
      char moduleName[MAX_PATH];
      GetModuleFileNameA(hModule, moduleName, MAX_PATH);
      char* baseName = strrchr(moduleName, '\\');
      if (baseName) baseName++; else baseName = moduleName;
      snprintf(result, resultSize, "0x%-15I64X %s+0x%I64X", 
              address, baseName, address - (DWORD64)hModule);
    } 
    else 
      snprintf(result, resultSize, "0x%-15I64X [未知地址]", address);
  }
}

/******************************************************************************
 * @brief 使用外部工具获取符号信息
 * @param address 内存地址
 * @param result 结果缓冲区
 * @param resultSize 缓冲区大小
 ******************************************************************************/
static void GetSymbolInfoWithAddr2Line(DWORD64 address, char* result, size_t resultSize)
{
  // 创建临时文件保存地址
  // char tempFile[MAX_PATH] = "temp_addr.txt";
  // FILE* f = fopen(tempFile, "w");
  // if (f) {
  //   fprintf(f, "0x%I64X\n", address);
  //   fclose(f);
  // }
  
  // 构建 addr2line 命令
  // char exePath[MAX_PATH];
  // GetModuleFileNameA(NULL, exePath, MAX_PATH);

  const char *exeName = getExeName();
  char command[1024];
  snprintf(command, sizeof(command), 
            //"addr2line -e \"%s\" -f -C -p < \"%s\"", exePath, tempFile); 
            "addr2line -e \"%s\" -f -C -p 0x%I64X", exeName, address);
  
  // 执行命令并捕获输出
  FILE* pipe = _popen(command, "r");
  if (pipe) {
    char buffer[512];
    if ( fgets(buffer, sizeof buffer, pipe) ) {
      buffer[strcspn(buffer, "\r\n")] = 0; // 移除换行符
      snprintf(result, resultSize, "%s", buffer);
    }
    else {
      snprintf(result, resultSize, "[无法获取符号信息]，请到有addr2line命令的系统执行如下命令：\n"
        "addr2line -e \"%s\" -f -C -p 0x%I64X\n", exeName, address);
    }
    _pclose(pipe);
  }
  else {
    snprintf(result, resultSize, "[addr2line执行失败]，请到有addr2line命令的系统执行如下命令：\n"
      "addr2line -e \"%s\" -f -C -p 0x%I64X\n", exeName, address);
  }
  
  // 删除临时文件
  //remove(tempFile);
}

/******************************************************************************
 * @brief 生成堆栈跟踪（简单版本）
 * @param stackTrace 堆栈跟踪缓冲区
 * @param bufferSize 缓冲区大小
 ******************************************************************************/
static const char * GenerateStackTraceSimple(void)
{
#ifdef _M_X64
    PVOID frames[16];
    USHORT frameCount = RtlCaptureStackBackTrace(0, 16, frames, NULL);
    
    static char trace[1024];
    memset(trace, 0, sizeof trace);
    for (USHORT i = 0; i < frameCount; i++) {
      char frameInfo[256], symbolInfo[512];
      GetSymbolInfoDirect((DWORD64)frames[i], symbolInfo, sizeof(symbolInfo));
      memset(frameInfo, 0, sizeof frameInfo);
      snprintf(frameInfo, sizeof frameInfo, "#%02d %s\n", i, symbolInfo);
      strcat(trace, frameInfo);
    }
#else
    strncpy(trace, "堆栈跟踪: 32位架构暂不支持\n", bufferSize);
#endif
    return trace;
}

/******************************************************************************
 * @brief 获取异常描述
 * @param exceptionCode 异常代码
 * @return 异常描述字符串
 ******************************************************************************/
static const char* GetExceptionDescription(DWORD exceptionCode)
{
  switch(exceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:         return "访问违规";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "数组越界";
    case EXCEPTION_BREAKPOINT:               return "断点";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "数据未对齐";
    case EXCEPTION_FLT_DENORMAL_OPERAND:     return "浮点数异常操作数";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "浮点数除零";
    case EXCEPTION_FLT_INEXACT_RESULT:       return "浮点数不精确结果";
    case EXCEPTION_FLT_INVALID_OPERATION:    return "无效浮点数操作";
    case EXCEPTION_FLT_OVERFLOW:             return "浮点数上溢";
    case EXCEPTION_FLT_STACK_CHECK:          return "浮点数堆栈检查";
    case EXCEPTION_FLT_UNDERFLOW:            return "浮点数下溢";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "非法指令";
    case EXCEPTION_IN_PAGE_ERROR:            return "页面错误";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "整数除零";
    case EXCEPTION_INT_OVERFLOW:             return "整数溢出";
    case EXCEPTION_INVALID_DISPOSITION:      return "无效处置";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "不可继续异常";
    case EXCEPTION_PRIV_INSTRUCTION:         return "特权指令";
    case EXCEPTION_SINGLE_STEP:              return "单步执行";
    case EXCEPTION_STACK_OVERFLOW:           return "堆栈溢出";
    default:                                 return "未知异常";
  }
}

/******************************************************************************
 * @brief 记录异常信息（完整模式）
 * @param ExceptionInfo 异常信息指针
 * @param handlerType 处理器类型
 ******************************************************************************/
static void LogExceptionInfo(PEXCEPTION_POINTERS ExceptionInfo, const char* handlerType)
{ 
  printf("程序为 %s 版本\n", ENABLE_EXCEPTION_MONITOR? "Debug":"Release");
  // 检查符号系统状态
  printf("符号系统: %s初始化 (代码: %lu)\n", g_SymInitialized?"已":"未", 
      g_SymInitialized? 0:GetLastError());
  if (g_SymInitialized) {
    // 测试符号查找
    HANDLE hProcess = GetCurrentProcess();
    DWORD64 testAddr = (DWORD64)ExceptionInfo->ExceptionRecord->ExceptionAddress;
    
    char buffer[sizeof(SYMBOL_INFO) + 256];
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSymbol->MaxNameLen = 256;
    DWORD64 displacement = 0;
    WINBOOL ret = SymFromAddr(hProcess, testAddr, &displacement, pSymbol); 
    printf("符号查找%s（代码%ld），标志:%s\n", ret?"成功":"失败", 
        GetLastError(), ret?pSymbol->Name:"无效"); 
  }

  DWORD processId = GetCurrentProcessId();
  DWORD threadId = GetCurrentThreadId();
  const char *exeName = getExeName();
  const char *timeStr = getLocalTime();
  
  // 获取异常地址的符号信息
  char exceptionSymbol[512] = {0};
  GetSymbolInfoWithAddr2Line((DWORD64)ExceptionInfo->ExceptionRecord->ExceptionAddress, 
                            exceptionSymbol, sizeof exceptionSymbol);
  
  // 生成堆栈跟踪
  const char *stackTrace = GenerateStackTraceSimple();
  
  // 构建异常信息
  snprintf(g_LastExceptionInfo, sizeof g_LastExceptionInfo,
            "======== 异常捕获 =========\n"
            "时间    : %s\n"
            "处理器  : %s\n"
            "程序名称: %s\n"
            "进程ID  : %lu\n"
            "线程ID  : %lu\n"
            "异常原因: 0x%08lX (%s)\n"
            "异常地址: 0x%-15I64X\n"
            "异常位置: %s\n"
            "堆栈跟踪:\n%s\n",
            timeStr, handlerType, exeName, processId, threadId,
            ExceptionInfo->ExceptionRecord->ExceptionCode,
            GetExceptionDescription(ExceptionInfo->ExceptionRecord->ExceptionCode),
            (DWORD64)ExceptionInfo->ExceptionRecord->ExceptionAddress, 
            exceptionSymbol, stackTrace);
  
  // 输出到控制台
  printf("\n%s\n", g_LastExceptionInfo);
  outLogToFile(g_LastExceptionInfo);
}

#else

/******************************************************************************
 * @brief 记录异常信息（精简模式）
 * @param ExceptionInfo 异常信息指针
 * @param handlerType 处理器类型
 ******************************************************************************/
static void LogExceptionInfo(PEXCEPTION_POINTERS ExceptionInfo, const char* handlerType)
{
  printf("程序为 %s 版本\n", ENABLE_EXCEPTION_MONITOR? "Debug":"Release"); 
  DWORD processId = GetCurrentProcessId();
  DWORD threadId = GetCurrentThreadId();
  const char *exeName = getExeName();
  const char *timeStr = getLocalTime(); 
  snprintf(g_LastExceptionInfo, sizeof g_LastExceptionInfo,
            "[异常监控已禁用]\n"
            "时间  : %s\n"
            "进程  : %s\n"
            "程序ID: %lu\n"
            "线程ID: %lu\n"
            "异常  : 0x%08lX\n"
            "地址  : 0x%p\n"
            "处理器: %s\n",
            timeStr, exeName, processId, threadId,
            ExceptionInfo->ExceptionRecord->ExceptionCode,
            ExceptionInfo->ExceptionRecord->ExceptionAddress, handlerType);
  
  printf("\n%s\n", g_LastExceptionInfo); 
  outLogToFile(g_LastExceptionInfo);
}
#endif

/******************************************************************************
 * @brief 异常处理函数 - 向量异常处理
 * @param ExceptionInfo 异常信息指针
 * @return 异常处理结果
 ******************************************************************************/
static LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS ExceptionInfo)
{
    LogExceptionInfo(ExceptionInfo, "VectoredExceptionHandler");
    return EXCEPTION_CONTINUE_SEARCH;
}

/******************************************************************************
 * @brief 异常处理函数 - 未处理异常过滤
 * @param ExceptionInfo 异常信息指针
 * @return 异常处理结果
 ******************************************************************************/
static LONG WINAPI UnhandledExceptionFilterA(PEXCEPTION_POINTERS ExceptionInfo)
{
    LogExceptionInfo(ExceptionInfo, "UnhandledExceptionFilter");
    return EXCEPTION_EXECUTE_HANDLER;
}

/*================== 公共接口实现 ============================================*/

/******************************************************************************
 * @brief 初始化进程异常监控
 ******************************************************************************/
void InitializeProcessExceptionMonitor(void)
{
  //printf("使用%s异常监控...\n", ENABLE_EXCEPTION_MONITOR? "完整":"精简"); 
#if ENABLE_EXCEPTION_MONITOR 
  InitializeSymbolsSimple(); 
#endif
  SetUnhandledExceptionFilter(UnhandledExceptionFilterA);
  AddVectoredExceptionHandler(1, VectoredExceptionHandler);
}

/******************************************************************************
 * @brief 清理进程异常监控
 ******************************************************************************/
void CleanupProcessExceptionMonitor(void)
{
#if ENABLE_EXCEPTION_MONITOR
  if (g_SymInitialized)
    SymCleanup(GetCurrentProcess());
#endif
  printf("异常监控已停止\n");
}

/******************************************************************************
 * @brief 设置异常日志路径
 * @param logPath 日志文件路径
 ******************************************************************************/
void SetExceptionLogPath(const char* logPath)
{
  memcpy(g_LogPath, logPath, (strlen(logPath) < MAX_PATH? strlen(logPath):MAX_PATH - 1));
}

/******************************************************************************
 * @brief 获取最后一次异常信息
 * @return 异常信息字符串
 ******************************************************************************/
const char* GetLastExceptionInfo(void)
{
  return g_LastExceptionInfo;
}