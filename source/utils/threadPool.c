/******************************************************************************
  * @file    文件 threadPool.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 线程池实现，使用方法类似于 multi_timer 
  * 
  ******************************************************************************
  * @attention 注意
  * 
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#define __USE_POSIX199309

#include "threadPool.h"
#include "platform.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <time.h>

// 全局线程池
ThreadPool * gThreadPool = NULL;

/*================== 本地数据类型   =========================================*/

// 线程池内部结构
struct ThreadPool {
  uint16_t num;
  ThreadTask* task_head;
  ThreadTask* task_tail;
  thread_t* workers;
  thread_t timer_thread;  // 保存定时器线程句柄
  uint32_t worker_count;
  uint32_t active_count;
  mutex_type task_mutex;
  mutex_type pool_mutex;
#ifdef _WIN32
  HANDLE timer_event;   // 定时器线程事件
#endif
  semaphore_t  semaphore;
  volatile bool destroy;
  uint64_t start_time_us;  // 线程池启动时间
};


// 全局线程池初始化
bool globalThreadPoolInit(uint16_t num)
{
  gThreadPool = threadPoolCreate(num);
  if (gThreadPool == NULL){
    platformCleanup(); 
    SafePrintf("Failed to create thread pool!\n");
  }
  
  return gThreadPool == NULL? false : true;
}

/**
 * @brief 获取线程池运行时间(微秒)
 */
static uint64_t getElapsedTimeUs(ThreadPool const* pool)
{
    return getTickUs() - pool->start_time_us;
}

/**
 * @brief 获取到期的任务
 */
static ThreadTask* getExpiredTask(ThreadPool* pool, uint64_t current_time_us)
{ 
  for (ThreadTask * prev = NULL, *task = pool->task_head; task != NULL; task = task->next) {
    if (task->next_execute_time_us <= current_time_us) {
      if (prev == NULL) { // 移除任务
        pool->task_head = task->next;
        if (pool->task_head == NULL) 
          pool->task_tail = NULL;
      } 
      else {
        prev->next = task->next;
        if (task->next == NULL) 
          pool->task_tail = prev;
      }
      task->next = NULL;
      return task;
    }
    prev = task;
  }
  return NULL;
}

/**
 * @brief 按时间排序插入任务
 */
static void insertTaskSorted(ThreadPool* pool, ThreadTask* task)
{
  if (pool->task_head == NULL) {
    pool->task_head = task;
    pool->task_tail = task;
    return;
  }
  
  // 插入头部
  if (task->next_execute_time_us < pool->task_head->next_execute_time_us) {
    task->next = pool->task_head;
    pool->task_head = task;
    return;
  }
  
  // 插入中间或尾部
  ThreadTask* prev = pool->task_head;
  ThreadTask* curr = prev->next;
  
  while (curr != NULL && curr->next_execute_time_us <= task->next_execute_time_us) {
    prev = curr;
    curr = curr->next;
  }
  
  prev->next = task;
  task->next = curr;
  if (curr == NULL) 
    pool->task_tail = task;
}

/**
 * @brief 工作线程函数
 */
static threadRet workerThread(void* arg)
{
  ThreadPool* pool = (ThreadPool*)arg;
  ThreadTask* task = NULL;
  
  while (!pool->destroy) {
    task = NULL;
    
    EnterCriticalSection_Wrapper(&pool->task_mutex);
    uint64_t nowUs = getElapsedTimeUs(pool);
    task = getExpiredTask(pool, nowUs);
    LeaveCriticalSection_Wrapper(&pool->task_mutex);
    
    if (task == NULL) { // 没有到期任务，等待信号量
      semaphoreWait(pool->semaphore);
      continue;
    }
       
    // 执行任务
    EnterCriticalSection_Wrapper(&pool->pool_mutex);
    pool->active_count++;
    LeaveCriticalSection_Wrapper(&pool->pool_mutex);
    
    // 执行回调
    task->task_cb( &task->argWrapper);
    task->executed_count++;
    
    EnterCriticalSection_Wrapper(&pool->pool_mutex);
    pool->active_count--;
    LeaveCriticalSection_Wrapper(&pool->pool_mutex);
    
    if (task->repeat == 0) // repeat == 0 的任务执行一次后丢弃
      continue;

    // 处理重复任务，重新计算下次执行时间。
    uint64_t new_time_us = getElapsedTimeUs(pool) + (uint64_t)task->repeat * 1000;
    task->next_execute_time_us = new_time_us;
    
    EnterCriticalSection_Wrapper(&pool->task_mutex);
    insertTaskSorted(pool, task);
    LeaveCriticalSection_Wrapper(&pool->task_mutex);
  }
  
  return (threadRet)0;
}

/**
 * @brief 定时器线程函数
 */
static threadRet timerThread(void* arg)
{
  ThreadPool* pool = (ThreadPool*)arg;
  uint64_t wait_time_us, now_us;
  
  while (!pool->destroy) {
    EnterCriticalSection_Wrapper(&pool->task_mutex);
    
    now_us = getElapsedTimeUs(pool);
    
    if (pool->task_head == NULL) {
      wait_time_us = 10000;  // 无任务时等待10ms
    } 
    else if (pool->task_head->next_execute_time_us > now_us) {
      wait_time_us = pool->task_head->next_execute_time_us - now_us;
      if (wait_time_us > 10000) 
        wait_time_us = 10000;  // 最多等待10ms
    }
    else 
      wait_time_us = 0;

    LeaveCriticalSection_Wrapper(&pool->task_mutex);
    
    if (wait_time_us == 0) { // 有到期任务，唤醒工作线程
      semaphorePost(pool->semaphore);
      preciseSleepUs(100);  // 短暂等待
    }
    else 
      preciseSleepUs(wait_time_us);
  }
  
  return (threadRet)0;
}

/**
 * @brief 创建线程池
 */
ThreadPool* threadPoolCreate(uint16_t num)
{
  ThreadPool* pool = (ThreadPool*)calloc(1, sizeof(ThreadPool));
  if (pool == NULL)
      return NULL;
  
  pool->num = num <= 5 ? 5 : num; 
  
  // 记录启动时间
  pool->start_time_us = getTickUs();
  
  // 初始化同步对象
  InitializeCriticalSection_Wrapper(&pool->task_mutex);
  InitializeCriticalSection_Wrapper(&pool->pool_mutex);
  
  // 创建信号量
  pool->semaphore = semaphoreCreate(0, num);
  if (pool->semaphore == NULL) {
    free(pool);
    return NULL;
  }
  
#ifdef _WIN32
  pool->timer_event = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (pool->timer_event == NULL) {
    semaphoreDestroy(pool->semaphore);
    DeleteCriticalSection_Wrapper(&pool->task_mutex);
    DeleteCriticalSection_Wrapper(&pool->pool_mutex);
    free(pool);
    return NULL;
  }
#endif
  
  // 创建工作线程数组
  pool->workers = (thread_t*)malloc(sizeof(thread_t) * num);
  if (pool->workers == NULL) {
#ifdef _WIN32
    CloseHandle(pool->timer_event);
#endif
    semaphoreDestroy(pool->semaphore);
    DeleteCriticalSection_Wrapper(&pool->task_mutex);
    DeleteCriticalSection_Wrapper(&pool->pool_mutex);
    free(pool);
    return NULL;
  }
  
  // 创建工作线程
  for (uint32_t i = 0; i < num; i++) {
    thread_t thread = threadCreate(NULL, (threadStartRoutine)workerThread, pool);
    if (thread == (thread_t)0) {
      pool->worker_count = i;
      pool->destroy = true;
      threadPoolDestroy(pool);
      return NULL;
    }
    pool->workers[i] = thread;
    pool->worker_count++;
  }
  
  // 创建定时器线程
  pool->timer_thread = threadCreate(NULL, (threadStartRoutine)timerThread, pool);
  if (pool->timer_thread == (thread_t)0) {
    pool->destroy = true;
    threadPoolDestroy(pool);
    return NULL;
  }
  // 注意：需要在销毁时等待这个线程
  
  pool->destroy = false;
  
  return pool;
}

/**
 * @brief 销毁线程池
 */
void threadPoolDestroy(ThreadPool* pool)
{
    if (pool == NULL) return;
    
    pool->destroy = true;
    
    // 唤醒所有工作线程
    for (uint32_t i = 0; i < pool->worker_count; i++)
        semaphorePost(pool->semaphore);
    
    // 等待工作线程结束
    for (uint32_t i = 0; i < pool->worker_count; i++) {
        WaitForSingleObject_Wrapper(pool->workers[i], INFINITE);
        CloseHandle(pool->workers[i]);
    }
    
    // 等待定时器线程结束
    if (pool->timer_thread) {
        WaitForSingleObject_Wrapper(pool->timer_thread, INFINITE);
        CloseHandle(pool->timer_thread);
    }
    
    // 清理资源
    free(pool->workers);
    semaphoreDestroy(pool->semaphore);
    
    DeleteCriticalSection_Wrapper(&pool->task_mutex);
    DeleteCriticalSection_Wrapper(&pool->pool_mutex);
    
    free(pool);
}

/**
 * @brief 初始化定时任务
 */
void threadTaskInit(ThreadTask* task, ThreadTaskCallback_t task_cb,
                      void* arg, uint32_t delay, uint32_t repeat)
{
  if (task == NULL) return;

  task->argWrapper.arg = arg;
  task->argWrapper.threadTask = task;
  task->task_cb = task_cb;
  task->delay = delay;
  task->repeat = repeat;
  task->executed_count = 0;
  // 注意： next_execute_time_us 需要在 start 时设置，因为需要知道线程池的启动时间
  task->next_execute_time_us = 0;
  task->next = NULL;
}

/**
 * @brief 启动定时任务
 */
bool threadTtaskStart(ThreadPool * pool, ThreadTask* task)
{
  if (pool == NULL || task == NULL || pool->destroy) 
    return false;
  
  EnterCriticalSection_Wrapper(&pool->task_mutex);
  
  // 检查是否已存在
  for(ThreadTask* curr = pool->task_head; curr != NULL; curr = curr->next)
    if (curr == task) {
      LeaveCriticalSection_Wrapper(&pool->task_mutex);
      return false;
    }
  // 计算下次执行时间（基于线程池启动时间）
  uint64_t now_us = getElapsedTimeUs(pool);
  task->next_execute_time_us = now_us + (uint64_t)task->delay * 1000;
  task->next = NULL;
  
  // 插入队列
  insertTaskSorted(pool, task);
  
  LeaveCriticalSection_Wrapper(&pool->task_mutex);
  
  // 唤醒工作线程
  semaphorePost(pool->semaphore);
  return true;
}

/**
 * @brief 停止定时任务
 */
void threadTtaskStop(ThreadPool * pool, ThreadTask* task)
{
  if (pool == NULL || task == NULL) return;
  
  EnterCriticalSection_Wrapper(&pool->task_mutex);
  
  task->repeat = 0; // 停止重复
  for (ThreadTask *prev, * curr = pool->task_head; curr != NULL; curr = curr->next){
    if (curr == task) { // 从链表中移除任务
      if (prev == NULL) { // 移除的是头节点
        pool->task_head = curr->next;
        if(pool->task_head == NULL) 
          pool->task_tail = NULL;
      }
      else {  // 移除的是中间或尾节点
        prev->next = curr->next;
        if (curr->next == NULL)
            pool->task_tail = prev;
      }

      task->next = NULL;// 重置任务指针
      break;
    }
    prev = curr; 
  }
  
  LeaveCriticalSection_Wrapper(&pool->task_mutex);
}

/**
 * @brief 重新初始化并启动定时任务
 */
void threadTaskReinit(ThreadPool* pool, ThreadTask* task,
                        ThreadTaskCallback_t task_cb, void* arg,
                        uint32_t delay, uint32_t repeat)
{
    if (pool == NULL || task == NULL) return;
    
    threadTtaskStop(pool, task);
    threadTaskInit(task, task_cb, arg, delay, repeat);
    threadTtaskStart(pool, task);
}

/**
 * @brief 获取当前活动线程数
 */
uint32_t threadPoolGetActiveCount(ThreadPool* pool)
{
    if (pool == NULL) return 0;
    return pool->active_count;
}

/**
 * @brief 获取待处理任务数
 */
uint32_t threadPoolGetPendingCount(ThreadPool* pool)
{
    if (pool == NULL) return 0;
    
    uint32_t count = 0;
    EnterCriticalSection_Wrapper(&pool->task_mutex);
    ThreadTask* task = pool->task_head;
    while (task != NULL) {
        count++;
        task = task->next;
    }
    LeaveCriticalSection_Wrapper(&pool->task_mutex);
    return count;
}




// 线程池精度测试，需要传递要测试的线程池指针
void threadPoolAccuracyTest(void * arg)
{
  if ( arg == NULL)
    return;
  static int counter = 0;
  counter++;

//static uint64_t last_expected_time_us = 0;
  static uint64_t total_error_us = 0;
  static uint64_t max_error_us = 0;
  static uint64_t min_error_us = UINT64_MAX;
  static uint32_t sample_count = 0;
  static uint64_t last_actual_time_us = 0;
  static uint64_t pool_start_monotonic_ms = 0;  // 线程池启动时的单调时钟
  static bool first_run = true;

  ThreadPool *myArg = ((ThreadPoolArgWrapper*)arg)->arg;
  ThreadTask *task = ((ThreadPoolArgWrapper*)arg)->threadTask;

  // 获取单调时钟时间（微秒）
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  uint64_t monotonic_us = (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
  uint64_t monotonic_ms = monotonic_us / 1000;
  
  // 获取线程池运行时间
  uint64_t pool_time_us = getElapsedTimeUs(myArg);
  uint64_t pool_time_ms = pool_time_us / 1000;
  
  // 第一次运行时记录线程池启动时的单调时钟
  if (first_run) {
    pool_start_monotonic_ms = monotonic_ms - pool_time_ms;
    first_run = false;
  }
  
  // 基于单调时钟的系统相对时间
  uint64_t system_relative_ms = monotonic_ms - pool_start_monotonic_ms;
  
  // 获取任务预期执行时间
  uint64_t expected_pool_time_us = task->next_execute_time_us;
  uint64_t expected_pool_time_ms = expected_pool_time_us / 1000;
  
  // 计算误差（使用单调时钟作为标准）
  int64_t abs_error_us = (int64_t)system_relative_ms * 1000 - (int64_t)expected_pool_time_us;
  
  // 更新统计
  sample_count++;
  uint64_t abs_error_abs = (abs_error_us > 0 ? abs_error_us : -abs_error_us);
  total_error_us += abs_error_abs;
  if (abs_error_abs > max_error_us) max_error_us = abs_error_abs;
  if (abs_error_abs < min_error_us) min_error_us = abs_error_abs;
  
  // 格式化显示
  uint32_t pool_hour = pool_time_ms / 3600000;
  uint32_t pool_min = (pool_time_ms % 3600000) / 60000;
  uint32_t pool_sec = (pool_time_ms % 60000) / 1000;
  uint32_t pool_ms = pool_time_ms % 1000;
  
  uint32_t exp_pool_hour = expected_pool_time_ms / 3600000;
  uint32_t exp_pool_min = (expected_pool_time_ms % 3600000) / 60000;
  uint32_t exp_pool_sec = (expected_pool_time_ms % 60000) / 1000;
  uint32_t exp_pool_ms = expected_pool_time_ms % 1000;
  
  uint32_t sys_rel_hour = system_relative_ms / 3600000;
  uint32_t sys_rel_min = (system_relative_ms % 3600000) / 60000;
  uint32_t sys_rel_sec = (system_relative_ms % 60000) / 1000;
  uint32_t sys_rel_ms = system_relative_ms % 1000;
  
  // 获取真实系统时间用于显示（修正时区）
  #ifdef _WIN32
    SYSTEMTIME st;
    GetSystemTime(&st);
    uint32_t real_hour = st.wHour;
    uint32_t real_min = st.wMinute;
    uint32_t real_sec = st.wSecond;
    uint32_t real_ms = st.wMilliseconds;
  #else
    struct timespec real_ts;
    struct tm* tm_info;
    clock_gettime(CLOCK_REALTIME, &real_ts);
    tm_info = localtime(&real_ts.tv_sec);
    uint32_t real_hour = tm_info->tm_hour;
    uint32_t real_min = tm_info->tm_min;
    uint32_t real_sec = tm_info->tm_sec;
    uint32_t real_ms = real_ts.tv_nsec / 1000000;
  #endif
  
  SafePrintf("\n========== Task Execution #%d ==========\n", counter);
  SafePrintf("Callback: count=%d, repeat=%d ms, executed=%u times\n", 
              counter, task->repeat, task->executed_count);
   
  SafePrintf("  预计执行时间: %02u:%02u:%02u.%03u (%"PRIu64" ms)\n", 
              exp_pool_hour, exp_pool_min, exp_pool_sec, exp_pool_ms,
              expected_pool_time_ms); 
  SafePrintf("  线 程 池计时: %02u:%02u:%02u.%03u (%"PRIu64" ms)\n", 
              pool_hour, pool_min, pool_sec, pool_ms,
              pool_time_ms);
  SafePrintf("  系统单调时钟: %02u:%02u:%02u.%03u (%"PRIu64" ms) 【基准】\n",
              sys_rel_hour, sys_rel_min, sys_rel_sec, sys_rel_ms,
              system_relative_ms);
  SafePrintf("  系统绝对时间: %02u:%02u:%02u.%03u\n", 
              real_hour, real_min, real_sec, real_ms);
  
  SafePrintf("\n【误差统计】\n");
  SafePrintf("  绝对误差:  %+.3f ms   (基于单调时钟)\n", abs_error_us / 1000.0);
  
  // 计算间隔误差
  if (task->repeat > 0 && last_actual_time_us > 0) {
    uint64_t actual_interval_us = system_relative_ms * 1000 - last_actual_time_us;
    uint64_t expected_interval_us = (uint64_t)task->repeat * 1000;
    int64_t interval_error_us = (int64_t)actual_interval_us - (int64_t)expected_interval_us;

    SafePrintf("  间隔误差:  %+.3f ms \n", interval_error_us / 1000.0);
    SafePrintf("  实际间隔:   %.3f ms (预期: %.3f ms)\n", 
            actual_interval_us / 1000.0, expected_interval_us / 1000.0);
    double avg_error_us = (double)total_error_us / sample_count;
    SafePrintf("\n【累计统计】(samples=%u)\n", sample_count);
    SafePrintf("  平均误差:   %.3f ms\n", avg_error_us / 1000.0);
    SafePrintf("  最小误差:   %.3f ms\n", min_error_us / 1000.0);
    SafePrintf("  最大误差:   %.3f ms\n", max_error_us / 1000.0);
  }

  // 记录本次执行时间用于下次间隔计算
  //last_expected_time_us = expected_time_us;  
  last_actual_time_us = system_relative_ms * 1000;

  // if( task->executed_count >= 50 )
  //     threadTtaskStop(gThreadPool, task);
}
