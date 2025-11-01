/******************************************************************************
  * @file    文件 DCM.c 
  * @author  作者 
  * @version 版本 V1.1
  * @date    日期 2025-08-17
  * @brief   简介 监控Win设备插入或拔出
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include <winsock2.h>
#include <windows.h>
#include <dbt.h>       // 设备通知相关定义
#include <setupapi.h>  // 设备安装相关
#include <initguid.h>  // GUID定义

#include "logPrint.h"
#include "public.h"
#include "COM.h"

/*================== 本地宏定义     =========================================*/
// #define WM_USER_DEVICE_CHANGE (WM_USER + 100)  // 自定义设备变更消息

/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static volatile BOOL g_bDeviceChangeThreadRunning = FALSE;
static HWND g_hDeviceMonitorWnd = NULL;

/*================== 本地函数声明    ========================================*/
static LRESULT CALLBACK DeviceMonitorWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
static void HandleDeviceChange(WPARAM wParam, LPARAM lParam);

/*================== 外部函数和变量声明    ==================================*/

// 自定义窗口过程函数，专门处理设备变更消息
static LRESULT CALLBACK DeviceMonitorWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_DEVICECHANGE:
            HandleDeviceChange(wParam, lParam);
            return TRUE;
            
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
    updataConsoleTitle("DCM changed");
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
            sendComPortsListToClient(NULL, true);
        } break;
            
        default: // 其他设备变更事件
        break;
    }
}

// 设备变化通知线程
static DWORD WINAPI DeviceChangeMonitorThread(void *lpParam)
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

    g_bDeviceChangeThreadRunning = TRUE;
    // SafePrintf("Device change monitor started successfully\n");
    
    MSG msg;
    while (g_bDeviceChangeThreadRunning && GetMessage(&msg, NULL, 0, 0)) {
      updataConsoleTitle("DCM Thread");
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

void DeviceChangeMonitor(bool state)
{
    static HANDLE g_hDeviceChangeThread = NULL; 
    
    if (state) { // 启动设备监听线程
        if (g_hDeviceChangeThread == NULL) {
            g_hDeviceChangeThread = CreateThread(NULL, 0, 
                DeviceChangeMonitorThread, NULL, 0, NULL);
                
            if (g_hDeviceChangeThread == NULL)
                SafePrintf("CreateThread failed: %ld\n", GetLastError());
        }
    }
    else { // 停止设备监听线程
        g_bDeviceChangeThreadRunning = FALSE;
        
        // 发送退出消息
        if (g_hDeviceMonitorWnd)
            PostMessage(g_hDeviceMonitorWnd, WM_QUIT, 0, 0);

        if (g_hDeviceChangeThread) {
            WaitForSingleObject(g_hDeviceChangeThread, 1000);
            CloseHandle(g_hDeviceChangeThread);
            g_hDeviceChangeThread = NULL;
        }
    }
}
