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

#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <tchar.h>
#include <stdlib.h>
/*================== 本地数据类型   =========================================*/
/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量   =========================================*/
/*================== 本地常量声明   =========================================*/
/*================== 本地变量声明   =========================================*/
/*================== 本地函数声明   =========================================*/

/*================== 外部函数声明   =========================================*/
/*================== 外部变量声明   =========================================*/
 
/**
 * @brief 
 * @param 
 * @param 
 * @param 
 * @return
 * @attention
 */
 



#define MAX_SYMBOL_NAME 256

// 全局变量
HANDLE g_hProcess = NULL;
BOOL g_symbolsInitialized = FALSE;

// 初始化符号处理
BOOL InitSymbols() {
    g_hProcess = GetCurrentProcess();
    
    // 设置符号选项
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
    
    // 初始化符号处理器 - 使用当前进程的搜索路径
    if (!SymInitialize(g_hProcess, NULL, TRUE)) {
        _tprintf(_T("SymInitialize failed. Error: %ld\n"), GetLastError());
        
        // 尝试另一种方式：使用当前目录
        TCHAR currentDir[MAX_PATH];
        if (GetCurrentDirectory(MAX_PATH, currentDir)) {
            if (SymInitialize(g_hProcess, currentDir, TRUE)) {
                _tprintf(_T("SymInitialize succeeded with current directory: %s\n"), currentDir);
                g_symbolsInitialized = TRUE;
                return TRUE;
            }
        }
        
        return FALSE;
    }
    
    g_symbolsInitialized = TRUE;
    _tprintf(_T("Symbol handler initialized successfully\n"));
    return TRUE;
}

// 清理符号处理
void CleanupSymbols() {
    if (g_hProcess && g_symbolsInitialized) {
        SymCleanup(g_hProcess);
        g_symbolsInitialized = FALSE;
    }
}

// 获取模块名称
LPTSTR GetModuleName(DWORD64 address) {
    static TCHAR moduleName[MAX_PATH] = _T("unknown");
    IMAGEHLP_MODULE64 moduleInfo;
    ZeroMemory(&moduleInfo, sizeof(moduleInfo));
    moduleInfo.SizeOfStruct = sizeof(moduleInfo);
    
    if (SymGetModuleInfo64(g_hProcess, address, &moduleInfo)) {
        _tcscpy(moduleName, moduleInfo.ModuleName);
    } else {
        HMODULE hModule = NULL;
        if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, 
                            (LPCTSTR)address, &hModule)) {
            TCHAR path[MAX_PATH];
            if (GetModuleFileName(hModule, path, MAX_PATH)) {
                TCHAR* name = _tcsrchr(path, _T('\\'));
                if (name) name++;
                else name = path;
                _tcscpy(moduleName, name);
            }
        }
    }
    
    return moduleName;
}

// 获取函数名称和位移
void GetFunctionInfo(DWORD64 address, LPTSTR funcName, DWORD nameSize, DWORD64* displacement) {
    if (!g_symbolsInitialized) {
        _tcscpy(funcName, _T("Unknown Function"));
        if (displacement) *displacement = 0;
        return;
    }
    
    BYTE symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYMBOL_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO symbol = (PSYMBOL_INFO)symbolBuffer;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYMBOL_NAME;
    
    if (SymFromAddr(g_hProcess, address, displacement, symbol)) {
        _tcsncpy(funcName, symbol->Name, nameSize - 1);
        funcName[nameSize - 1] = _T('\0');
    } else {
        _tcscpy(funcName, _T("Unknown Function"));
        if (displacement) *displacement = 0;
    }
}

// 获取源代码行信息
BOOL GetSourceLine(DWORD64 address, LPTSTR fileName, DWORD fileSize, PDWORD lineNumber) {
    if (!g_symbolsInitialized) {
        return FALSE;
    }
    
    IMAGEHLP_LINE64 line;
    ZeroMemory(&line, sizeof(line));
    line.SizeOfStruct = sizeof(line);
    
    DWORD displacement = 0;
    if (SymGetLineFromAddr64(g_hProcess, address, &displacement, &line)) {
        _tcsncpy(fileName, line.FileName, fileSize - 1);
        fileName[fileSize - 1] = _T('\0');
        *lineNumber = line.LineNumber;
        return TRUE;
    }
    
    return FALSE;
}

// 打印堆栈跟踪
void PrintStackTrace(CONTEXT* context) {
    STACKFRAME64 stackFrame;
    ZeroMemory(&stackFrame, sizeof(stackFrame));
    
#ifdef _M_IX86
    DWORD machineType = IMAGE_FILE_MACHINE_I386;
    stackFrame.AddrPC.Offset = context->Eip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context->Ebp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context->Esp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#elif _M_X64
    DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
    stackFrame.AddrPC.Offset = context->Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context->Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context->Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#else
    _tprintf(_T("Unsupported architecture\n"));
    return;
#endif

    _tprintf(_T("\nStack Trace:\n"));
    
    for (int frameNum = 0; frameNum < 50; frameNum++) {
        if (!StackWalk64(machineType, g_hProcess, GetCurrentThread(), 
                        &stackFrame, context, NULL,
                        SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
            break;
        }
        
        if (stackFrame.AddrPC.Offset == 0) {
            break;
        }
        
        DWORD64 displacement = 0;
        TCHAR funcName[MAX_SYMBOL_NAME];
        GetFunctionInfo(stackFrame.AddrPC.Offset, funcName, MAX_SYMBOL_NAME, &displacement);
        
        LPTSTR moduleName = GetModuleName(stackFrame.AddrPC.Offset);
        
        TCHAR fileName[MAX_PATH] = _T("");
        DWORD lineNumber = 0;
        BOOL hasSource = GetSourceLine(stackFrame.AddrPC.Offset, fileName, MAX_PATH, &lineNumber);
        
        _tprintf(_T("#%02d 0x%08lX"), frameNum, (DWORD)stackFrame.AddrPC.Offset);
        
        if (_tcscmp(moduleName, _T("unknown")) != 0) {
            _tprintf(_T(" %s!"), moduleName);
        }
        
        _tprintf(_T("%s"), funcName);
        
        if (displacement > 0) {
            _tprintf(_T(" + 0x%lX"), (DWORD)displacement);
        }
        
        if (hasSource) {
            // 只显示文件名，不显示完整路径
            TCHAR* shortName = _tcsrchr(fileName, _T('\\'));
            if (shortName) shortName++;
            else shortName = fileName;
            
            _tprintf(_T(" [%s:%ld]"), shortName, lineNumber);
        }
        
        _tprintf(_T("\n"));
    }
}

// 生成minidump文件（可选）
void CreateMiniDump(EXCEPTION_POINTERS* ExceptionInfo) {
    TCHAR dumpPath[MAX_PATH];
    _stprintf(dumpPath, _T("%s\\crash_dump_%ld.dmp.txt"), _T("."), GetCurrentProcessId());
    
    HANDLE hFile = CreateFile(dumpPath, GENERIC_WRITE, 0, NULL, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = ExceptionInfo;
        mdei.ClientPointers = FALSE;
        
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), 
                         hFile, MiniDumpNormal, 
                         ExceptionInfo ? &mdei : NULL, NULL, NULL);
        
        CloseHandle(hFile);
        _tprintf(_T("Minidump created: %s\n"), dumpPath);
    }
}

// 异常过滤器函数
LONG WINAPI ExceptionFilter(EXCEPTION_POINTERS* ExceptionInfo) {
    _tprintf(_T("\n=== Unhandled Exception Caught ===\n"));
    
    // 输出异常信息
    switch (ExceptionInfo->ExceptionRecord->ExceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:
            _tprintf(_T("Exception: EXCEPTION_ACCESS_VIOLATION\n"));
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            _tprintf(_T("Exception: EXCEPTION_INT_DIVIDE_BY_ZERO\n"));
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            _tprintf(_T("Exception: EXCEPTION_FLT_DIVIDE_BY_ZERO\n"));
            break;
        default:
            _tprintf(_T("Exception: 0x%08lX\n"), 
                    ExceptionInfo->ExceptionRecord->ExceptionCode);
            break;
    }
    
    // 初始化符号处理（如果还没初始化）
    if (!g_symbolsInitialized) {
        InitSymbols();
    }
    
    // 打印堆栈跟踪
    PrintStackTrace(ExceptionInfo->ContextRecord);
    
    // 创建minidump文件
    CreateMiniDump(ExceptionInfo);
    
    _tprintf(_T("\n=== Application terminated ===\n"));
    
    return EXCEPTION_EXECUTE_HANDLER;
}

// 设置异常处理器
void SetupExceptionHandler1(void) {
    // 初始化符号处理
    InitSymbols();
    
    // 设置未处理异常过滤器
    SetUnhandledExceptionFilter(ExceptionFilter);
    
    // 禁用Windows错误报告对话框
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

// 测试函数
void TestFunction() {
    _tprintf(_T("Testing exception handling...\n"));
    int zero = 0;
    int result = 100 / zero;  // 这里会触发异常
    if( result || zero ){}
}

int _tmainA(int argc, _TCHAR* argv[]) {
    if( argc || argv){}
    SetupExceptionHandler();
    
    _tprintf(_T("Exception Handler Demo\n"));
    _tprintf(_T("Process ID: %ld\n"), GetCurrentProcessId());
    
    // 调用测试函数
    TestFunction();
    
    CleanupSymbols();
    return 0;
}




#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <stdlib.h>

// MinGW兼容的堆栈遍历实现
typedef struct _STACK_FRAME {
    DWORD64 AddressPC;
    DWORD64 AddressReturn;
    DWORD64 AddressFrame;
    DWORD64 AddressStack;
} STACK_FRAME, *PSTACK_FRAME;

// 获取当前堆栈帧
BOOL GetStackFrame(CONTEXT* context, PSTACK_FRAME stackFrame) {
#ifdef _M_IX86
    stackFrame->AddressPC = context->Eip;
    stackFrame->AddressFrame = context->Ebp;
    stackFrame->AddressStack = context->Esp;
    stackFrame->AddressReturn = *(DWORD64*)(context->Ebp + 4);
    return TRUE;
#elif _M_X64
    stackFrame->AddressPC = context->Rip;
    stackFrame->AddressFrame = context->Rbp;
    stackFrame->AddressStack = context->Rsp;
    stackFrame->AddressReturn = *(DWORD64*)(context->Rbp + 8);
    return TRUE;
#else
    return FALSE;
#endif
}

// 简单的堆栈跟踪函数
void SimpleStackTrace(CONTEXT* context) {
    STACK_FRAME stackFrame;
    GetStackFrame(context, &stackFrame);
    
    _tprintf(_T("\n=== Simple Stack Trace ===\n"));
    _tprintf(_T("PC: 0x%08lX\n"), (DWORD)stackFrame.AddressPC);
    _tprintf(_T("Return: 0x%08lX\n"), (DWORD)stackFrame.AddressReturn);
    _tprintf(_T("Frame: 0x%08lX\n"), (DWORD)stackFrame.AddressFrame);
    
    // 手动解析一些常见的地址模式
    _tprintf(_T("\nTry these addr2line commands:\n"));
    _tprintf(_T("addr2line -e com2tcp_server.exe -f -C 0x%08lX\n"), (DWORD)stackFrame.AddressPC);
    _tprintf(_T("addr2line -e com2tcp_server.exe -f -C 0x%08lX\n"), (DWORD)stackFrame.AddressReturn);
}

// 生成map文件用于后续分析
void GenerateMapFileHint() {
    _tprintf(_T("\n=== For better debugging, compile with:\n"));
    _tprintf(_T("gcc -g -Wl,-Map=com2tcp_server.map -o com2tcp_server.exe your_files.c\n"));
    _tprintf(_T("Then use the map file to match addresses to functions\n"));
}

// 异常过滤器函数
LONG WINAPI ExceptionFilterB(EXCEPTION_POINTERS* ExceptionInfo) {
    _tprintf(_T("\n=== Unhandled Exception Caught ===\n"));
    
    switch (ExceptionInfo->ExceptionRecord->ExceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:
            _tprintf(_T("Exception: EXCEPTION_ACCESS_VIOLATION\n"));
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            _tprintf(_T("Exception: EXCEPTION_INT_DIVIDE_BY_ZERO\n"));
            break;
        default:
            _tprintf(_T("Exception: 0x%08lX\n"), 
                    ExceptionInfo->ExceptionRecord->ExceptionCode);
            break;
    }
    
    // 简单堆栈跟踪
    SimpleStackTrace(ExceptionInfo->ContextRecord);
    
    // 生成调试提示
    GenerateMapFileHint();
    
    // 保存关键信息到文件
    FILE* f = fopen("crash_info.txt", "w");
    if (f) {
        fprintf(f, "Exception: 0x%08lX\n", ExceptionInfo->ExceptionRecord->ExceptionCode);
        fprintf(f, "Address: 0x%08lX\n", *(DWORD*)ExceptionInfo->ExceptionRecord->ExceptionAddress);
        fprintf(f, "PC: 0x%08lX\n", (DWORD)ExceptionInfo->ContextRecord->Rip);
        fclose(f);
    }
    
    return EXCEPTION_EXECUTE_HANDLER;
}

void SetupExceptionHandler(void) {
    SetUnhandledExceptionFilter(ExceptionFilterB);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}
 