/******************************************************************************
  * @file    文件 platform.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 跨平台线程封装和一些高精度延迟和时间戳函数
  * 
  ******************************************************************************
  * @attention 注意
  * 
  *
  *******************************************************************************
*/


#ifndef _WIN32
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#endif

#include <stdlib.h>
#include "platform.h"

// 平台初始化
bool platformInitialize(void)
{
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    // Linux 不需要特殊的socket初始化
    // signal(SIGPIPE, SIG_IGN); // 忽略 SIGPIPE 信号
    return true;
#endif
}

void platformCleanup(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

// 创建线程
thread_t threadCreate(threadID_t *lpThreadId,
                        threadStartRoutine lpStartAddress,
                        void *lpParameter)
{
#ifdef _WIN32
    return CreateThread(NULL, 0, lpStartAddress, lpParameter, 0, lpThreadId);
#else
    pthread_t thread;
    int result = pthread_create(&thread, NULL, lpStartAddress, lpParameter);
    if( lpThreadId )
      *lpThreadId = thread;
    return (result != 0) ? (thread_t)0 : (thread_t)thread;
#endif
}

// 创建线程完整
thread_t threadCreateFull(void * lpThreadAttributes,
                                  size_t dwStackSize,
                                  threadStartRoutine lpStartAddress,
                                  void * lpParameter,
                                  uint32_t dwCreationFlags,
                                  threadID_t* lpThreadId)
{
#ifdef _WIN32
    return CreateThread(lpThreadAttributes, dwStackSize, 
                        lpStartAddress, lpParameter, 
                        dwCreationFlags,  lpThreadId);
#else
    pthread_t thread;
    pthread_attr_t attr;
    int result;
    
    (void)lpThreadAttributes; // 未使用参数
    (void)dwCreationFlags;    // 未使用参数
    
    pthread_attr_init(&attr);
    
    if (dwStackSize) 
        pthread_attr_setstacksize(&attr, dwStackSize);
    
    // 设置线程为可连接状态
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    
    result = pthread_create(&thread, &attr, lpStartAddress, lpParameter);
    pthread_attr_destroy(&attr);

    if (lpThreadId)
        *lpThreadId = thread;
    
    return (result != 0) ? (thread_t)0 : (thread_t)thread;
#endif
}

// 等待线程结束
DWORD WaitForSingleObject_Wrapper(thread_t hHandle, uint32_t dwMilliseconds)
{
#ifdef _WIN32
    return WaitForSingleObject(hHandle, dwMilliseconds);
#else
    if (hHandle == (thread_t)0)
        return WAIT_FAILED;
    
    if (dwMilliseconds == INFINITE) {
        return pthread_join((pthread_t)hHandle, NULL) 
                == 0 ? WAIT_OBJECT_0 : WAIT_FAILED;
    }

    // 简单的超时等待实现
    struct timespec start, current;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    while (true) {
        int result = pthread_tryjoin_np((pthread_t)hHandle, NULL);
        if (result == 0) {
            return WAIT_OBJECT_0;
        } 
        else if (result != EBUSY) {
            return WAIT_FAILED;
        }
        
        // 检查超时
        clock_gettime(CLOCK_MONOTONIC, &current);
        uint64_t elapsed_ms = (current.tv_sec - start.tv_sec) * 1000 + 
                              (current.tv_nsec - start.tv_nsec) / 1000000;
        
        if (elapsed_ms >= dwMilliseconds)
            return WAIT_TIMEOUT;
        
        usleep(10000); // 10ms
    }
    
#endif
}


/**
 * @brief 信号量操作
 */
void* semaphoreCreate(int initialCount, int maxCount)
{
#ifdef _WIN32
    return CreateSemaphore(NULL, initialCount, maxCount, NULL);
#else
    (void)maxCount; // Linux的信号量不需要指定最大值
    sem_t* sem = (sem_t*)malloc(sizeof(sem_t));
    if (sem) sem_init(sem, 0, initialCount);
    return sem;
#endif
}

void semaphoreDestroy(void* sem)
{
    if (!sem) return;
#ifdef _WIN32
    CloseHandle((HANDLE)sem);
#else
    sem_destroy((sem_t*)sem);
    free(sem);
#endif
}

void semaphorePost(void* sem)
{
#ifdef _WIN32
    ReleaseSemaphore((HANDLE)sem, 1, NULL);
#else
    sem_post((sem_t*)sem);
#endif
}

void semaphoreWait(void* sem)
{
#ifdef _WIN32
    WaitForSingleObject((HANDLE)sem, INFINITE);
#else
    sem_wait((sem_t*)sem);
#endif
}
/**
 * @brief 获取当前高精度时间戳(毫秒)
 */
uint64_t getTickMs(void)
{
  return getTickUs() / 1000;
}

/**
 * @brief 获取当前高精度时间戳(微秒)
 */
uint64_t getTickUs(void)
{
#ifdef _WIN32
  LARGE_INTEGER frequency;
  LARGE_INTEGER counter;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&counter);
  return (uint64_t)((counter.QuadPart * 1000000) / frequency.QuadPart);
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}

/**
 * @brief 高精度微秒级睡眠
 */
void preciseSleepUs(uint64_t us)
{
  if (us == 0) return;
    
#ifdef _WIN32 // Windows 使用高精度等待
    LARGE_INTEGER due_time;
    due_time.QuadPart = -((LONGLONG)us * 10);  // 100ns单位
    HANDLE timer = CreateWaitableTimer(NULL, TRUE, NULL);
    if (timer) {
        SetWaitableTimer(timer, &due_time, 0, NULL, NULL, FALSE);
        WaitForSingleObject(timer, INFINITE);
        CloseHandle(timer);
    } 
    else {
        Sleep((DWORD)(us / 1000));
    }
#else // Linux 使用 nanosleep
    struct timespec ts;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (us % 1000000) * 1000;
    nanosleep(&ts, NULL);
#endif
}
