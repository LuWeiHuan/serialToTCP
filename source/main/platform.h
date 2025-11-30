#ifndef __PLATFORM_H_
#define __PLATFORM_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
    // Windows 特定头文件和定义
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <windows.h>
    
    #define PRIu64 "I64u"
    
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE  INVALID_SOCKET
    #define closeSocket           closesocket
    #define SOCKET_ERROR_CODE     WSAGetLastError()

    typedef HANDLE      thread_t;
    typedef DWORD       threadID_t;
    typedef CRITICAL_SECTION      mutex_type; 
    typedef LPTHREAD_START_ROUTINE threadStartRoutine;
    typedef DWORD     threadRet;
#else
    // Linux 特定头文件和定义
    #include <pthread.h>
    #include <errno.h>

    #ifndef __arm__
    #define PRIu64 "lu"
    #else
    #define PRIu64 "llu"
    #endif

    typedef int socket_t;
    #define INVALID_SOCKET_VALUE      (-1)
    #define SOCKET_ERROR_CODE         errno 
    #define SOCKET_ERROR	            (-1)
    #define INVALID_HANDLE_VALUE      (-1)
    #define closeSocket               close

    #define WSAEWOULDBLOCK            EAGAIN
    #define WSAECONNRESET             ECONNRESET
    #define WSAECONNABORTED           ECONNABORTED

    // 线程函数类型
    typedef pthread_t threadID_t;
    typedef pthread_t thread_t;
    typedef pthread_mutex_t mutex_type;
    typedef void* threadRet;
    typedef void* (*threadStartRoutine)(void*);
    #define WINAPI

    // Windows 类型兼容定义 
    typedef unsigned long DWORD;
    
    // Windows API 兼容函数
    #define Sleep(ms)     usleep((ms) * 1000)
    #define strnicmp      strncasecmp
    
    // Windows 常量兼容定义
    #define INFINITE      0xFFFFFFFF
    #define WAIT_OBJECT_0 0
    #define WAIT_TIMEOUT  258
    #define WAIT_FAILED   0xFFFFFFFF
    
    #define MAX_PATH      260

    // Linux 中 pthread_t 不需要显式关闭
    #define CloseHandle(noIn)   true  
#endif


#ifdef __cplusplus  
extern "C" {
#endif

// 平台初始化函数
bool Platform_Initialize(void);
void Platform_Cleanup(void);

// 线程管理函数
thread_t threadCreate(threadID_t* lpThreadId,
                      threadStartRoutine lpStartAddress,
                      void *lpParameter);
// 线程管理函数完整
thread_t threadCreateFull(void * lpThreadAttributes,
                          size_t dwStackSize,
                          threadStartRoutine lpStartAddress,
                          void *lpParameter,
                          uint32_t dwCreationFlags,
                          threadID_t* lpThreadId);
// 等待线程结束
DWORD WaitForSingleObject_Wrapper(thread_t hHandle, uint32_t dwMilliseconds);


// 初始化临界区
static inline void InitializeCriticalSection_Wrapper(mutex_type *mutex)
{
#ifdef _WIN32
    InitializeCriticalSection(mutex);
#else
    pthread_mutex_init(mutex, NULL);
#endif
}

// 进入临界区
static inline void EnterCriticalSection_Wrapper(mutex_type *mutex)
{
#ifdef _WIN32
    EnterCriticalSection(mutex);
#else
    pthread_mutex_lock(mutex);
#endif
}

// 离开临界区
static inline void LeaveCriticalSection_Wrapper(mutex_type *mutex)
{
#ifdef _WIN32
    LeaveCriticalSection(mutex);
#else
    pthread_mutex_unlock(mutex);
#endif
}

// 删除临界区
static inline void DeleteCriticalSection_Wrapper(mutex_type *mutex)
{
#ifdef _WIN32
    DeleteCriticalSection(mutex);
#else
    pthread_mutex_destroy(mutex);
#endif
}

// 获取当前线程ID
static inline threadID_t GetCurrentThreadId_Wrapper(void)
{
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return pthread_self();
#endif
}

#ifndef _WIN32 
// 获取最后错误代码 
static inline DWORD GetLastError(void)  { return errno; }
#endif

#ifdef __cplusplus
}
#endif


#endif /* __PLATFORM_H_ */