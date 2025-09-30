
#ifndef __EXCEPTION_H_
#define __EXCEPTION_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdint.h>
#include <stdbool.h>

/*================== 宏定义声明			=========================================*/
// 定义控制宏



/*================== 数据类型声明		=========================================*/
//struct enum union

/*================== 外部变量声明		=========================================*/
//extern

/*================== 外部函数声明		=========================================*/

/**
 * @brief 初始化进程异常监控
 */
void InitializeProcessExceptionMonitor(void);

/**
 * @brief 清理进程异常监控
 */
void CleanupProcessExceptionMonitor(void);

/**
 * @brief 设置异常日志路径
 * @param logPath 日志文件路径
 */
void SetExceptionLogPath(const char* logPath);

/**
 * @brief 获取最后一次异常信息
 * @return 异常信息字符串
 */
const char* GetLastExceptionInfo(void);
#ifdef __cplusplus
}
#endif

#endif /*__EXCEPTION_H_*/
