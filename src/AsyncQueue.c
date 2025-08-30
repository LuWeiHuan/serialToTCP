/******************************************************************************
  * @file    文件 AsyncQueue.c 
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
#include "AsyncQueue.h"
#include "logPrint.h"
#include "main.h"

#include <winsock2.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 本地函数声明    ========================================*/
/*================== 外部函数和变量声明    ==================================*/

// 初始化队列和启动线程
BOOL InitAsyncSendThread(AsyncSendQueue_t *queue, LPTHREAD_START_ROUTINE lpStartAddress, int queueSize) {

    if( queue == NULL )
      return FALSE;

    if (queueSize <= 10 || queueSize >= MAX_QUEUE_SIZE) {
        SafePrintf("Invalid queue size: %d\n", queueSize);
        return FALSE;
    }

    if( queue->running )
      FreeAsyncSendQueue(queue);

    // 分配队列内存
    queue->queue = (queueData_t*)malloc(queueSize * sizeof(queueData_t));
    if (!queue->queue) {
        SafePrintf("Failed to allocate queue memory\n");
        return FALSE;
    }

    // 初始化队列属性
    queue->capacity = queueSize;
    queue->front = 0;
    queue->rear = 0;
    queue->running = TRUE;

    // 创建同步对象
    queue->hMutex = CreateMutex(NULL, FALSE, NULL);
    queue->hDataEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // 初始无数据
    queue->hSpaceEvent = CreateEvent(NULL, TRUE, TRUE, NULL); // 初始有空间
    
    // 创建发送线程
    queue->hThread = CreateThread(NULL, 0, lpStartAddress, queue, 0, NULL);
    if (!queue->hThread) {
        SafePrintf("Failed to create async send thread\n");
        FreeAsyncSendQueue(queue);
        return FALSE;
    }
    
    SafePrintf("Async send thread started with queue size: %d\n", queueSize);
 
    return TRUE;
}

// 释放队列资源
void FreeAsyncSendQueue(AsyncSendQueue_t *queue) {
    // 设置停止标志
    queue->running = FALSE; 

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
    
    // 释放队列内存
    if (queue->queue) {
        free(queue->queue);
        queue->queue = NULL;
    }
    
    queue->capacity = 0;
    SafePrintf("Async send queue freed\n");
}

// 添加数据到发送队列
BOOL AddDataToAsyncQueue(AsyncSendQueue_t *queue, const char *data, uint32_t size) {

    if( queue->running == FALSE )
      return FALSE;
 
    // 检查数据大小
    if (size > RECV_BUFFER_SIZE) {
        SafePrintf("Data too large (%u > %d), discarding\n", size, RECV_BUFFER_SIZE);
        return FALSE;
    }
    
    // 等待队列空间可用
    DWORD waitResult = WaitForSingleObject(queue->hSpaceEvent, 100); // 100ms超时
    if (waitResult != WAIT_OBJECT_0) {
        SafePrintf("Async queue full, discarding data\n");
        return FALSE;
    }
    
    // 获取队列互斥锁
    WaitForSingleObject(queue->hMutex, INFINITE);
    
    // 检查队列是否已满
    int nextRear = (queue->rear + 1) % queue->capacity;
    if (nextRear == queue->front) {
        ReleaseMutex(queue->hMutex);
        SafePrintf("Queue full after space event, discarding data\n");
        return FALSE;
    }
    
    // 添加数据到队列
    queue->queue[queue->rear].size = size;
    memcpy(queue->queue[queue->rear].buff, data, size);
    queue->rear = nextRear;
    
    // 设置数据可用事件
    SetEvent(queue->hDataEvent);
    
    // 如果队列满，重置空间事件
    if ((queue->rear + 1) % queue->capacity == queue->front)
        ResetEvent(queue->hSpaceEvent);

    ReleaseMutex(queue->hMutex);
    return TRUE;
}
