
#ifndef __EXCEPTION_H_
#define __EXCEPTION_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
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
void ProcessExceptionMonitorInit(void);

/**
 * @brief 清理进程异常监控
 */
void CleanupProcessExceptionMonitor(void);
#ifdef __cplusplus
}
#endif

#endif /*__EXCEPTION_H_*/
