/******************************************************************************
  * @file    文件 COMAutoReOpen.h 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 串口自动重连功能（简化版）
  ******************************************************************************
  * @attention 注意
  *
  *******************************************************************************/

#ifndef __COM_AUTO_REOPEN_H_
#define __COM_AUTO_REOPEN_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 外部函数声明 ============================================*/

/**
 * @brief 加载自动打开串口功能
 */
void loadAutoReOpenCOMConfig(void);

/**
 * @brief 自动打开串口功能是否启动
 */
void COM_AutoReOpen(bool);

/**
 * @brief 串口设备插拔时的回调
 */
void COM_AutoReOpen_OnDeviceChange(void);

/**
 * @brief 定时1秒调用
 */
void Time1SecProcessPendingOpen(void);

/**
 * @brief 保存当前打开的串口信息
 * @param portName 串口名称，传入空代表暂停自动打开
 * @param baudRate 波特率
 * @param dataBits 数据位
 * @param stopBits 停止位
 * @param parity 校验位
 */
void COM_AutoReOpen_SaveCurrentPort(const char* portName, 
      uint32_t baudRate, uint8_t dataBits, uint8_t stopBits, uint8_t parity); 
#ifdef __cplusplus
}
#endif

#endif /* __COM_AUTO_REOPEN_H_ */