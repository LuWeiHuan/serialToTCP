
#ifndef __QUEUE_H_
#define __QUEUE_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>

#include <ws2tcpip.h>
#include <windows.h>

#include "main.h"

/*================== 宏定义声明			=========================================*/
#define MIN_QUEUE_SIZE  10    // 最少队列长度
#define MAX_QUEUE_SIZE  512   // 最大队列长度

/*================== 数据类型声明		=========================================*/
// struct enum union

typedef struct {
    uint32_t size;     
    char buff[RECV_BUFFER_SIZE];     // 数据内容
} queueData_t;

typedef struct {
    queueData_t *queue;       // 队列数组
    int capacity;             // 队列容量
    int front;                // 队列头指针
    int rear;                 // 队列尾指针
    HANDLE hMutex;            // 队列互斥锁
    HANDLE hDataEvent;        // 数据可用事件
    HANDLE hSpaceEvent;       // 空间可用事件
    HANDLE hThread;           // 发送线程句柄
    volatile BOOL running;    // 线程运行标志
    void(*callBack)(queueData_t *); // 如果有数据就待用该回调
}AsyncSendQueue_t;

/*================== 外部变量声明		=========================================*/
//extern

/*================== 外部函数声明		=========================================*/

BOOL startAsyncDataHandleThread(AsyncSendQueue_t *queue, void(*callBack)(queueData_t *), int queueSize);
BOOL AddDataToAsyncQueue(AsyncSendQueue_t *queue, const char *data, uint32_t size);
void FreeAsyncSendQueue(AsyncSendQueue_t *queue);

int GetAsyncQueueRemainingSpace(AsyncSendQueue_t *queue);
int GetAsyncQueueCurrentSize(AsyncSendQueue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /*__QUEUE_H_*/







