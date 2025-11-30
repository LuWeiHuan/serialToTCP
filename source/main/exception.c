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

/*================== 头文件包含 =========================================*/
#include "exception.h" 
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "logPrint.h"

/*================== 平台配置 =========================================*/
#ifndef CLOSE_EXCEPTION_MONITOR
  #define ENABLE_EXCEPTION_MONITOR 1
#else
  #define ENABLE_EXCEPTION_MONITOR 0
#endif

/*================== 平台特定头文件包含 =================================*/
#ifdef _WIN32
  /* Windows 头文件 */
  #include <windows.h>
  #include <psapi.h>
  #include <dbghelp.h>
  #include <io.h>
  //#include <signal.h>  // 添加信号处理支持
  #define access _access
  #define F_OK 0
#else
  /* Linux 头文件 */
  #include <stdint.h>
  #include <errno.h>
  #include <signal.h>
  #include <libgen.h>
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/syscall.h>  // 添加这一行
   
#if ENABLE_EXCEPTION_MONITOR
  #include <execinfo.h>
  #include <ucontext.h>
  #include <sys/ucontext.h>
  #include "main.h"
#endif

#endif

/*================== 宏定义 =========================================*/
#define MAX_STACK_FRAMES 64
#define EXCEPTION_INFO_SIZE 8192

#ifndef _WIN32
#define MAX_PATH  4096
#endif 

/*================== 全局共享变量 =====================================*/

static char g_LastExceptionInfo[EXCEPTION_INFO_SIZE] = {0};

#if ENABLE_EXCEPTION_MONITOR
  #ifdef _WIN32
    static bool g_SymInitialized = false;
  #else
    static struct sigaction g_OldActions[NSIG];
  #endif
#endif



/*================== 本地函数声明 =====================================*/

/* 通用工具函数 */
static const char *getLocalTime(void);
static const char *getExeName(void);

#ifdef _WIN32
/* Windows 特定函数 */
  #if ENABLE_EXCEPTION_MONITOR
    static void GetSymbolInfoDirect(DWORD64 address, char* result, size_t resultSize);
    static void GetSymbolInfoWithAddr2Line(DWORD64 address, char* result, size_t resultSize);
    static const char * GenerateStackTraceSimple(PEXCEPTION_POINTERS ExceptionInfo);
    static const char* GetExceptionDescription(DWORD exceptionCode); 
  #else
  
  #endif
    static bool InitializeSymbolsSimple(void);
    static void LogExceptionInfo(PEXCEPTION_POINTERS ExceptionInfo, const char* handlerType);
    static long WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS ExceptionInfo);
    static long WINAPI UnhandledExceptionFilterA(PEXCEPTION_POINTERS ExceptionInfo);
    static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType);
    
#else
/* Linux 特定函数 */
    static void SignalHandler(int sig, siginfo_t *info, void *context);
    static void SetupSignalHandlers(void);

  #if ENABLE_EXCEPTION_MONITOR 
    static void RestoreSignalHandlers(void);
    static const char* GetSignalDescription(int sig);
    static void GenerateLinuxStackTrace(char* buffer, size_t bufferSize);
    static void ResolveAddressToSource(void *address, char *output, size_t output_size);
    static bool CheckAddr2LineAvailable(void);
    static const char* GetArchitectureInfo(void);
  #else 
    static void LogExceptionInfo(int sig, siginfo_t *info, void *context);
    //static void LogNormalExit(const char* reason);
  #endif
#endif

/*================== 通用工具函数实现 =================================*/


static void outLogToFile(const char *log)
{
  logPrintFull(LOG_LEVEL_ERROR, "\n%s", log);
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
    
#ifdef _WIN32
    GetModuleFileNameA(NULL, processName, MAX_PATH);
    char* exeName = strrchr(processName, '\\'); 
#else
    ssize_t len = readlink("/proc/self/exe", processName, MAX_PATH - 1);
    if (len != -1)
        processName[len] = '\0';
    char* exeName = strrchr(processName, '/');
#endif
    
    return exeName ? exeName + 1 : processName;
}



/*================== 平台特定实现 =====================================*/
#ifdef _WIN32


static void LogVoluntaryExit(const char* reason)
{
    const char *exeName = getExeName();
    const char *timeStr = getLocalTime();
    
#ifdef _WIN32
    DWORD processId = GetCurrentProcessId();
    DWORD threadId = GetCurrentThreadId();
#else
    pid_t processId = getpid();
    #if defined(__arm__) // ARM32 似乎不支持 gettid
    pid_t threadId = syscall(SYS_gettid);
    #else
    pid_t threadId = gettid();
    #endif
#endif
    
    char exitInfo[1024];
    snprintf(exitInfo, sizeof(exitInfo),
              "======== 程序退出 =========\n"
              "时间    : %s\n"
              "程序名称: %s\n"
              "进程ID  : %d\n"
              "线程ID  : %d\n"
              "退出原因: %s\n"
              "退出类型: %s\n",
              timeStr, exeName, (int)processId, (int)threadId, reason,
              strstr(reason, "主动") ? "主动退出" : "外部中断");
    
    printf("\n%s\n", exitInfo);
    outLogToFile(exitInfo);
}


static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    const char* reason = NULL;
    
    switch (dwCtrlType) {
        case CTRL_C_EVENT:
            reason = "用户按下 CTRL+C";
            break;
        case CTRL_BREAK_EVENT:
            reason = "用户按下 CTRL+BREAK";
            break;
        case CTRL_CLOSE_EVENT:
            reason = "控制台窗口关闭";
            break;
        case CTRL_LOGOFF_EVENT:
            reason = "用户注销";
            break;
        case CTRL_SHUTDOWN_EVENT:
            reason = "系统关机";
            break;
        default:
            reason = "未知控制台事件";
            break;
    }
    
    printf("\n检测到程序退出: %s (事件类型: %lu)\n", reason, dwCtrlType);
    
    // 记录退出信息
    LogVoluntaryExit(reason);
    
    // 允许正常退出
    return FALSE;
}
#if 0
static void LogNormalExit(const char* reason)
{
    const char *exeName = getExeName();
    const char *timeStr = getLocalTime();
    DWORD processId = GetCurrentProcessId();
    DWORD threadId = GetCurrentThreadId();
    
    char exitInfo[1024];
    snprintf(exitInfo, sizeof(exitInfo),
              "======== 正常退出 =========\n"
              "时间    : %s\n"
              "程序名称: %s\n"
              "进程ID  : %lu\n"
              "线程ID  : %lu\n"
              "退出原因: %s\n"
              "退出类型: 正常退出\n",
              timeStr, exeName, processId, threadId, reason);
    
    printf("\n%s\n", exitInfo);
    outLogToFile(exitInfo);
}
#endif
static bool InitializeSymbolsSimple(void)
{
#if ENABLE_EXCEPTION_MONITOR==0
  bool g_SymInitialized = false;
  return g_SymInitialized;
#endif
  HANDLE hProcess = GetCurrentProcess(); 
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  g_SymInitialized = SymInitialize(hProcess, NULL, true); 
  if( g_SymInitialized == false)
    printf("符号系统初始化失败，错误: %lu\n", GetLastError()); 
  return g_SymInitialized; 
}
#endif

#if ENABLE_EXCEPTION_MONITOR

/******************** 启用异常监控版本 ********************/

static const char* extract_path_info(const char* input) {
    static char output[512];
    output[0] = '\0';
    
    // 尝试查找分隔符，支持 " at " 和 " 于 "
    const char* separator = NULL;
    const char* at_pos = strstr(input, " at ");
    const char* yu_pos = strstr(input, " 于 ");
    
    if (at_pos) {
        separator = at_pos;
    } else if (yu_pos) {
        separator = yu_pos;
    }
    
    if (!separator) return input;
    
    // 计算分隔符长度
    size_t separator_len = (separator == at_pos) ? 4 : 3; // " at " 是4个字符，" 于 " 是3个字符
    size_t prefix_len = separator - input;
    const char* path_start = separator + separator_len;
    
    // 计算路径中的斜杠总数
    int total_slashes = 0;
    for (const char* p = path_start; *p; p++) {
        if (*p == '/') total_slashes++;
    }
    
    const char* project_path_start = path_start;
    
    if (total_slashes >= 3) {
        // 如果有3个或更多斜杠，跳过前面的部分
        int slashes_to_find = total_slashes - 1;
        const char* p = path_start;
        while (slashes_to_find > 0 && *p) {
            if (*p == '/') slashes_to_find--;
            p++;
        }
        if (*p) project_path_start = p;
    } else if (total_slashes == 2) {
        // 如果正好2个斜杠，从第一个斜杠后面开始
        const char* first_slash = strchr(path_start, '/');
        if (first_slash) project_path_start = first_slash + 1;
    }
    // 如果只有0-1个斜杠，直接使用完整路径
    
    // 使用原始的分隔符重新构建输出
    const char* original_separator = (separator == at_pos) ? " at " : " 于 ";
    snprintf(output, sizeof(output), "%.*s%s%s", 
             (int)prefix_len, input, original_separator, project_path_start);
    
    return output;
}

#ifdef _WIN32
/*================== Windows 启用监控版本 ====================*/

static void GetSymbolInfoDirect(DWORD64 address, char* result, size_t resultSize)
{
    HANDLE hProcess = GetCurrentProcess();
    
    if (!g_SymInitialized) {
        snprintf(result, resultSize, "0x%llX [符号未初始化]", address);
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
            snprintf(result, resultSize, "0x%-15llX %s [%s:%ld]", 
                    address, pSymbol->Name, line.FileName, line.LineNumber);
        else
            snprintf(result, resultSize, "0x%-15llX %s+0x%llX", 
                    address, pSymbol->Name, displacement);
    } 
    else {
        HMODULE hModule = NULL;
        if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, 
                              (LPCSTR)address, &hModule)) {
            char moduleName[MAX_PATH];
            GetModuleFileNameA(hModule, moduleName, MAX_PATH);
            char* baseName = strrchr(moduleName, '\\');
            if (baseName) baseName++; else baseName = moduleName;
            snprintf(result, resultSize, "0x%-15llX %s+0x%llX", 
                    address, baseName, address - (DWORD64)hModule);
        } 
        else {
            snprintf(result, resultSize, "0x%-15llX [未知地址]", address);
        }
    }
}

static void GetSymbolInfoWithAddr2Line(DWORD64 address, char* result, size_t resultSize)
{
    const char *exeName = getExeName();
    char command[1024];
    snprintf(command, sizeof(command), 
              "addr2line -e \"%s\" -f -C -p 0x%llX", exeName, address);
    
    FILE* pipe = _popen(command, "r");
    if (pipe) {
        char buffer[512];
        if ( fgets(buffer, sizeof buffer, pipe) ) {
            buffer[strcspn(buffer, "\r\n")] = 0;
            snprintf(result, resultSize, "%s", buffer);
        }
        else {
            snprintf(result, resultSize, "[无法获取符号信息]，请执行: addr2line -e \"%s\" -f -C -p 0x%llX", 
                    exeName, address);
        }
        _pclose(pipe);
    }
    else {
        snprintf(result, resultSize, "[addr2line执行失败]，请执行: addr2line -e \"%s\" -f -C -p 0x%llX", 
                exeName, address);
    }
}

static const char * GenerateStackTraceSimple(PEXCEPTION_POINTERS ExceptionInfo)
{
    (void)ExceptionInfo;
#ifdef _M_X64
    PVOID frames[16];
    USHORT frameCount = RtlCaptureStackBackTrace(0, 16, frames, NULL);
    
    static char trace[2048];
    memset(trace, 0, sizeof trace);
    
    for (USHORT i = 0; i < frameCount; i++) {
        char frameInfo[512], symbolInfo[256];
        GetSymbolInfoDirect((DWORD64)frames[i], symbolInfo, sizeof symbolInfo);
        memset(frameInfo, 0, sizeof frameInfo);
        snprintf(frameInfo, sizeof frameInfo, "#%02d %s\n", i, symbolInfo);
        strcat(trace, frameInfo);
    }
#else
    static char trace[256];
    strncpy(trace, "堆栈跟踪: 32位架构暂不支持\n", sizeof(trace));
#endif
    return trace;
}

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

 


static void LogExceptionInfo(PEXCEPTION_POINTERS ExceptionInfo, const char* handlerType)
{ 
    printf("程序为 %s 版本\n", ENABLE_EXCEPTION_MONITOR? "Debug":"Release");
    
    if (g_SymInitialized) {
        HANDLE hProcess = GetCurrentProcess();
        DWORD64 testAddr = (DWORD64)ExceptionInfo->ExceptionRecord->ExceptionAddress;
        
        char buffer[sizeof(SYMBOL_INFO) + 256];
        PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = 256;
        DWORD64 displacement = 0;
        bool ret = SymFromAddr(hProcess, testAddr, &displacement, pSymbol); 
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
    const char *exceptionSymbolRet = extract_path_info(exceptionSymbol);
    
    // 生成堆栈跟踪
    const char *stackTrace = GenerateStackTraceSimple(ExceptionInfo);
    
    // 构建异常信息
    snprintf(g_LastExceptionInfo, sizeof g_LastExceptionInfo,
              "======== 异常捕获 =========\n"
              "时间    : %s\n"
              "处理器  : %s\n"
              "程序名称: %s\n"
              "进程ID  : %lu\n"
              "线程ID  : %lu\n"
              "异常原因: 0x%08lX (%s)\n"
              "异常地址: 0x%-15llX\n"
              "异常位置: %s\n"
              "堆栈跟踪:\n%s\n",
              timeStr, handlerType, exeName, processId, threadId,
              ExceptionInfo->ExceptionRecord->ExceptionCode,
              GetExceptionDescription(ExceptionInfo->ExceptionRecord->ExceptionCode),
              (DWORD64)ExceptionInfo->ExceptionRecord->ExceptionAddress, 
              exceptionSymbolRet, stackTrace);
    
    // 输出到控制台
    printf("\n%s\n", g_LastExceptionInfo);
    outLogToFile(g_LastExceptionInfo);
}

static long WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS ExceptionInfo)
{
    LogExceptionInfo(ExceptionInfo, "VectoredExceptionHandler");
    return EXCEPTION_CONTINUE_SEARCH;
}

static long WINAPI UnhandledExceptionFilterA(PEXCEPTION_POINTERS ExceptionInfo)
{
    LogExceptionInfo(ExceptionInfo, "UnhandledExceptionFilter");
    return EXCEPTION_EXECUTE_HANDLER;
}

#else
/*================== Linux 启用监控版本 ====================*/



static bool CheckAddr2LineAvailable(void)
{
    FILE *fp = popen("which addr2line", "r");
    if (fp) {
        char result[128];
        bool available = (fgets(result, sizeof(result), fp) != NULL);
        pclose(fp);
        return available;
    }
    return false;
}

static void ResolveAddressToSource(void *address, char *output, size_t output_size)
{
    static char exe_path[MAX_PATH] = {0};
    static bool addr2line_checked = false;
    static bool addr2line_available = false;
    
    // 第一次调用时检查 addr2line 是否可用
    if (!addr2line_checked) {
        addr2line_available = CheckAddr2LineAvailable();
        addr2line_checked = true;
        
        // 获取可执行文件路径
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path)-1);
        if (len > 0) {
            exe_path[len] = '\0';
        } else {
            strcpy(exe_path, "unknown");
        }
    }
    
    // 如果 addr2line 不可用，给出提示信息
    if (!addr2line_available) {
        snprintf(output, output_size, 
                 "0x%p [需要addr2line工具解析，请安装: sudo apt install binutils]",
                 address);
        return;
    }
    
    char command[MAX_PATH + 2048];
    snprintf(command, sizeof command,
             "addr2line -e \"%s\" -f -C -p %p 2>/dev/null", 
             exe_path, address);
    
    FILE *fp = popen(command, "r");
    if (fp) {
        char buffer[512];
        if (fgets(buffer, sizeof(buffer), fp)) {
            // 移除换行符
            buffer[strcspn(buffer, "\r\n")] = '\0';
            const char* processed = extract_path_info(buffer);
            snprintf(output, output_size, "%s", processed);
        } else {
            snprintf(output, output_size, "0x%p [符号解析失败]", address);
        }
        pclose(fp);
    } else {
        snprintf(output, output_size, 
                 "0x%p [addr2line执行失败，请手动运行: addr2line -e \"%s\" -f -C -p %p]",
                 address, exe_path, address);
    }
}

static const char* GetSignalDescription(int sig)
{
    switch(sig) {
        case SIGSEGV: return "段错误 (无效内存访问)";
        case SIGFPE:  return "算术异常 (除零/溢出)";
        case SIGILL:  return "非法指令";
        case SIGBUS:  return "总线错误";
        case SIGABRT: return "程序中止";
        case SIGTERM: return "终止信号";
        case SIGINT:  return "中断信号";
        case SIGQUIT: return "退出信号";
        case SIGHUP:  return "挂起信号";
        case SIGALRM: return "定时器信号";
        default:      return "未知信号";
    }
}

static void GenerateLinuxStackTrace(char* buffer, size_t bufferSize)
{
    void* array[MAX_STACK_FRAMES];
    size_t size;
    char** strings;
    size_t i;
    
    size = backtrace(array, MAX_STACK_FRAMES);
    strings = backtrace_symbols(array, size);
    
    buffer[0] = '\0';
    
    if (strings != NULL) {
        for (i = 0; i < size && strlen(buffer) < bufferSize - 200; i++) {
            char line[512];
            char symbolInfo[256] = {0};
            
            // 使用符号解析获取详细的行号信息
            ResolveAddressToSource(array[i], symbolInfo, sizeof(symbolInfo));
            
            snprintf(line, sizeof(line), "#%02zu %s\n     -> %s\n", 
                    i, strings[i], symbolInfo);
            strcat(buffer, line);
        }
        free(strings);
    } else {
        strcpy(buffer, "无法生成堆栈跟踪\n");
    }
}

static const char* GetArchitectureInfo(void)
{
#if defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "x86";
#elif defined(__aarch64__)
    return "ARM64";
#elif defined(__arm__)
    return "ARM32";
#elif defined(__mips__)
    return "MIPS";
#elif defined(__powerpc__)
    return "PowerPC";
#elif defined(__riscv)
    return "RISC-V";
#else
    return "Unknown";
#endif
}

static void SignalHandler(int sig, siginfo_t *info, void *context)
{
    ucontext_t *ucontext = (ucontext_t *)context;
    // 根据不同架构获取指令指针
#if defined(__x86_64__) // x86_64 架构
    void *instruction_ptr = (void*)ucontext->uc_mcontext.gregs[REG_RIP];
#elif defined(__i386__) // x86 架构
    void *instruction_ptr = (void*)ucontext->uc_mcontext.gregs[REG_EIP];
#elif defined(__aarch64__) // ARM64 架构
    void *instruction_ptr = (void*)ucontext->uc_mcontext.pc;
#elif defined(__arm__) // ARM32 架构
    void *instruction_ptr = (void*)ucontext->uc_mcontext.arm_pc;
#else // 其他架构，使用通用方法
    void *instruction_ptr = info->si_addr; // 回退到错误地址
#endif
    
    // 使用指令指针而不是错误地址
    if (sig != SIGINT){
      printf("错误地址: %p\n", info->si_addr);
      printf("指令指针: %p\n", instruction_ptr);
    }

    // 恢复默认信号处理，防止递归异常
    signal(sig, SIG_DFL);
    
    // 对于 SIGINT，可以选择是否立即退出
    if (sig == SIGINT)
      voluntaryWithdrawal("程序被用户中断 (CTRL+C)");
    
    const char *exeName = getExeName();
    const char *timeStr = getLocalTime();
    pid_t processId = getpid();

    #if defined(__arm__) // ARM32 似乎不支持 gettid
    pid_t threadId = syscall(SYS_gettid);
    #else
    pid_t threadId = gettid();
    #endif

    // 生成增强的堆栈跟踪（使用指令指针）
    char stackTrace[4096] = {0};
    GenerateLinuxStackTrace(stackTrace, sizeof(stackTrace));
    
    // 构建异常信息
    snprintf(g_LastExceptionInfo, sizeof g_LastExceptionInfo,
              "======== 信号捕获 =========\n"
              "时间    : %s\n"
              "程序名称: %s\n"
              "进程ID  : %d\n"
              "线程ID  : %d\n"
              "信号类型: %d (%s)\n"
              "错误地址: %p\n"
              "指令指针: %p\n"
              "架构信息: %s\n"
              "错误代码: %d\n"
              "发送者ID: %d\n"
              "堆栈跟踪:\n%s\n",
              timeStr, exeName, processId, threadId,
              sig, GetSignalDescription(sig),
              info->si_addr, instruction_ptr,
              GetArchitectureInfo(),
              info->si_code, info->si_pid, stackTrace);
    
    // 输出到控制台和日志
    printf("\n%s\n", g_LastExceptionInfo);
    outLogToFile(g_LastExceptionInfo); 

    // 其他信号重新抛出以生成核心转储
    raise(sig);
}

static void SetupSignalHandlers(void)
{
    struct sigaction sa;
    sa.sa_sigaction = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    
    // 注册需要处理的信号
    int signals[] = {
        SIGSEGV, // 段错误
        SIGFPE,  // 算术异常
        SIGILL,  // 非法指令
        SIGBUS,  // 总线错误
        SIGABRT, // 中止信号
        SIGTERM, // 终止信号
        SIGINT,  // 中断信号 (CTRL+C) - 新增
    };
    
    for (size_t i = 0; i < sizeof(signals)/sizeof(signals[0]); i++) {
        if (sigaction(signals[i], &sa, &g_OldActions[signals[i]]) == -1) {
            printf("Failed to set handler for signal %d\n", signals[i]);
        }
    }
}

static void RestoreSignalHandlers(void)
{
    int signals[] = {SIGSEGV, SIGFPE, SIGILL, SIGBUS, SIGABRT, SIGTERM, SIGINT};
    for (size_t i = 0; i < sizeof(signals)/sizeof(signals[0]); i++) 
        sigaction(signals[i], &g_OldActions[signals[i]], NULL);
}

#endif  // _WIN32

#else
/******************** 禁用异常监控版本 ********************/

#ifdef _WIN32
/*================== Windows 禁用监控版本 ====================*/

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

static long WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS ExceptionInfo)
{
    LogExceptionInfo(ExceptionInfo, "VectoredExceptionHandler");
    return EXCEPTION_CONTINUE_SEARCH;
}

static long WINAPI UnhandledExceptionFilterA(PEXCEPTION_POINTERS ExceptionInfo)
{
    LogExceptionInfo(ExceptionInfo, "UnhandledExceptionFilter");
    return EXCEPTION_EXECUTE_HANDLER;
}

#else
/*================== Linux 禁用监控版本 ====================*/

static void LogExceptionInfo(int sig, siginfo_t *info, void *context)
{
    (void)context;
    const char *exeName = getExeName();
    const char *timeStr = getLocalTime();
    pid_t processId = getpid();
    
    snprintf(g_LastExceptionInfo, sizeof g_LastExceptionInfo,
              "[异常监控已禁用]\n"
              "时间  : %s\n"
              "进程  : %s\n"
              "程序ID: %d\n"
              "信号  : %d\n"
              "地址  : %p\n",
              timeStr, exeName, processId,
              sig, info->si_addr);
    
    printf("\n%s\n", g_LastExceptionInfo); 
    outLogToFile(g_LastExceptionInfo);
}

static void SignalHandler(int sig, siginfo_t *info, void *context)
{
    signal(sig, SIG_DFL);
    LogExceptionInfo(sig, info, context);
    raise(sig);
}

static void SetupSignalHandlers(void)
{
    struct sigaction sa;
    sa.sa_sigaction = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    
    int signals[] = {SIGSEGV, SIGFPE, SIGILL, SIGBUS, SIGABRT, SIGINT};
    
    for (size_t i = 0; i < sizeof(signals)/sizeof(signals[0]); i++) {
        sigaction(signals[i], &sa, NULL);
    }
}

#endif  // _WIN32
#endif  // ENABLE_EXCEPTION_MONITOR

/*================== ASAN 支持 =====================================*/
#ifdef __SANITIZE_ADDRESS__
// 声明 ASAN 接口函数
extern void __asan_set_error_report_callback(void (*callback)(const char*));

// ASAN 错误报告回调函数
static void ASANReportCallback(const char* report)
{
    FILE* logFile = fopen("process_exceptions.log", "a");
    if (logFile) {
        fprintf(logFile, "=== ASAN Report ===\n");
        fprintf(logFile, "%s\n", report);
        fprintf(logFile, "===================\n");
        fclose(logFile);
    }
    
    // 同时输出到控制台
    printf("=== ASAN Report ===\n");
    printf("%s\n", report);
    printf("===================\n");
}
#endif

/*================== 公共接口实现 ===================================*/
// 平台相关的异常处理设置
void ProcessExceptionMonitorInit(void)
{  
#ifdef __SANITIZE_ADDRESS__ // GCC 的 ASAN 宏
    __asan_set_error_report_callback(ASANReportCallback);
    printf("启用了 Address Sanitizer ASAN内存检测\n");
    printf("注意: ASAN版本性能较慢，建议仅用于调试\n");
    return;
#endif

#ifdef _WIN32   // Windows 特有的监控初始化  
    InitializeSymbolsSimple(); 
    SetUnhandledExceptionFilter(UnhandledExceptionFilterA);
    AddVectoredExceptionHandler(1, VectoredExceptionHandler); 
    
    // 新增：注册控制台事件处理
    if (SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        printf("控制台事件监控已启用 (CTRL+C, 窗口关闭等)\n");
    } else {
        printf("控制台事件监控设置失败，错误: %lu\n", GetLastError());
    }
    
#else           // Unix/Linux 系统
    SetupSignalHandlers();
#endif // _WIN32
}

void CleanupProcessExceptionMonitor(void)
{
#if ENABLE_EXCEPTION_MONITOR

#ifdef _WIN32
    // 移除控制台事件处理
    SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
    
    if (g_SymInitialized)
      SymCleanup(GetCurrentProcess());
#else
    RestoreSignalHandlers();
#endif

#endif  // ENABLE_EXCEPTION_MONITOR
  printf("异常监控已停止\n");
}



const char* GetLastExceptionInfo(void)
{
    return g_LastExceptionInfo;
}

