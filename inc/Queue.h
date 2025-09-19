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

/*================== 宏定义声明			=========================================*/
#define MIN_QUEUE_SIZE          10    // 最少队列长度
#define MAX_QUEUE_SIZE          512   // 最大队列长度
#define DEFAULT_ELEMENT_SIZE    1024  // 默认元素大小

/*================== 数据类型声明		=========================================*/
// struct enum union

typedef struct {
  uint32_t len;            // 数据实际大小
  char *data;              // 数据内容指针
} queueData_t;

typedef struct {
  volatile BOOL running;    // 线程运行标志
  uint16_t capacity;        // 队列容量
  uint32_t elementSize;     // 每个元素的最大大小
  int front;                // 队列头指针
  int rear;                 // 队列尾指针
  HANDLE hMutex;            // 队列互斥锁
  HANDLE hDataEvent;        // 数据可用事件
  HANDLE hSpaceEvent;       // 空间可用事件
  HANDLE hThread;           // 发送线程句柄

  char *dataBuffer;         // 数据缓冲区（一次性分配）
  queueData_t *index;       // 队列数组 
  void(*outDataCallBack)(queueData_t *); // 必须有调用回调
}AsyncSendQueue_t;

/*================== 外部变量声明		=========================================*/
//extern

/*================== 外部函数声明		=========================================*/

BOOL startAsyncDataHandleThread(AsyncSendQueue_t *queue, 
      void(*outDataCallBack)(queueData_t *), uint16_t queueSize, uint32_t elementSize);
BOOL AddDataToAsyncQueue(AsyncSendQueue_t *queue, const char *data, uint32_t len);
void FreeAsyncSendQueue(AsyncSendQueue_t *queue);

int GetAsyncQueueRemainingSpace(AsyncSendQueue_t *queue);
int GetAsyncQueueCurrentSize(AsyncSendQueue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /*__QUEUE_H_*/