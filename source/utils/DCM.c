/******************************************************************************
  * @file    文件 DCM.c 
  * @author  作者 
  * @version 版本 V1.1
  * @date    日期 2025-08-17
  * @brief   简介 监控系统USB设备插入或拔出
  ******************************************************************************
  * @attention 注意
  *
  *
    编译要求：Linux系统需要安装libudev开发包：
    # Ubuntu/Debian
    sudo apt-get install libudev-dev

    # CentOS/RHEL
    sudo yum install libudev-devel

    权限要求：读取串口设备需要相应权限：
    # 将用户添加到dialout组
    sudo usermod -a -G dialout $USER

    # 或者设置设备权限
    sudo chmod 666 /dev/ttyUSB0
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/


#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <dbt.h>       // 设备通知相关定义
#include <setupapi.h>  // 设备安装相关
#include <initguid.h>  // GUID定义
#else
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#endif  // HAVE_LIBUDEV

#endif

#include "log.h"
#include "platform.h"

/*================== 本地宏定义     =========================================*/
// #define WM_USER_DEVICE_CHANGE (WM_USER + 100)  // 自定义设备变更消息

/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static volatile bool g_bDeviceChangeThreadRunning = false;
#ifdef _WIN32
static HWND g_hDeviceMonitorWnd = NULL;
#endif


/*================== 本地函数声明    ========================================*/
#ifdef _WIN32
static LRESULT CALLBACK DeviceMonitorWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
static void HandleDeviceChange(WPARAM wParam, LPARAM lParam);
#endif

/*================== 外部函数声明    ========================================*/
extern void usbDeviceChange(void);

static threadRet WINAPI DeviceChangeMonitorThread(void *);

__attribute__((weak)) void usbDeviceChange(void)
{
  SafePrintf("USB 产生了插拔变化，但是用户没有定义这个函数调用\n");
}


void DeviceChangeMonitor(bool state)
{
    static thread_t g_hDeviceChangeThread = (thread_t)0; 
 

    if (state) {  // 启动设备监听线程
        if (g_hDeviceChangeThread == (thread_t)0) {
 
            g_hDeviceChangeThread = threadCreate(NULL, DeviceChangeMonitorThread, NULL);
              
            if (g_hDeviceChangeThread == (thread_t)0)
                SafePrintf("CreateThread failed: %ld\n", GetLastError());
        }
    }
    else {    // 停止设备监听线程
        g_bDeviceChangeThreadRunning = false;
        
        if (g_hDeviceChangeThread) {
            WaitForSingleObject_Wrapper(g_hDeviceChangeThread, 1000);
            CloseHandle(g_hDeviceChangeThread);
            g_hDeviceChangeThread = (thread_t)0;
        }
    }
}


#ifdef _WIN32

// 自定义窗口过程函数，专门处理设备变更消息
static LRESULT CALLBACK DeviceMonitorWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_DEVICECHANGE:
            HandleDeviceChange(wParam, lParam);
            return true;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
            
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
}

// 处理设备变更事件
static void HandleDeviceChange(WPARAM wParam, LPARAM lParam)
{
    PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam; (void)pHdr;
    #ifdef __COMMON_UTILS_H_
    updataConsoleTitle("DCM changed");
    #endif // __COMMON_UTILS_H_
    
    switch (wParam)
    {
        #if 0
        case DBT_DEVICEARRIVAL:         // 设备插入
            if (pHdr && pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr; 
                SafePrintf("Device arrived, type %ld, size %lX :%s\n", 
                  pDevInf->dbcc_devicetype, pDevInf->dbcc_size, 
                  pDevInf->dbcc_name);
            }
        break;
            
        case DBT_DEVICEREMOVECOMPLETE:  // 设备拔出
            if (pHdr && pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
                SafePrintf("Device removed, type %ld, size %lX :%s\n", 
                  pDevInf->dbcc_devicetype, pDevInf->dbcc_size, 
                  pDevInf->dbcc_name);
            }
        break;
        #endif
        case DBT_DEVNODES_CHANGED:{ // 设备节点变化 
            static uint32_t count = 0;
            SafePrintf("Device nodes changed %-5d\r", ++count);
            usbDeviceChange();
        } break;
            
        default: // 其他设备变更事件
        break;
    }
}

// 设备变化通知线程
static threadRet WINAPI DeviceChangeMonitorThread(void *lpParam)
{
    (void)lpParam;

    // 注册窗口类
    WNDCLASSEX wcex = {0};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc = DeviceMonitorWndProc;
    wcex.hInstance = GetModuleHandle(NULL);
    wcex.lpszClassName = "DeviceMonitorClass";
    
    if (!RegisterClassEx(&wcex)) {
        SafePrintf("RegisterClassEx failed: %ld\n", GetLastError());
        return 1;
    }

    // 创建隐藏窗口接收消息
    g_hDeviceMonitorWnd = CreateWindowEx(0, 
      "DeviceMonitorClass",  "DeviceMonitorWindow", 
        WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, 
        NULL, NULL,  GetModuleHandle(NULL),  NULL);
    
    if (!g_hDeviceMonitorWnd) {
        SafePrintf("CreateWindowEx failed: %ld\n", GetLastError());
        UnregisterClass("DeviceMonitorClass", GetModuleHandle(NULL));
        return 1;
    }

    // 设置设备接口通知
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter = {0};
    NotificationFilter.dbcc_size = sizeof(NotificationFilter);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_COMPORT;

    HDEVNOTIFY hDevNotify = RegisterDeviceNotification(
        g_hDeviceMonitorWnd,  &NotificationFilter, 
        DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
        
    if (hDevNotify == NULL) {
        SafePrintf("RegisterDeviceNotification failed: %ld\n", GetLastError());
        DestroyWindow(g_hDeviceMonitorWnd);
        UnregisterClass("DeviceMonitorClass", GetModuleHandle(NULL));
        return 1;
    }

    g_bDeviceChangeThreadRunning = true;
    // SafePrintf("Device change monitor started successfully\n");
    
    MSG msg;
    while (g_bDeviceChangeThreadRunning && GetMessage(&msg, NULL, 0, 0)) {

      #ifdef __COMMON_UTILS_H_
      updataConsoleTitle("DCM Thread");
      #endif // __COMMON_UTILS_H_
      
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }

    // 清理
    UnregisterDeviceNotification(hDevNotify);
    DestroyWindow(g_hDeviceMonitorWnd);
    UnregisterClass("DeviceMonitorClass", GetModuleHandle(NULL));
    g_hDeviceMonitorWnd = NULL;
    
    return 0;
}

#else


// Linux 设备监控实现
static threadRet WINAPI DeviceChangeMonitorThread(void *lpParam)
{
    (void)lpParam;

#ifdef HAVE_LIBUDEV 
    struct udev *udev = udev_new();
    g_bDeviceChangeThreadRunning = udev? true:false;
    if (!udev) {
        SafePrintf("Failed to create udev context, using fallback method\n");
        goto fallback_monitor;  // 如果udev不可用，使用轮询方式
    }

    // 监控USB设备
    struct udev_monitor *mon = udev_monitor_new_from_netlink(udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(mon, "usb", NULL);
    udev_monitor_filter_add_match_subsystem_devtype(mon, "tty", NULL);
    udev_monitor_enable_receiving(mon);
    
    int fd = udev_monitor_get_fd(mon);
    
    SafePrintf("Linux device monitor started (using udev)\n");
    
    while(g_bDeviceChangeThreadRunning) {
        #ifdef __COMMON_UTILS_H_
        updataConsoleTitle("DCM Thread");
        #endif // __COMMON_UTILS_H_

        fd_set fds;
        struct timeval tv;
        
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int ret = select(fd + 1, &fds, NULL, NULL, &tv); 
        if (ret <= 0 || !FD_ISSET(fd, &fds)) 
          continue;

        struct udev_device *dev = udev_monitor_receive_device(mon);
        if (dev == NULL) 
          continue;

        const char *action = udev_device_get_action(dev);
        const char *subsystem = udev_device_get_subsystem(dev);
        
        if (action && subsystem) {
            if (strcmp(subsystem, "tty") == 0) {
                SafePrintf("Serial device %s: %-20s\r", action, udev_device_get_devnode(dev));
                usbDeviceChange();
            }
        }
        udev_device_unref(dev); 
    }
    
    udev_monitor_unref(mon);
    udev_unref(udev);
fallback_monitor:
#endif // HAVE_LIBUDEV

    // udev不可用时的回退方案 
    g_bDeviceChangeThreadRunning = true;
    SafePrintf("Linux device monitor started (using polling fallback)\n");
    
    // 简单的轮询方式检测设备变化
    uint16_t device_count = 0;
    
    while (g_bDeviceChangeThreadRunning) {
      Sleep(1500); // 每1.5秒检查一次设备列表

      #ifdef __COMMON_UTILS_H_
      updataConsoleTitle("DCM Thread (Polling)");
      #endif // __COMMON_UTILS_H_ 

      // 检查 /dev 目录下的串口设备数量
      DIR *devDir = opendir("/dev");
      if (devDir == NULL)
        break;

      struct dirent *entry;
      uint16_t new_count = 0;
      
      while ((entry = readdir(devDir)) != NULL) {
        if (strncmp(entry->d_name, "ttyUSB", 6) == 0 ||
            strncmp(entry->d_name, "ttyACM", 6) == 0) 
          new_count++;
      }
      closedir(devDir);
      
      // 如果设备数量发生变化，通知客户端
      if (new_count != device_count) {
          device_count = new_count;
          SafePrintf("Device count changed: %d devices\n", device_count);
          usbDeviceChange();
      }
    }
    
    SafePrintf("Linux device monitor stopped\n");
    return (threadRet)0;
}
#endif