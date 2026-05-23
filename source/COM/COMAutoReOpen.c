/******************************************************************************
  * @file    文件 COMAutoReOpen.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 串口自动重连功能实现（简化版）
  ******************************************************************************
  * @attention 注意
  *
  *******************************************************************************/

/*================== 头文件包含     =========================================*/
#include "COMAutoReOpen.h"
#include "COM.h"
#include "COMinfo.h"
#include "commonUtils.h"
#include "log.h"
#include "configSave.h"
#include "platform.h"

#include "configSave.h"
#include "minIni.h"
#include "clients.h"

#include <stdio.h>
#include <string.h>

/*================== 本地宏定义     =========================================*/
#define DEVICE_CHANGE_DELAY_MS   2000      // 设备变化后延迟打开时间

/*================== 本地数据类型     =========================================*/
typedef struct {                  // 上次成功打开的串口信息
    char lastOpenedPort[32];      // 串口名称
    uint32_t lastBaudRate;        // 波特率
    uint8_t lastDataBits;         // 数据位
    uint8_t lastStopBits;         // 停止位
    uint8_t lastParity;           // 校验位

    bool    ReOpen;               // 是否启用自动打开串口功能
    uint8_t changeTime;           // 打开倒计时
    uint8_t delaySave;            // 延迟保存
} AutoReOpenManager_t;

/*================== 本地变量声明    ========================================*/
static AutoReOpenManager_t autoReOpenMgr = {
    .lastOpenedPort = "NULL",
    .lastBaudRate = 921600,
    .lastDataBits = 8,
    .lastStopBits = 1,
    .lastParity = 0, 
    .changeTime = 0,
    .delaySave = 0,
    .ReOpen = false,
};

static mutex_type csAutoReOpenMgr; 

/*================== 本地函数声明    ========================================*/
static void SaveConfigToFile(void);

/*================== 模块初始化/反初始化 ====================================*/

void COM_AutoReOpen(bool start)
{ 
  autoReOpenMgr.ReOpen = start;
  autoReOpenMgr.delaySave = 5; // 5秒后保存自动打开串口设置

  static bool init = false;
  if (autoReOpenMgr.ReOpen){
    if( init == false)
      InitializeCriticalSection_Wrapper(&csAutoReOpenMgr);
    init = true;
  } 
  else{
    if( init == true)
      DeleteCriticalSection_Wrapper(&csAutoReOpenMgr);
    init = false;
  }

}

void COM_AutoReOpen_OnDeviceChange(void)
{ 
    autoReOpenMgr.changeTime = 2; // 串口插拔变化的话就缩短到2秒打开一次
}

/**
 * @brief 尝试打开上次的串口 处理等待打开的逻辑（需要每隔1秒调用一次）
 */
void Time1SecProcessPendingOpen(void *arg)
{ 
  (void)arg;
  if( autoReOpenMgr.delaySave && --autoReOpenMgr.delaySave <= 0)
      SaveConfigToFile();
  
  if( autoReOpenMgr.ReOpen == false)
    return;
  
  if( strcmp(autoReOpenMgr.lastOpenedPort, "NULL") == 0 || getComIsOpen() )
    return;

  bool retExists = COM_PortExists(autoReOpenMgr.lastOpenedPort);
  if( retExists == false )
      return ;
  
  #if 0 // 每隔10秒打开一次串口
  if( getComIsOpen() == false && autoReOpenMgr.changeTime == 0 )
    autoReOpenMgr.changeTime = 10;
  #endif

  if ( --autoReOpenMgr.changeTime )
      return;
  
  EnterCriticalSection_Wrapper(&csAutoReOpenMgr); 
  char portName[32];  // 获取串口参数 
  strcpy(portName, autoReOpenMgr.lastOpenedPort);
  uint32_t baudRate = autoReOpenMgr.lastBaudRate;
  uint8_t dataBits = autoReOpenMgr.lastDataBits;
  uint8_t stopBits = autoReOpenMgr.lastStopBits;
  uint8_t parity = autoReOpenMgr.lastParity; 
  LeaveCriticalSection_Wrapper(&csAutoReOpenMgr);

  // 尝试打开串口
  int8_t result = OpenComPort(portName, baudRate, dataBits, stopBits, parity); 
  static uint32_t openCount = 0;

  static char reOpenInfo[200];
  memset(reOpenInfo, 0, sizeof reOpenInfo);
  snprintf(reOpenInfo, sizeof reOpenInfo, 
      "Attempting To Reopen %s Port (baud:%d) %s(code:%d) Open Num:%-10d", 
      portName, baudRate, result == 0? "Success":"Fail", result, ++openCount);
  printfSend(NULL, "%s\n", reOpenInfo);
  SafePrintf("%s\r", reOpenInfo);
}

/*================== 串口信息保存函数 =======================================*/

void COM_AutoReOpen_SaveCurrentPort(const char* portName, 
      uint32_t baudRate, uint8_t dataBits, uint8_t stopBits, uint8_t parity)
{ 
    if( autoReOpenMgr.ReOpen == false )
        return;
    
    if (portName == NULL || strlen(portName) == 0){
      strcpy(autoReOpenMgr.lastOpenedPort, "NULL");
      return;
    }
    
    EnterCriticalSection_Wrapper(&csAutoReOpenMgr);
    
    // 保存到最近打开的串口
    strncpy(autoReOpenMgr.lastOpenedPort, portName, 
            sizeof(autoReOpenMgr.lastOpenedPort) - 1);
    
    autoReOpenMgr.lastBaudRate = baudRate;
    autoReOpenMgr.lastDataBits = dataBits;
    autoReOpenMgr.lastStopBits = stopBits;
    autoReOpenMgr.lastParity = parity;

    autoReOpenMgr.delaySave = 5;
    LeaveCriticalSection_Wrapper(&csAutoReOpenMgr); 
}
 
 


 

/**
 * @brief 加载自动打开串口功能
 */
void loadAutoReOpenCOMConfig(void)
{ 
  ini_gets("AutoReOpenCOM", "autoReOpenMgr-lastOpenedPort", autoReOpenMgr.lastOpenedPort, 
      autoReOpenMgr.lastOpenedPort, sizeof autoReOpenMgr.lastOpenedPort, getConfigFilePath()); 
  autoReOpenMgr.lastBaudRate = ini_getl("AutoReOpenCOM", "autoReOpenMgr-lastBaudRate", 
      autoReOpenMgr.lastBaudRate, getConfigFilePath());
  autoReOpenMgr.lastDataBits = ini_getl("AutoReOpenCOM", "autoReOpenMgr-lastDataBits", 
      autoReOpenMgr.lastDataBits, getConfigFilePath());
  autoReOpenMgr.lastStopBits = ini_getl("AutoReOpenCOM", "autoReOpenMgr-lastStopBits", 
      autoReOpenMgr.lastStopBits, getConfigFilePath());
  autoReOpenMgr.lastParity = ini_getl("AutoReOpenCOM", "autoReOpenMgr-lastParity", 
      autoReOpenMgr.lastParity, getConfigFilePath());
  
  autoReOpenMgr.ReOpen = ini_getl("AutoReOpenCOM", "autoReOpenMgr-ReOpen", 
    autoReOpenMgr.ReOpen, getConfigFilePath());
  COM_AutoReOpen(autoReOpenMgr.ReOpen);
  if( autoReOpenMgr.ReOpen )
    autoReOpenMgr.changeTime = 3;
}

/**
 * @brief 保存 自动打开串口功能 设置到配置文件
 */
static void SaveConfigToFile(void)
{
    char buffer[32];
      
    // 保存串口配置
    ini_puts("AutoReOpenCOM", "autoReOpenMgr-lastOpenedPort", 
             autoReOpenMgr.lastOpenedPort, getConfigFilePath());
    
    snprintf(buffer, sizeof(buffer), "%u", autoReOpenMgr.lastBaudRate);
    ini_puts("AutoReOpenCOM", "autoReOpenMgr-lastBaudRate", buffer, getConfigFilePath());
    
    snprintf(buffer, sizeof(buffer), "%u", autoReOpenMgr.lastDataBits);
    ini_puts("AutoReOpenCOM", "autoReOpenMgr-lastDataBits", buffer, getConfigFilePath());
    
    snprintf(buffer, sizeof(buffer), "%u", autoReOpenMgr.lastStopBits);
    ini_puts("AutoReOpenCOM", "autoReOpenMgr-lastStopBits", buffer, getConfigFilePath());
    
    snprintf(buffer, sizeof(buffer), "%u", autoReOpenMgr.lastParity);
    ini_puts("AutoReOpenCOM", "autoReOpenMgr-lastParity", buffer, getConfigFilePath());
    
    // 保存自动重连开关状态
    snprintf(buffer, sizeof(buffer), "%u", autoReOpenMgr.ReOpen ? 1 : 0);
    ini_puts("AutoReOpenCOM", "autoReOpenMgr-ReOpen", buffer, getConfigFilePath()); 
}