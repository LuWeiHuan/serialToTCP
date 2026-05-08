
#ifndef __CONFIG_SAVE_H_
#define __CONFIG_SAVE_H_

#ifdef __cplusplus  
extern "C" {
#endif

/*================== 头文件包含			=========================================*/
#include <stdbool.h>
#include <stdint.h>
/*================== 宏定义声明			=========================================*/

/*================== 数据类型声明		=========================================*/
//struct enum union
typedef struct {
  bool      COMsendPoll;        // 就是就是发给串口的日志要不要滚动
  bool      COMrecvPoll;        // 就是串口发上来的每条数据条目要不要滚动
  uint8_t   serverPrintData;    // 0，不显示，1为字符串显示，2为Hex显示，3只显示命令
  uint32_t  COMalignedRecv4K;   // 设置串口接收多少个对齐数据包就发送
  char      hostName[50];       // 保存的主机名
  char      passwordMD5[32];    // 密码MD5值
} saveInfo_t;

typedef struct {
  uint16_t *monopolizeComRecvIndex;  // 独享 串口收到的数据
  uint16_t *monopolizeComSendIndex;  // 独享 数据发给串口  
} runInfo_t;

/*================== 外部变量声明		=========================================*/
extern saveInfo_t saveInfo;
extern runInfo_t runInfo;


/*================== 外部函数声明		=========================================*/
void saveConfigTest(bool state);
void loadConfig(void);
void saveConfig(void);
const char *getConfigFilePath(void);
void setConfigFilePath(const char *path);

#ifdef __cplusplus
}
#endif

#endif /*__CONFIG_SAVE_H_*/







