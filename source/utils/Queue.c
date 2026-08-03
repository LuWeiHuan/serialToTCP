/******************************************************************************
  * @file    文件 Queue.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "Queue.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/
static threadRet WINAPI AsyncQueueThreadProc(void *);



/*=============================================================================
 功   能：初始化启动队列线程，专门异步处理数据
 参   数：queue             队列结构体指针（必须有）
          outDataCallBack   出数据回调函数（必须有）
          queueNum          队列数量，范围选择要在 MIN_QUEUE_SIZE 和 MAX_QUEUE_SIZE 之间
          elementSize       数据最大长度，小于 10 会使用默认大小 DEFAULT_ELEMENT_SIZE 
          queueName         队列名字，可以为空
 返   回：成功返回真，失败返回假
 描   述：无
=============================================================================*/
bool startAsyncQueue(AsyncQueue_t *queue, outDataCallBack_t CallBack, 
    uint16_t queueNum, uint32_t elementSize, const char *queueName) 
{
  if( queue == NULL || CallBack == NULL )
    return false;
  
  if (queueNum < MIN_QUEUE_SIZE || MAX_QUEUE_SIZE < queueNum) {
      SafePrintf("Invalid queue Num: %d/%d ~ %d, Name: %s\n", 
          queueNum, MIN_QUEUE_SIZE, MAX_QUEUE_SIZE, queueName?queueName:"unknown");
      return false;
  }

  if( elementSize < 10 ) 
      elementSize = DEFAULT_ELEMENT_SIZE;

  if( queue->running )
    FreeAsyncQueue(queue);

  // 一次性分配队列结构数组内存
  queue->index = (queueData_t*)malloc(queueNum * sizeof(queueData_t));
  if (!queue->index) {
      SafePrintf("Failed to allocate queue memory\n");
      return false;
  }

  // 一次性分配数据缓冲区内存（所有元素的数据连续存储）
  queue->dataBuffer = (uint8_t*)malloc(queueNum * elementSize);
  if (!queue->dataBuffer) {
      SafePrintf("Failed to allocate data buffer memory\n");
      free(queue->index);
      queue->index = NULL;
      return false;
  }

  // 初始化队列元素指针
  for (uint16_t i = 0; i < queueNum; i++) {
    queue->index[i].data = queue->dataBuffer + (i * elementSize);
    queue->index[i].len = 0;
  }

  // 初始化队列属性
  queue->capacity = queueNum;
  queue->elementSize = elementSize;
  queue->front = queue->rear = 0;
  
  // 创建同步对象
#ifdef _WIN32
  queue->hMutex = CreateMutex(NULL, false, NULL);
  queue->hDataEvent = CreateEvent(NULL, true, false, NULL);
  queue->hSpaceEvent = CreateEvent(NULL, true, true, NULL);
#else
  queue->hMutex = &queue->hMutexV;
  queue->hDataEvent = &queue->hDataEventV;
  queue->hSpaceEvent = &queue->hSpaceEventV;

  pthread_mutex_init(queue->hMutex, NULL);
  pthread_cond_init(queue->hDataEvent, NULL);
  pthread_cond_init(queue->hSpaceEvent, NULL);
#endif
  
  queue->outDataCallBack = CallBack;
  queue->name = queueName? queueName:"unknown";
  
  // 创建队列线程
  queue->hThread = threadCreate(NULL, AsyncQueueThreadProc, queue);
  if (!queue->hThread) {
      SafePrintf("Failed to Create Async Queue Thread\n");
      FreeAsyncQueue(queue);
      return false;
  }

  return true;
}

// 异步队列线程主函数
static threadRet WINAPI AsyncQueueThreadProc(void *lpParam)
{
  AsyncQueue_t *queue = (AsyncQueue_t*)lpParam;
  // SafePrintf("Queue initialized: queueNum=%d, elementSize=%d, totalMemory=%I64d KB\n",
  //      queueNum, elementSize, (queueNum * sizeof(queueData_t)) + totalDataSize);
  SafePrintf("Async Queue Thread %s! Num %d, Element size: %d, Name: %s\n", 
    lpParam == NULL? "Fail":"Started",
    lpParam == NULL? 0:queue->capacity, 
    lpParam == NULL? 0:queue->elementSize,
    lpParam == NULL? "unknown":queue->name);
  
  if( lpParam == NULL)
    return (threadRet)-1;
  
  queue->running = true;
  
  while (queue->running) {
#ifdef _WIN32
    // Windows 版本保持不变
    DWORD waitResult = WaitForSingleObject(queue->hDataEvent, INFINITE);
    
    if (!queue->running) 
      break;
    
    if (waitResult != WAIT_OBJECT_0) 
      continue;
#else
    // Linux 版本修复：在循环内加锁，正确处理条件变量
    pthread_mutex_lock((pthread_mutex_t*)queue->hMutex);
    
    // 等待数据可用或退出信号
    while (queue->running && queue->front == queue->rear) {
      pthread_cond_wait((pthread_cond_t*)queue->hDataEvent, (pthread_mutex_t*)queue->hMutex);
    }
    
    // 检查是否要退出
    if (!queue->running) {
      pthread_mutex_unlock((pthread_mutex_t*)queue->hMutex);
      break;
    }
#endif

    // 处理所有可用数据
    while (queue->running) {
#ifdef _WIN32
      WaitForSingleObject(queue->hMutex, INFINITE);
#endif  // Linux版本这里已经持有锁，不需要重复加锁
      
      // 检查队列是否为空
      if (queue->front == queue->rear) {
#ifdef _WIN32
          ResetEvent(queue->hDataEvent);
          ReleaseMutex(queue->hMutex);
#else
          // Linux版本：解锁并跳出内层循环，回到外层等待
          pthread_mutex_unlock((pthread_mutex_t*)queue->hMutex);
#endif
          break;
      }
      
      // 取出队列头的数据
      queueData_t indexData = queue->index[queue->front];
      queue->front = (queue->front + 1) % queue->capacity;
      
      // 如果有空间可用，设置空间事件
      if ((queue->rear + 1) % queue->capacity != queue->front) {
#ifdef _WIN32
          SetEvent(queue->hSpaceEvent);
#else
          pthread_cond_signal((pthread_cond_t*)queue->hSpaceEvent);
#endif
      }

#ifdef _WIN32
      ReleaseMutex(queue->hMutex);
#else
      // Linux版本：先解锁再调用回调，避免回调函数阻塞队列
      pthread_mutex_unlock((pthread_mutex_t*)queue->hMutex);
#endif
      
      // 调用用户提供的回调（在锁外执行）
      queue->outDataCallBack(indexData.data, indexData.len);
      
#ifndef _WIN32
      // Linux版本：重新获取锁以检查队列状态
      pthread_mutex_lock((pthread_mutex_t*)queue->hMutex);
#endif
    }
  }
  
  SafePrintf("Async Queue Thread Exiting, Name:%s\n", queue->name);
  return (threadRet)0;
}

// 释放队列资源
void FreeAsyncQueue(AsyncQueue_t *queue) 
{
    if (!queue) return;
    
    // 设置停止标志
    queue->running = false; 

#ifdef _WIN32
    // 唤醒线程以便退出
    SetEvent(queue->hDataEvent);
    
    // 等待线程退出
    if (queue->hThread) {
        WaitForSingleObject(queue->hThread, 1000);
        CloseHandle(queue->hThread);
        queue->hThread = NULL;
    }
    
    // 关闭同步对象
    if (queue->hMutex) {
        CloseHandle(queue->hMutex);
        queue->hMutex = NULL;
    }
    if (queue->hDataEvent) {
        CloseHandle(queue->hDataEvent);
        queue->hDataEvent = NULL;
    }
    if (queue->hSpaceEvent) {
        CloseHandle(queue->hSpaceEvent);
        queue->hSpaceEvent = NULL;
    }
#else
    // Linux 版本清理
    if (queue->hThread) { // 唤醒等待的线程
        pthread_mutex_lock((pthread_mutex_t*)queue->hMutex);
        pthread_cond_signal((pthread_cond_t*)queue->hDataEvent);
        pthread_mutex_unlock((pthread_mutex_t*)queue->hMutex);
        
        WaitForSingleObject_Wrapper(queue->hThread, 1000);
        CloseHandle(queue->hThread);
        queue->hThread = (thread_t)0;
    }
    
    // 销毁同步对象
    if (queue->hMutex) {
        pthread_mutex_destroy((pthread_mutex_t*)queue->hMutex);
        queue->hMutex = NULL;
    }
    if (queue->hDataEvent) {
        pthread_cond_destroy((pthread_cond_t*)queue->hDataEvent);
        queue->hDataEvent = NULL;
    }
    if (queue->hSpaceEvent) {
        pthread_cond_destroy((pthread_cond_t*)queue->hSpaceEvent);
        queue->hSpaceEvent = NULL;
    }
#endif
    
    // 释放内存
    if (queue->dataBuffer) {
        free(queue->dataBuffer);
        queue->dataBuffer = NULL;
    }
    
    if (queue->index) {
        free(queue->index);
        queue->index = NULL;
    }
    
    queue->capacity = 0;
    queue->elementSize = 0;
    SafePrintf("Async queue freed, name:%s\n", queue->name);
}

// 添加数据到发送队列
bool AddDataToAsyncQueue(AsyncQueue_t *queue, const uint8_t *data, uint32_t len) 
{
    if( queue->running == false )
      return false;
 
    // 检查数据大小
    if (len > queue->elementSize) {
        SafePrintf("Data too large (%d > %d), discarding\n", len, queue->elementSize);
        return false;
    }
    
#ifdef _WIN32
    // Windows 版本使用事件等待
    DWORD waitResult = WaitForSingleObject(queue->hSpaceEvent, 100);
    if (waitResult != WAIT_OBJECT_0) {
        SafePrintf("Async queue full, discarding data\n");
        return false;
    }
    
    WaitForSingleObject(queue->hMutex, INFINITE);
    int nextRear = (queue->rear + 1) % queue->capacity;
#else
// Linux 版本使用条件变量
    pthread_mutex_lock((pthread_mutex_t*)queue->hMutex);
    
    // 等待队列空间可用
    int nextRear = (queue->rear + 1) % queue->capacity;
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1; // 1秒超时
    
    while (nextRear == queue->front && queue->running) {
        int result = pthread_cond_timedwait((pthread_cond_t*)queue->hSpaceEvent, 
                                           (pthread_mutex_t*)queue->hMutex, &ts);
        if (result == ETIMEDOUT) {
            pthread_mutex_unlock((pthread_mutex_t*)queue->hMutex);
            SafePrintf("Async queue full, discarding data\n");
            return false;
        }
        nextRear = (queue->rear + 1) % queue->capacity;
    }
    
    if (!queue->running) {
        pthread_mutex_unlock((pthread_mutex_t*)queue->hMutex);
        return false;
    }
#endif
    
    // 检查队列是否已满
    if (nextRear == queue->front) {
#ifdef _WIN32
        ReleaseMutex(queue->hMutex);
#else
        pthread_mutex_unlock((pthread_mutex_t*)queue->hMutex);
#endif
        SafePrintf("Queue full after space event, discarding data\n");
        return false;
    }
    
    // 添加数据到队列
    queue->index[queue->rear].len = len;
    memcpy(queue->index[queue->rear].data, data, len);
    queue->rear = nextRear;
    
    // 设置数据可用事件
#ifdef _WIN32
    SetEvent(queue->hDataEvent);
    
    // 如果队列满，重置空间事件
    if ((queue->rear + 1) % queue->capacity == queue->front)
        ResetEvent(queue->hSpaceEvent);

    ReleaseMutex(queue->hMutex);
#else
    pthread_cond_signal((pthread_cond_t*)queue->hDataEvent);
    
    // 如果队列满，不发送空间信号
    if ((queue->rear + 1) % queue->capacity != queue->front)
      pthread_cond_signal((pthread_cond_t*)queue->hSpaceEvent);
    
    pthread_mutex_unlock((pthread_mutex_t*)queue->hMutex);
#endif
    
    return true;
}

// 获取队列当前元素数量
int getAsyncQueueCurrentSize(AsyncQueue_t *queue) 
{
  if (!queue || !queue->running || !queue->hMutex) 
      return -1;

#ifdef _WIN32
  WaitForSingleObject(queue->hMutex, INFINITE);
#else
  pthread_mutex_lock((pthread_mutex_t*)queue->hMutex);
#endif
  
  int currentSize;
  if (queue->rear >= queue->front) 
      currentSize = queue->rear - queue->front;
  else 
      currentSize = queue->capacity - queue->front + queue->rear;
  
#ifdef _WIN32
  ReleaseMutex(queue->hMutex);
#else
  pthread_mutex_unlock((pthread_mutex_t*)queue->hMutex);
#endif
  
  return currentSize;
}

// 获取队列剩余可用数量
int getAsyncQueueRemainingSpace(AsyncQueue_t *queue) 
{
  int currentSize = getAsyncQueueCurrentSize(queue); 
  return currentSize < 0? -1:queue->capacity - currentSize - 1;
}






typedef void(*funcHandleCallBack_t)(void *);
typedef struct { 
  funcHandleCallBack_t  func;
  void                  *arg;
} funcHandle_t;

static AsyncQueue_t AsyncFuncHandleQueue;
static void AsyncFuncHandleCallBack(uint8_t *data, uint32_t len)
{
  (void)len;
  funcHandle_t *funcHandle = (funcHandle_t *)data;
  if( funcHandle->func )
    funcHandle->func( funcHandle->arg );
}

/*=============================================================================
 功   能：初始化启动队列线程，专门处理异步函数 
 参   数：无
 返   回：成功返回真，失败返回假
 描   述：无
=============================================================================*/
bool startAsyncFuncHandle(bool start)
{
  if( start )
    return startAsyncQueue(&AsyncFuncHandleQueue, AsyncFuncHandleCallBack, 
                  100, sizeof(funcHandle_t) + 10, "Async Function Handle");
  else
    FreeAsyncQueue(&AsyncFuncHandleQueue);
  return true;
}

/*=============================================================================
 功   能：添加处理异步函数 
 参   数：CallBack 回调函数
          arg      附带参数
 返   回：成功返回真，失败返回假
 描   述：最好能够快进快出的不要阻塞太久的函数，因为大家都是在一个线程里
=============================================================================*/
bool addAsyncFuncHandle(funcHandleCallBack_t CallBack, void *arg)
{
  funcHandle_t AsyncFunc;
  AsyncFunc.func = CallBack;
  AsyncFunc.arg = arg;
  return AddDataToAsyncQueue(&AsyncFuncHandleQueue, (uint8_t*)&AsyncFunc, sizeof AsyncFunc);
}