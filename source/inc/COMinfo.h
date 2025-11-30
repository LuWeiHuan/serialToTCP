#ifndef __COM_INFO_H_
#define __COM_INFO_H_

#include <stdint.h>
#include <stdbool.h>

/*================== 数据类型声明    ========================================*/
/*================== 外部变量声明    ========================================*/
/*================== 外部函数声明    ========================================*/

#ifdef __cplusplus  
extern "C" {
#endif

#ifdef __linux
long get_usb_speed_mbps(const char* device_name);
#endif

const char *getComPortList(bool VPID);
void get_COM_VID_PID_REV(const char* portName, char *retVID, char *retPID, char *retREV);
#ifdef __cplusplus
}
#endif

#endif /*__COM_H_*/