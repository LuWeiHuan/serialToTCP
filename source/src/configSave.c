/******************************************************************************
  * @file    文件 configSave.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 流量统计
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "configSave.h"
#include "minIni.h"
#include "platform.h"
#include "main.h"

#include <stdio.h>
#include <string.h>
#ifdef __linux
#include <unistd.h>
#endif

/*================== 宏定义    ========================================*/
static char configFile[100] = "configInfo.ini";
#define DEFAULT_HOST   "DefaultHost"
saveInfo_t saveInfo = {
  .COMsendPoll = false,
  .COMrecvPoll = false,
  .COMalignedRecv4K = RECV_4K_MAX,
  .hostName = DEFAULT_HOST,
  .serverPrintData = 0,
};

void setConfigFilePath(const char *path)
{
  if( strlen(path) > sizeof configFile - 1 )
    return;
  strcpy(configFile, path);
}

void loadConfig(void)
{
  saveInfo.COMsendPoll = ini_getl("Settings", "COMsendPoll", false, configFile);
  saveInfo.COMrecvPoll = ini_getl("Settings", "COMrecvPoll", false, configFile);
  saveInfo.COMalignedRecv4K = ini_getl("Settings", "COMalignedRecv4K", RECV_4K_MAX, configFile);
  if( saveInfo.COMalignedRecv4K > RECV_4K_MAX )
    saveInfo.COMalignedRecv4K = RECV_4K_MAX;
  saveInfo.serverPrintData = ini_getl("Settings", "serverPrintData", 0, configFile);
  ini_gets("Settings", "hostName", DEFAULT_HOST, saveInfo.hostName, sizeof saveInfo.hostName, configFile);
}

void saveConfig(void)
{
  static volatile bool runOnce = false;
  
  // 简单忙等待
  for( uint8_t i=0; runOnce && i<10; i++ ) { // 短暂等待后重试
    if( i>10 )
      return; // 等待时间过长，放弃本次保存
    Sleep(1); // 1ms
  }
  runOnce = true;

  char buffer[20];

  ini_puts("Settings", "COMsendPoll", saveInfo.COMsendPoll? "1" : "0", configFile);
  ini_puts("Settings", "COMrecvPoll", saveInfo.COMrecvPoll? "1" : "0", configFile);

  snprintf(buffer, sizeof buffer, "%u", saveInfo.COMalignedRecv4K);
  ini_puts("Settings", "COMalignedRecv4K", buffer, configFile);
  snprintf(buffer, sizeof buffer, "%u", saveInfo.serverPrintData);
  ini_puts("Settings", "serverPrintData", buffer, configFile);
  
  if( strncmp(saveInfo.hostName, DEFAULT_HOST, strlen(DEFAULT_HOST)) != 0 ) 
    ini_puts("Settings", "hostName", saveInfo.hostName, configFile);
  
  runOnce = false;
}


void saveConfigTest(bool state)
{
    if (state) {
        printf("配置保存功能已启用。\n");
    } else {
        printf("配置保存功能已禁用。\n");
    }

    char buffer[100];

    // 写入配置
    ini_puts("Database", "host", "localhost", configFile);
    ini_puts("Database", "port", "3306", configFile);
    ini_puts("Settings", "timeout", "30", configFile);
    
    // 读取字符串配置
    ini_gets("Database", "host", "127.0.0.1", buffer, sizeof buffer, configFile);
    printf("Host: %s\n", buffer);
    
    // 读取数字配置
    long port = ini_getl("Database", "port", 3306, configFile);
    printf("Port: %ld\n", port);
    
    // 读取不存在的配置（使用默认值）
    ini_gets("Database", "username", "admin", buffer, sizeof buffer, configFile);
    printf("Username: %s\n", buffer);
}