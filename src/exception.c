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
 

#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <stdlib.h>

// 正确的跨平台CONTEXT成员访问宏
#ifdef _M_IX86
#define GET_CONTEXT_EIP(context) ((context)->Eip)
#define GET_CONTEXT_EBP(context) ((context)->Ebp)
#define GET_CONTEXT_ESP(context) ((context)->Esp)
#elif _M_X64
#define GET_CONTEXT_EIP(context) ((context)->Rip)
#define GET_CONTEXT_EBP(context) ((context)->Rbp)
#define GET_CONTEXT_ESP(context) ((context)->Rsp)
#else
#define GET_CONTEXT_EIP(context) (0)
#define GET_CONTEXT_EBP(context) (0)
#define GET_CONTEXT_ESP(context) (0)
#endif

// 简单的堆栈跟踪函数
void SimpleStackTrace(CONTEXT* context) {
    _tprintf(_T("\n=== Simple Stack Trace ===\n"));
    
    // 获取关键的寄存器值
    DWORD64 pc = GET_CONTEXT_EIP(context);
    DWORD64 frame = GET_CONTEXT_EBP(context);
    DWORD64 stack = GET_CONTEXT_ESP(context);
    
    _tprintf(_T("Program Counter (EIP/RIP): 0x%I64X\n"), pc);
    _tprintf(_T("Frame Pointer (EBP/RBP): 0x%I64X\n"), frame);
    _tprintf(_T("Stack Pointer (ESP/RSP): 0x%I64X\n"), stack);
    
    // 尝试读取返回地址
    if (frame != 0) {
        DWORD64 returnAddress = 0;
        SIZE_T bytesRead = 0;
        
        // 在x64上，返回地址通常在RBP+8的位置
        if (ReadProcessMemory(GetCurrentProcess(), 
                            (LPCVOID)(frame + sizeof(DWORD64)), 
                            &returnAddress, sizeof(returnAddress), &bytesRead) &&
            bytesRead == sizeof(returnAddress)) {
            _tprintf(_T("Return Address: 0x%I64X\n"), returnAddress);
        } else {
            _tprintf(_T("Failed to read return address\n"));
        }
    }
    
    _tprintf(_T("\nUse these commands to debug:\n"));
    _tprintf(_T("addr2line -e com2tcp_server.exe -f -C 0x%I64X\n"), pc);
    
    if (frame != 0) {
        DWORD64 returnAddress = 0;
        SIZE_T bytesRead = 0;
        if (ReadProcessMemory(GetCurrentProcess(), 
                            (LPCVOID)(frame + sizeof(DWORD64)), 
                            &returnAddress, sizeof(returnAddress), &bytesRead) &&
            bytesRead == sizeof(returnAddress)) {
            _tprintf(_T("addr2line -e com2tcp_server.exe -f -C 0x%I64X\n"), returnAddress);
        }
    }
}

// 生成调试信息文件
void GenerateDebugInfo(EXCEPTION_POINTERS* ExceptionInfo) {
    FILE* f = fopen("crash_report.txt", "w");
    if (f) {
        fprintf(f, "=== Crash Report ===\n");
        fprintf(f, "Exception Code: 0x%08lX\n", ExceptionInfo->ExceptionRecord->ExceptionCode);
        fprintf(f, "Exception Address: 0x%I64X\n", (DWORD64)ExceptionInfo->ExceptionRecord->ExceptionAddress);
        
        DWORD64 pc = GET_CONTEXT_EIP(ExceptionInfo->ContextRecord);
        fprintf(f, "Program Counter: 0x%I64X\n", pc);
        fprintf(f, "Frame Pointer: 0x%I64X\n", GET_CONTEXT_EBP(ExceptionInfo->ContextRecord));
        fprintf(f, "Stack Pointer: 0x%I64X\n", GET_CONTEXT_ESP(ExceptionInfo->ContextRecord));
        
        // 保存寄存器状态
#ifdef _M_IX86
        fprintf(f, "\nRegisters:\n");
        fprintf(f, "EAX: 0x%08lX\n", ExceptionInfo->ContextRecord->Eax);
        fprintf(f, "EBX: 0x%08lX\n", ExceptionInfo->ContextRecord->Ebx);
        fprintf(f, "ECX: 0x%08lX\n", ExceptionInfo->ContextRecord->Ecx);
        fprintf(f, "EDX: 0x%08lX\n", ExceptionInfo->ContextRecord->Edx);
        fprintf(f, "ESI: 0x%08lX\n", ExceptionInfo->ContextRecord->Esi);
        fprintf(f, "EDI: 0x%08lX\n", ExceptionInfo->ContextRecord->Edi);
#elif _M_X64
        fprintf(f, "\nRegisters:\n");
        fprintf(f, "RAX: 0x%I64X\n", ExceptionInfo->ContextRecord->Rax);
        fprintf(f, "RBX: 0x%I64X\n", ExceptionInfo->ContextRecord->Rbx);
        fprintf(f, "RCX: 0x%I64X\n", ExceptionInfo->ContextRecord->Rcx);
        fprintf(f, "RDX: 0x%I64X\n", ExceptionInfo->ContextRecord->Rdx);
        fprintf(f, "RSI: 0x%I64X\n", ExceptionInfo->ContextRecord->Rsi);
        fprintf(f, "RDI: 0x%I64X\n", ExceptionInfo->ContextRecord->Rdi);
        fprintf(f, "R8:  0x%I64X\n", ExceptionInfo->ContextRecord->R8);
        fprintf(f, "R9:  0x%I64X\n", ExceptionInfo->ContextRecord->R9);
        fprintf(f, "R10: 0x%I64X\n", ExceptionInfo->ContextRecord->R10);
        fprintf(f, "R11: 0x%I64X\n", ExceptionInfo->ContextRecord->R11);
        fprintf(f, "R12: 0x%I64X\n", ExceptionInfo->ContextRecord->R12);
        fprintf(f, "R13: 0x%I64X\n", ExceptionInfo->ContextRecord->R13);
        fprintf(f, "R14: 0x%I64X\n", ExceptionInfo->ContextRecord->R14);
        fprintf(f, "R15: 0x%I64X\n", ExceptionInfo->ContextRecord->R15);
#endif
        
        fclose(f);
        _tprintf(_T("Debug info saved to crash_report.txt\n"));
    }
}

// 异常过滤器函数
LONG WINAPI ExceptionFilter(EXCEPTION_POINTERS* ExceptionInfo) {
    _tprintf(_T("\n=== Unhandled Exception Caught ===\n"));
    
    // 输出异常信息
    switch (ExceptionInfo->ExceptionRecord->ExceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:
            _tprintf(_T("Exception: EXCEPTION_ACCESS_VIOLATION\n"));
            if (ExceptionInfo->ExceptionRecord->NumberParameters >= 2) {
                _tprintf(_T("Attempt to %s address 0x%I64X\n"),
                        (ExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 0) ? 
                        _T("read") : _T("write"),
                        (DWORD64)ExceptionInfo->ExceptionRecord->ExceptionInformation[1]);
            }
            break;
            
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            _tprintf(_T("Exception: EXCEPTION_INT_DIVIDE_BY_ZERO\n"));
            break;
            
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            _tprintf(_T("Exception: EXCEPTION_FLT_DIVIDE_BY_ZERO\n"));
            break;
            
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            _tprintf(_T("Exception: EXCEPTION_ILLEGAL_INSTRUCTION\n"));
            break;
            
        case EXCEPTION_STACK_OVERFLOW:
            _tprintf(_T("Exception: EXCEPTION_STACK_OVERFLOW\n"));
            break;
            
        default:
            _tprintf(_T("Exception: 0x%08lX\n"), 
                    ExceptionInfo->ExceptionRecord->ExceptionCode);
            break;
    }
    
    DWORD64 exceptionAddress = (DWORD64)ExceptionInfo->ExceptionRecord->ExceptionAddress;
    _tprintf(_T("Exception Address: 0x%I64X\n"), exceptionAddress);
    
    // 简单堆栈跟踪
    SimpleStackTrace(ExceptionInfo->ContextRecord);
    
    // 生成调试信息文件
    GenerateDebugInfo(ExceptionInfo);
    
    _tprintf(_T("\n=== Debugging Tips ===\n"));
    
    DWORD64 pc = GET_CONTEXT_EIP(ExceptionInfo->ContextRecord);
    if (pc > 0x00007FFFFFFF) {
        _tprintf(_T("WARNING: Program counter is in very high memory (0x%I64X)\n"), pc);
        _tprintf(_T("This usually indicates:\n"));
        _tprintf(_T("1. Stack corruption (return address overwritten)\n"));
        _tprintf(_T("2. Invalid function pointer call\n"));
        _tprintf(_T("3. Buffer overflow\n"));
        _tprintf(_T("4. Memory corruption\n"));
    }
    
    _tprintf(_T("\n1. Make sure com2tcp_server.exe was compiled with -g option\n"));
    _tprintf(_T("2. Use the map file to analyze the crash address\n"));
    _tprintf(_T("3. Check crash_report.txt for detailed information\n"));
    _tprintf(_T("4. Look for buffer overflows or invalid pointer operations\n"));
    
    return EXCEPTION_EXECUTE_HANDLER;
}

void SetupExceptionHandler() {
    SetUnhandledExceptionFilter(ExceptionFilter);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

// 测试函数
void TestFunction() {
    _tprintf(_T("Testing exception handling...\n"));
    
    // 测试访问违规
    int* ptr = NULL;
    *ptr = 42;  // 这里会触发异常
}

int mainA() {
    SetupExceptionHandler();
    
    _tprintf(_T("Exception Handler Demo - Fixed Version\n"));
    _tprintf(_T("Process ID: %ld\n"), GetCurrentProcessId());
    
#ifdef _M_IX86
    _tprintf(_T("Architecture: 32-bit\n"));
#elif _M_X64
    _tprintf(_T("Architecture: 64-bit\n"));
#else
    _tprintf(_T("Architecture: Unknown\n"));
#endif
    
    // 调用测试函数
    TestFunction();
    
    _tprintf(_T("Normal exit - this should not be reached\n"));
    return 0;
}