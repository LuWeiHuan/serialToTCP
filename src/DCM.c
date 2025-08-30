/******************************************************************************
  * @file    文件 DCM.c 
  * @author  作者 
  * @version 版本 V1.0
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
#include <winuser.h>   // 窗口消息相关

#include "main.h"
#include "logPrint.h"
#include "public.h"
#include "client.h"

/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量    ========================================*/
/*================== 本地常量声明    ========================================*/
/*================== 本地变量声明    ========================================*/
static volatile BOOL g_bDeviceChangeThreadRunning = FALSE;

/*================== 本地函数声明    ========================================*/
/*================== 外部函数和变量声明    ==================================*/


// 设备变化通知线程
static DWORD WINAPI DeviceChangeMonitorThread(LPVOID lpParam)
{
    (void)lpParam;

    // 创建隐藏窗口接收消息
    HWND hWnd = CreateWindowEx(0, "STATIC", "DeviceMonitor", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);

    // 设置设备接口通知
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter = {0};
    NotificationFilter.dbcc_size = sizeof NotificationFilter;
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_COMPORT;

    HDEVNOTIFY hDevNotify = RegisterDeviceNotification(hWnd, &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (hDevNotify == NULL) {
        SafePrintf("RegisterDeviceNotification failed: %ld\n", GetLastError());
        DestroyWindow(hWnd);
        return 1;
    }

    g_bDeviceChangeThreadRunning = TRUE;
    MSG msg;
    while ( g_bDeviceChangeThreadRunning && GetMessage(&msg, hWnd, 0, 0) ) {
      updataConsoleTitle("DCM",  GetCurrentThreadId());

      // 打开 word 后 任何地方按下 crtl+c crtl+V 等快捷键操作这里的消息就会变的很多*/
      SafePrintf("Device change detected, wParam:%I64d, lParam:%I64d, "
        "message:%d, XY(%ld:%ld), time:%ld hwnd:0x%I64d theradID:%ld\n",
        msg.wParam, msg.lParam, msg.message, msg.pt.x, msg.pt.y, msg.time, 
        (uint64_t)msg.hwnd, GetCurrentThreadId());
      
      // 以下参数是设备插拔或最明显的变化
      if( msg.wParam == 0 && msg.lParam == 0 ){ 
        printfSend(NULL, "Device change detected (%I64d:%d)\n", 
          msg.wParam, msg.message);
        sendComPortsListToClient( NULL ); 
      }

      #if 0
        if (msg.message == WM_DEVICECHANGE) {
            switch (msg.wParam) {
                case DBT_DEVICEARRIVAL:         // 设备插入
                case DBT_DEVICEREMOVECOMPLETE:  // 设备拔出 
                    // 通知所有客户端串口列表变化
                    break;
            }
        }
        #endif
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理
    UnregisterDeviceNotification(hDevNotify);
    DestroyWindow(hWnd);
    return 0;
}


void DeviceChangeMonitor(bool state)
{
  static HANDLE g_hDeviceChangeThread = NULL; 
  if( state ) {// 启动设备监听线程
    WNDCLASS wc = {0};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "DeviceMonitor";
    RegisterClass(&wc);

    if (g_hDeviceChangeThread == NULL)
      g_hDeviceChangeThread = CreateThread(NULL, 0, 
          DeviceChangeMonitorThread, NULL, 0, NULL);
  }
  else{// 停止设备监听线程
    g_bDeviceChangeThreadRunning = FALSE;
    if (g_hDeviceChangeThread) {
        WaitForSingleObject(g_hDeviceChangeThread, 1000);
        CloseHandle(g_hDeviceChangeThread);
        g_hDeviceChangeThread = NULL;
    }
  }

}
 






