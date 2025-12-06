/******************************************************************************
  * @file    文件 COMinfo.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 获取串口信息
  ******************************************************************************
  * @attention 注意
  *
  * 
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/

#include "COMinfo.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <tchar.h>
#else
#include <errno.h> 
#include <dirent.h>
#include <sys/stat.h> 
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#endif
#endif



/*================== 本地数据类型     =========================================*/
/*================== 本地宏定义     =========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
/*================== 全局共享变量    ========================================*/
/*================== 本地函数声明    ========================================*/


#ifdef __linux
/**
 * @brief Linux下获取USB串口设备的VID/PID/REV信息（兼容udev和sysfs方式）
 * @param portName 串口设备名（如 "ttyUSB0", "ttyACM0"）
 * @param retVID 返回的VID字符串（需要至少5字节空间）
 * @param retPID 返回的PID字符串（需要至少5字节空间） 
 * @param retREV 返回的REV字符串（需要至少5字节空间）
 */
void get_COM_VID_PID_REV(const char* portName, char *retVID, char *retPID, char *retREV)
{
    if (!portName || strlen(portName) == 0)
        return;
    
    // 初始化返回值
    if (retVID) strcpy(retVID, "N/A");
    if (retPID) strcpy(retPID, "N/A");
    if (retREV) strcpy(retREV, "N/A");

    // 首先尝试使用udev方式（如果可用）
    #ifdef HAVE_LIBUDEV
    struct udev *udev = udev_new();
    if (udev) {
        struct udev_enumerate *enumerate = udev_enumerate_new(udev);
        if (enumerate) {
            udev_enumerate_add_match_subsystem(enumerate, "tty");
            udev_enumerate_scan_devices(enumerate);
            
            struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
            struct udev_list_entry *entry;
            
            udev_list_entry_foreach(entry, devices) {
                const char *path = udev_list_entry_get_name(entry);
                struct udev_device *device = udev_device_new_from_syspath(udev, path);
                
                if (!device) continue;
                
                const char *devnode = udev_device_get_devnode(device);
                if (!devnode || !strstr(devnode, portName)) {
                    udev_device_unref(device);
                    continue;
                }
                
                // 找到匹配的设备，获取父USB设备
                struct udev_device *parent = udev_device_get_parent_with_subsystem_devtype(
                    device, "usb", "usb_device");
                
                if (!parent) {
                    udev_device_unref(device);
                    continue;
                }
                
                // 获取VID
                const char *vid = udev_device_get_sysattr_value(parent, "idVendor");
                if (vid && retVID) {
                    memcpy(retVID, vid, 4);
                    retVID[4] = '\0';
                }
                
                // 获取PID
                const char *pid = udev_device_get_sysattr_value(parent, "idProduct");
                if (pid && retPID) {
                    memcpy(retPID, pid, 4);
                    retPID[4] = '\0';
                }
                
                // 获取REV
                const char *rev = udev_device_get_sysattr_value(parent, "bcdDevice");
                if (rev && retREV) {
                    memcpy(retREV, rev, 4);
                    retREV[4] = '\0';
                }
                
                udev_device_unref(device);
                
                // 转换为大写
                if (retVID && strcmp(retVID, "N/A") != 0) {
                    for (char *p = retVID; *p; p++) *p = toupper(*p);
                }
                if (retPID && strcmp(retPID, "N/A") != 0) {
                    for (char *p = retPID; *p; p++) *p = toupper(*p);
                }
                if (retREV && strcmp(retREV, "N/A") != 0) {
                    for (char *p = retREV; *p; p++) *p = toupper(*p);
                }
                
                udev_enumerate_unref(enumerate);
                udev_unref(udev);
                return; // 成功通过udev获取，直接返回
            }
            udev_enumerate_unref(enumerate);
        }
        udev_unref(udev);
    }
    #endif // HAVE_LIBUDEV
     
    /*********** 测试命令 *********************
    udevadm info --export-db | grep -A 20 "SUBSYSTEM=usb" | grep -A 20 "DEVTYPE=usb_device"
    ********************************************/
    // 如果udev不可用或获取失败，尝试使用sysfs方式  
    char command[256];    // 构建udevadm命令
    snprintf(command, sizeof command, 
             "udevadm info -q property -n /dev/%s 2>/dev/null", 
             portName);
    
    // 执行命令并读取输出
    FILE *fp = popen(command, "r");
    if (!fp) 
        return;
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0'; // 移除换行符
        
        // 解析VID
        if (strncmp(line, "ID_VENDOR_ID=", 13) == 0 && retVID) {
            memcpy(retVID, line + 13, 4);
            retVID[4] = '\0';
            for (char *p = retVID; *p; p++) *p = toupper(*p);
        }
        // 解析PID
        else if (strncmp(line, "ID_MODEL_ID=", 12) == 0 && retPID) {
            memcpy(retPID, line + 12, 4);
            retPID[4] = '\0';
            for (char *p = retPID; *p; p++) *p = toupper(*p);
        }
        // 解析REV
        else if (strncmp(line, "ID_REVISION=", 12) == 0 && retREV) {
            memcpy(retREV, line + 12, 4);
            retREV[4] = '\0';
            for (char *p = retREV; *p; p++) *p = toupper(*p);
        }
    }
    
    pclose(fp);
}




long get_usb_speed_mbps(const char* device_name) {
    char path[512];
    char speed_str[32];
    FILE* fp;
    
    // 参数检查
    if (device_name == NULL || strlen(device_name) == 0) {
        SafePrintf("Error: Invalid device name\n");
        return -1;
    }
    
    // 提取基础设备名（去掉/dev/前缀）
    const char* base_name = device_name;
    if (strncmp(device_name, "/dev/", 5) == 0) {
        base_name = device_name + 5;
    }
    
    // 尝试多种可能的sysfs路径
    const char* paths[] = {
        "/sys/class/tty/%s/device/../speed",
        "/sys/class/tty/%s/device/speed", 
        "/sys/class/tty/%s/device/../usb_device/speed",
        NULL
    };
    
    for (int i = 0; paths[i] != NULL; i++) {
        snprintf(path, sizeof(path), paths[i], base_name);
        
        fp = fopen(path, "r");
        if (fp != NULL) {
            if (fgets(speed_str, sizeof(speed_str), fp)) {
                fclose(fp);
                
                // 清理字符串
                speed_str[strcspn(speed_str, "\n\r")] = '\0';
                
                // 转换为整数
                char* endptr;
                long speed_value = strtol(speed_str, &endptr, 10);
                
                if (endptr != speed_str) { // 成功转换
                    return speed_value;
                }
            }
            fclose(fp);
        }
    }
    
    SafePrintf("Error: Cannot read speed information for device %s: %s\n", 
            device_name, strerror(errno));
    return -1;
}

// 获取Win系统串口列表。Linux 串口实现
// VPID 是否需要PID、VID和REV信息，真需要，否不需要
const char *getComPortList(bool VPID) 
{
    static char response[4096];
    size_t pos = 0;
    
    // 初始化响应
    const char *header = VPID ? "COM Ports:\n" : "COM Ports: ";
    strcpy(response, header);
    pos = strlen(header);
    
    DIR *dir;
    struct dirent *entry;
    bool exist = false;
    
    dir = opendir("/dev");
    if (!dir) return "无法访问 /dev 目录";
    
    while ((entry = readdir(dir)) != NULL) {
        if (  strncmp(entry->d_name, "ttyS", 4) == 0 &&
          '0' <= entry->d_name[4] && entry->d_name[4] <= '9' )
          continue;

        if (strncmp(entry->d_name, "ttyS", 4) == 0 || 
            strncmp(entry->d_name, "ttyUSB", 6) == 0 ||
            strncmp(entry->d_name, "ttyACM", 6) == 0) {
            
            // 设备有效性检查
            char fullPath[32];
            snprintf(fullPath, sizeof(fullPath), "/dev/%.10s", entry->d_name);
            struct stat st;
            if (stat(fullPath, &st) != 0 || !S_ISCHR(st.st_mode)) {
                continue;
            }
            
            // 添加分隔符
            if (exist) {
                const char *separator = VPID ? ",\n" : ", ";
                size_t sep_len = strlen(separator);
                if (pos + sep_len < sizeof(response)) {
                    strcat(response + pos, separator);
                    pos += sep_len;
                }
            }
            
            // 添加设备信息
            if (VPID) {
                char vid[5] = "N/A", pid[5] = "N/A", rev[5] = "N/A";
                
                get_COM_VID_PID_REV(entry->d_name, vid, pid, rev);
                
                char info[285];
                snprintf(info, sizeof(info), "%-12s [VID_%-4s PID_%-4s REV_%-4s]", 
                        entry->d_name, vid, pid, rev);
                
                size_t info_len = strlen(info);
                if (pos + info_len < sizeof(response)) {
                    strcat(response + pos, info);
                    pos += info_len;
                }
            } else {
                size_t name_len = strlen(entry->d_name);
                if (pos + name_len < sizeof(response)) {
                    strcat(response + pos, entry->d_name);
                    pos += name_len;
                }
            }
            
            exist = true;
        }
    }
    
    closedir(dir);
    
    if (!exist) {
        if (pos + strlen("No COM ports found") < sizeof(response)) {
            strcat(response + pos, "No COM ports found");
        }
    }
    
    return response;
}
#else

// 获取Win系统串口列表。Windows 串口实现
// VPID 是否需要PID、VID和REV信息，真需要，否不需要
const char *getComPortList(bool VPID) 
{
  // EnterCriticalSection_Wrapper(&csComPort);
  HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
  if (hDevInfo == INVALID_HANDLE_VALUE) 
    return "COM Ports: GUID_DEVCLASS_PORTS NULL";
      
  bool exist = false;  
  static char response[2048];
  memset(response, 0, sizeof response);

  #if 1
  strcpy(response, VPID? "COM Ports:\n": "COM Ports: ");
  #else
  sprintf(response, "[%s %s] %s",
    comPort.isOpen? comPort.portName:"COM",
    comPort.isOpen? "has been opened":"not open",
    VPID? "COM Ports:\n": "COM Ports: "); 
  #endif

  SP_DEVINFO_DATA deviceInfoData;
  deviceInfoData.cbSize = sizeof deviceInfoData;
  BYTE buffer[256];

  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); i++) {
    memset(buffer, 0, sizeof buffer);
    DWORD dataType, bufferSize = sizeof buffer;

    bool ret = SetupDiGetDeviceRegistryPropertyA(hDevInfo, &deviceInfoData, 
        SPDRP_FRIENDLYNAME, &dataType, buffer, bufferSize, &bufferSize);
        
    if (ret == false) 
        continue;
    // 更精确地查找COM端口号 - 查找最后一个括号内的COMX
    char* start = strrchr((char*)buffer, '(');
    if (start == NULL)
        continue;
        
    char* end = strchr(start, ')');
    if (end == NULL)
        continue;
    
    *end = '\0';  // 临时截断字符串
    
    // 检查括号内的内容是否为COM端口
    char* portName = start + 1; // 跳过'('
    if (strncmp(portName, "COM", 3) != 0) {
        *end = ')'; // 如果不是COM端口，恢复字符串并跳过
        continue;
    }
    
    // 确认这是有效的COM端口号（后面跟着数字）
    if (strlen(portName) > 3 && isdigit(portName[3])) {
      if (exist) 
          strcat(response, VPID? ",\n":", ");
      
      static char retID[3][5], IDstring[50];
      if( VPID ){
        memset(retID, 0, sizeof retID);
        memset(IDstring, 0, sizeof IDstring);
        get_COM_VID_PID_REV(portName, retID[0], retID[1], retID[2]);
        snprintf(IDstring, sizeof IDstring, "%-6s [VID_%-4s PID_%-4s REV_%-4s]", 
          portName, retID[0], retID[1], retID[2]);
      }

      strcat(response, VPID? IDstring:portName);
        exist = true;
    }

    *end = ')'; // 恢复原始字符串
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);

  if (exist == false)
    strcat(response, "No COM ports found");

  // LeaveCriticalSection_Wrapper(&csComPort);
  return response;
}

// 获取设备属性
static LPTSTR GetDeviceProperty(HDEVINFO hDevInfo, PSP_DEVINFO_DATA pDevInfoData, DWORD Property)
{
  static TCHAR buffer[1024];
  DWORD nSize = 0, dataType = 0;
  memset(buffer, 0, sizeof buffer);
  // 第一次调用获取所需缓冲区大小
  if (!SetupDiGetDeviceRegistryProperty(hDevInfo, pDevInfoData, Property, NULL, NULL, 0, &nSize))
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
      return NULL;
  
  // 检查是否需要缓冲区超出静态数组大小
  if (nSize > sizeof buffer)
    return NULL;

  // 第二次调用获取实际数据
  if (!SetupDiGetDeviceRegistryProperty(hDevInfo, pDevInfoData, 
    Property, &dataType, (PBYTE)buffer, sizeof buffer, NULL))
      return NULL;
  
  return buffer;
}

// 获取COM串口设备VID和PID，VID和PID长度大概在5个字符，可以给多一点
void get_COM_VID_PID_REV(const char* portName, char *retVID, char *retPID, char *retREV)
{
  if( portName == NULL )
    return;

  // 获取所有端口设备信息
  HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
  if (hDevInfo == INVALID_HANDLE_VALUE) {
      SafePrintf("SetupDiGetClassDevs failed. Error: %ld\n", GetLastError());
      return;
  }

  SP_DEVINFO_DATA devInfoData;
  devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
  // 枚举所有端口设备 
  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
    // 获取设备友好名称，并 检查是否是指定的串口
    LPTSTR DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
    if (DeviceInfo == NULL || _tcsstr(DeviceInfo, portName) == NULL )
      continue;
    
    #if 0
    SafePrintf("Found port: %s\n", DeviceInfo);
    // 获取设备描述
    DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_DEVICEDESC);
    if (DeviceInfo != NULL) 
        SafePrintf("Device Description: %s\n", DeviceInfo); 

    // 获取制造商信息
    DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_MFG);
    if (DeviceInfo != NULL) 
        SafePrintf("Manufacturer: %s\n", DeviceInfo);

    // 获取硬件ID
    DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_HARDWAREID);
    if (DeviceInfo != NULL) 
        SafePrintf("Hardware ID: %s\n", DeviceInfo);
    SafePrintf("\n");

    for (uint8_t j = 0; j < SPDRP_MAXIMUM_PROPERTY; j++) { 
      DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, j);
      if (DeviceInfo != NULL) 
          SafePrintf("DeviceInfo 0x%02X: %s\n", j, DeviceInfo);
    }
    SafePrintf("\n");
    #endif

    // 获取硬件ID
    DeviceInfo = GetDeviceProperty(hDevInfo, &devInfoData, SPDRP_HARDWAREID);
    if (DeviceInfo != NULL) {   // 从硬件ID中提取VID和PID 
        TCHAR* vidPos = _tcsstr(DeviceInfo, _T("VID_"));
        TCHAR* pidPos = _tcsstr(DeviceInfo, _T("PID_"));
        TCHAR* revPos = _tcsstr(DeviceInfo, _T("REV_"));
        if( retVID )
          memcpy(retVID, vidPos? vidPos + 4 :"NULL", 4);
        if( retPID )
          memcpy(retPID, pidPos? pidPos + 4 :"NULL", 4);
        if( retREV )
          memcpy(retREV, revPos? revPos + 4 :"NULL", 4);
    }
    
    break;
  }

  if (GetLastError() != NO_ERROR && GetLastError() != ERROR_NO_MORE_ITEMS)
      SafePrintf("SetupDiEnumDeviceInfo failed. Error: %ld\n", GetLastError());

  SetupDiDestroyDeviceInfoList(hDevInfo);
}

#endif



/**
 * @brief 检查串口是否存在（跨平台实现）
 * @param portName 串口名称
 * @return true 存在，false 不存在
 */
bool COM_PortExists(const char* portName)
{
    if (portName == NULL || strlen(portName) == 0) {
        return false;
    }

#ifdef _WIN32
    /*================== Windows 实现 ==================*/
    
    // 构建完整的设备路径
    char fullPortName[32];
    snprintf(fullPortName, sizeof(fullPortName), "\\\\.\\%s", portName);
    
    // 尝试以只读方式打开串口
    HANDLE hCom = CreateFileA(
        fullPortName,
        GENERIC_READ | GENERIC_WRITE,
        0,                          // 独占模式
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (hCom != INVALID_HANDLE_VALUE) {
        // 成功打开，说明串口存在
        CloseHandle(hCom);
        return true;
    }
    
    // 如果打开失败，检查错误码
    DWORD error = GetLastError();
    
    // 以下是串口存在但可能无法打开的常见错误码：
    // ERROR_ACCESS_DENIED (5)     - 串口被其他程序占用
    // ERROR_SHARING_VIOLATION (32) - 串口被共享冲突
    // ERROR_GEN_FAILURE (31)      - 一般设备错误（但设备存在）
    
    if (error == ERROR_ACCESS_DENIED || 
        error == ERROR_SHARING_VIOLATION || 
        error == ERROR_GEN_FAILURE) {
        // 这些错误通常表示串口存在但无法访问
        return true;
    }
    
    // 其他错误码通常表示串口不存在
    // ERROR_FILE_NOT_FOUND (2)    - 文件/设备未找到
    // ERROR_PATH_NOT_FOUND (3)    - 路径未找到
    // ERROR_INVALID_NAME (123)    - 文件名无效
    
    return false;

#else
    /*================== Linux 实现 ==================*/
    
    char fullPath[64];
    
    // 检查是否已包含 /dev/ 前缀
    if (strncmp(portName, "/dev/", 5) == 0) {
        // 已有 /dev/ 前缀，直接使用
        snprintf(fullPath, sizeof(fullPath), "%s", portName);
    } else if (strncmp(portName, "tty", 3) == 0) {
        // 是 tty 设备名，添加 /dev/ 前缀
        snprintf(fullPath, sizeof(fullPath), "/dev/%s", portName);
    } else {
        // 尝试直接作为设备名
        snprintf(fullPath, sizeof(fullPath), "/dev/%s", portName);
    }
    
    // 方法1：使用 access() 检查文件是否存在且可访问
    if (access(fullPath, F_OK) == 0) {
        // 文件存在，进一步检查是否为字符设备
        struct stat st;
        if (stat(fullPath, &st) == 0) {
            if (S_ISCHR(st.st_mode)) {
                // 是字符设备文件，很可能是串口
                return true;
            }
        }
    }
    
    // 方法2：扫描 /dev 目录检查设备是否存在
    DIR *dir = opendir("/dev");
    if (dir == NULL) {
        return false;
    }
    
    struct dirent *entry;
    bool exists = false;
    
    // 提取基础设备名（去掉可能的前缀）
    const char *baseName = portName;
    if (strncmp(portName, "/dev/", 5) == 0) {
        baseName = portName + 5;
    }
    
    char checkPath[300];
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, baseName) == 0) {
            // 构建完整路径 
            snprintf(checkPath, sizeof(checkPath), "/dev/%s", entry->d_name);
            
            // 检查是否为字符设备
            struct stat st;
            if (stat(checkPath, &st) == 0 && S_ISCHR(st.st_mode)) {
                exists = true;
                break;
            }
        }
        
        // 同时检查常见的串口设备前缀
        if (strncmp(entry->d_name, "ttyS", 4) == 0 && 
            strcmp(entry->d_name + 4, baseName + 4) == 0) {
            // 匹配 ttyS* 设备 
            snprintf(checkPath, sizeof(checkPath), "/dev/%s", entry->d_name);
            
            struct stat st;
            if (stat(checkPath, &st) == 0 && S_ISCHR(st.st_mode)) {
                exists = true;
                break;
            }
        }
        
        if (strncmp(entry->d_name, "ttyUSB", 6) == 0 && 
            strcmp(entry->d_name + 6, baseName + 6) == 0) {
            // 匹配 ttyUSB* 设备 
            snprintf(checkPath, sizeof(checkPath), "/dev/%s", entry->d_name);
            
            struct stat st;
            if (stat(checkPath, &st) == 0 && S_ISCHR(st.st_mode)) {
                exists = true;
                break;
            }
        }
        
        if (strncmp(entry->d_name, "ttyACM", 6) == 0 && 
            strcmp(entry->d_name + 6, baseName + 6) == 0) {
            // 匹配 ttyACM* 设备 
            snprintf(checkPath, sizeof(checkPath), "/dev/%s", entry->d_name);
            
            struct stat st;
            if (stat(checkPath, &st) == 0 && S_ISCHR(st.st_mode)) {
                exists = true;
                break;
            }
        }
    }
    
    closedir(dir);
    return exists;
#endif
}
