/******************************************************************************
  * @file    文件 configSave.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 配置保存和运行状态的信息
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

/*================== 本地宏定义      ========================================*/
#define DEFAULT_HOST   "DefaultHost"
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static char configFile[100] = "configInfo.ini";
static char *passwordMD5File = SAVE_DIR "/password.ini";

/*================== 全局共享变量    ========================================*/
saveInfo_t saveInfo = {
  .COMsendPoll = false,
  .COMrecvPoll = false,
  .COMalignedRecv4K = RECV_4K_MAX,
  .hostName = DEFAULT_HOST,
  .serverPrintData = 0,
  .passwordMD5Value = PASSWORD_MD5,
};

runInfo_t  runInfo = {
  .monopolizeComRecvIndex = NULL,
  .monopolizeComSendIndex = NULL,
};

const char *getConfigFilePath(void)
{
  return configFile;
}

void setConfigFilePath(const char *path)
{
  if( strlen(path) > sizeof configFile - 1 )
    return;
  strcpy(configFile, path);
}

void loadConfig(void)
{
  char configFileName[100];
  snprintf(configFileName, sizeof configFileName,
    "%s/configInfo%s-PORT%d.ini", SAVE_DIR, SYSTEM_NAME, getMainServerPort());
  setConfigFilePath(configFileName);

  saveInfo.COMsendPoll = ini_getl("Settings", "COMsendPoll", saveInfo.COMsendPoll, configFile);
  saveInfo.COMrecvPoll = ini_getl("Settings", "COMrecvPoll", saveInfo.COMrecvPoll, configFile);
  saveInfo.COMalignedRecv4K = ini_getl("Settings", "COMalignedRecv4K", saveInfo.COMalignedRecv4K, configFile);
  if( saveInfo.COMalignedRecv4K > RECV_4K_MAX )
    saveInfo.COMalignedRecv4K = RECV_4K_MAX;
  saveInfo.serverPrintData = ini_getl("Settings", "serverPrintData", saveInfo.serverPrintData, configFile);
  ini_gets("Settings", "hostName", saveInfo.hostName, saveInfo.hostName, sizeof saveInfo.hostName, configFile);

  ini_gets("password", "passwordMD5", saveInfo.passwordMD5Value, 
    saveInfo.passwordMD5Value, strlen(PASSWORD_MD5), passwordMD5File);
  if( strlen(saveInfo.passwordMD5Value) != strlen(PASSWORD_MD5) )
    strcpy(saveInfo.passwordMD5Value, PASSWORD_MD5);
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
  
  ini_puts("password", "passwordMD5", saveInfo.passwordMD5Value, passwordMD5File);
  runOnce = false;
}


void saveConfigTest(bool state)
{ 
    printf("配置保存功能已%s用。\n", state? "启":"禁"); 
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