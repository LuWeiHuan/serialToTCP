#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_

#include <stdint.h>
#include <stdbool.h>

// 参数包装结构
typedef struct {
    void* arg;  // 真正的业务参数
    void* threadTask; // 任务指针
} ThreadPoolArgWrapper; 

// 回调函数类型
typedef void (*ThreadTaskCallback_t)(void* arg);

// 线程池任务结构
typedef struct ThreadTask {
  ThreadTaskCallback_t task_cb;     // 任务回调函数
  ThreadPoolArgWrapper argWrapper;  // 回调函数封装参数
  uint32_t delay;                   // 延迟执行时间(ms)
  uint32_t repeat;                  // 重复间隔(0=只执行一次, >0=重复间隔ms)
  uint32_t executed_count;          // 已执行次数
  uint64_t next_execute_time_us;    // 下次执行时间戳(us)
  struct ThreadTask* next;          // 链表下一个节点
} ThreadTask;

// 线程池句柄
typedef struct ThreadPool ThreadPool;

// 全局线程池
extern ThreadPool * gThreadPool;


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 全局线程池初始化
 */
bool globalThreadPoolInit(uint16_t num);

/**
 * @brief 创建线程池
 * @param config: 线程池配置(NULL则使用默认配置)
 * @return 线程池句柄,失败返回NULL
 */
ThreadPool* threadPoolCreate(uint16_t num);

/**
 * @brief 销毁线程池
 * @param pool: 线程池句柄
 */
void threadPoolDestroy(ThreadPool* pool);

/**
 * @brief 初始化定时任务
 * @param task: 任务句柄
 * @param task_cb: 任务回调函数
 * @param arg: 回调函数参数
 * @param delay_ms: 延迟执行时间(ms)
 * @param repeat: 重复间隔(0=只执行一次, >0=重复间隔ms)
 */
void threadTaskInit(ThreadTask* task, ThreadTaskCallback_t task_cb,
                      void* arg, uint32_t delay_ms, uint32_t repeat);

/**
 * @brief 启动定时任务(添加到线程池)
 * @param pool: 线程池句柄
 * @param task: 任务句柄
 * @return 0:成功, -1:失败
 */
bool threadTtaskStart(ThreadPool* pool, ThreadTask* task);

/**
 * @brief 停止定时任务(从线程池移除)
 * @param pool: 线程池句柄
 * @param task: 任务句柄
 */
void threadTtaskStop(ThreadPool* pool, ThreadTask* task);

/**
 * @brief 重新初始化并启动定时任务
 */
void threadTaskReinit(ThreadPool* pool, ThreadTask* task, 
                        ThreadTaskCallback_t task_cb, void* arg,
                        uint32_t delay_ms, uint32_t repeat);

/**
 * @brief 获取当前活动线程数
 */
uint32_t threadPoolGetActiveCount(ThreadPool* pool);

/**
 * @brief 获取待处理任务数
 */
uint32_t threadPoolGetPendingCount(ThreadPool* pool);

/**
 * @brief 线程池精度测试
 */
void threadPoolAccuracyTest(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* _THREAD_POOL_H_ */
