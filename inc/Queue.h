#ifndef __QUEUE_H_
#define __QUEUE_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>

#include <windef.h>

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

typedef void(*outDataCallBack_t)(char *data, uint32_t len);

typedef struct {
  volatile bool running;    // 线程运行标志
  uint16_t capacity;        // 队列容量
  uint32_t elementSize;     // 每个元素的最大大小
  int front;                // 队列头指针
  int rear;                 // 队列尾指针
  HANDLE hMutex;            // 队列互斥锁
  HANDLE hDataEvent;        // 数据可用事件
  HANDLE hSpaceEvent;       // 空间可用事件
  HANDLE hThread;           // 发送线程句柄
  const char *name;
  char *dataBuffer;         // 数据缓冲区（一次性分配）
  queueData_t *index;       // 队列数组 
  outDataCallBack_t   outDataCallBack; // 必须有调用回调
}AsyncQueue_t;

/*================== 外部变量声明		=========================================*/
//extern

/*================== 外部函数声明		=========================================*/

bool startAsyncQueue(AsyncQueue_t *queue, outDataCallBack_t CallBack, 
      uint16_t queueSize, uint32_t elementSize, const char *queueName);
bool AddDataToAsyncQueue(AsyncQueue_t *queue, const char *data, uint32_t len);
void FreeAsyncQueue(AsyncQueue_t *queue);

int GetAsyncQueueRemainingSpace(AsyncQueue_t *queue);
int GetAsyncQueueCurrentSize(AsyncQueue_t *queue);

bool startAsyncFuncHandle(bool start);
bool addAsyncFuncHandle(void(*CallBack)(void*) , void *arg);

#ifdef __cplusplus
}
#endif

#endif /*__QUEUE_H_*/