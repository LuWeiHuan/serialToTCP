# serialToTCP - 串口转 TCP 服务器

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue)](https://gitee.com/wei_huan/serial-to-tcp)

## 项目简介

`serialToTCP` 是一个用 C 语言编写的轻量级服务端程序，主要功能是将串口数据透明地转发到 TCP 网络，反之亦然。
它可以将一个物理串口或虚拟串口转换为一个 TCP 服务器，使得任何支持 TCP 协议的客户端都能方便地与串口设备进行通信。

### 核心特性

-   **透明转发**：客户端发送的非控制指令数据将直接发送至串口，从串口接收到的数据也会广播给所有连接的客户端。
-   **动态串口管理**：支持客户端通过约定的控制指令动态获取串口列表、打开/关闭串口并配置参数（波特率、数据位等）。
-   **多客户端支持**：服务端可同时接受多个客户端连接，并可根据优先级策略（踢掉最早连接）管理连接。
-   **异步处理**：采用独立线程和队列机制实现串口数据的异步发送与接收，提高性能与稳定性。
-   **设备热插拔感知**：能够检测串口设备的插拔事件，并实时通知所有客户端。
-   **服务发现**：通过 IPv4 UDP 广播 和 IPv6 UDP 组播（组播地址"ff02::1"）端口 19000，实现服务端在局域网内的自动发现。
注意：有的WIFI路由器不会转发IPv6 UDP组播消息，会导致IPv6搜索功能会失效，需要自己去WIFI路由器里配置开启
-   **跨平台支持**：提供 Windows 和 Linux（x86 架构）下的预编译可执行文件，并支持从源码编译。

## 快速开始

### 1. 获取程序

-   **直接运行**：从本仓库的 `build` 文件夹中下载对应您操作系统（Windows/Linux）的预编译可执行文件 `com2tcp_server.exe` 或 `com2tcp_server`。
-   **自行编译**：请参考下文 [编译指南](#编译指南) 章节。
build 文件夹里有几个平台的发行版本的可执行文件，如果没有可能要您自己手动构建一下。

### 2. 运行服务端
在命令行中运行服务端程序：

# Windows
.\com2tcp_server.exe -p 9000

# Linux
./com2tcp_server -p 9000
默认监听端口为 9000，若被占用则自动递增。
可通过 -p 参数指定端口号，例如：./com2tcp_server -p 5000。

### 3. 测试客户端
进入仓库中的 测试工具 文件夹，运行 NetAssist.exe 网络调试助手，连接到服务端的 IP 和端口（如 127.0.0.1:9000）。
连接成功后，服务端会回复你在这个服务器里的索引位置。
您可以通过快捷指令或发送约定的 ctrlInfo: 命令来获取串口列表、打开串口等。
客户端控制指令 (Client Commands)
所有指令不区分大小写，并以 \n 结尾。指令格式为：ctrlInfo:指令内容。


## 核心指令：  (所有指令不区分大小写)
获取系统内可用的串口列表
ctrlInfo:comlist      只有COM口号
ctrlInfo:comlistID    有COM口号的同时增加PID、VID和REV等信息，可用于识别是不是之家的产品
ctrlInfo:comlistVPID  和 comlistID 命令一样

打开串口并使用默认参数。默认波特率921600，数据位8位，停止位1位，无校验位
ctrlInfo:open,COM4              打开串口其他参数默认
ctrlInfo:open,COM4,115200       打开串口并设置波特率，其他默认
ctrlInfo:open,COM1,9600,8,1,0   打开串口并设置全部参数

## 剩下的是一些扩展命令，可根据自己情况需要去去使用
让服务端退出仅此而已。服务器如果运行在 10000 端口的话，会拒绝这个指令会，用于复活服务端
ctrlInfo:exit

让服务端运行再运行一个新的服务端并不指定端口号，端口号被占用会递增加1
ctrlInfo:runNewServer                不指定端口号
ctrlInfo:runNewServer,-p 10000       指定端口号

串口独占
ctrlInfo:setCOMdata,Send,my|all	设置发送权限：my（仅自己）或 all（所有客户端）。
ctrlInfo:setCOMdata,Recv,my|all	设置接收权限：my（仅自己）或 all（所有客户端）。

设置客户端发给串口的数据，是否用异步队列缓存起来后，再发给串口
ctrlInfo:setCOMasyncSend,<队列长度>
设置接受到串口的数据，是否用异步队列缓存起来后，逐一发给所有客户端
ctrlInfo:setCOMasyncRecv,<队列长度>
<队列长度>如果是0则是取消异步队列功能，失败会有回复限定数量

获取服务端上的所有已经连接的客户端IP和索引
ctrlInfo:PrintAllclientIP

设置服务端控制台打印客户端发过来的数据和串口收到的数据，
打印方式里面 打印16进制
ctrlInfo:serverPrintData,NILL       关闭数据打印
ctrlInfo:serverPrintData,CMD        只打印命令其它不打印
ctrlInfo:serverPrintData,HEX        打印16进制
ctrlInfo:serverPrintData,ASCII      打印字符串

设置收发日志是否滚动切换，由于没有设计保存设置，这个收发默认都开启
ctrlInfo:cmdSetLogPollCut,recv      串口发上来 的日志滚动
ctrlInfo:cmdSetLogPollCut,send      发给串口   的日志滚动
ctrlInfo:cmdSetLogPollCut,se        匹配不上 recv 或 send 都切换滚动

让所有客户端下线
ctrlInfo:KickAllClients

自动打开串口功能，用于快速自动重开串口，程序重启也会自动打开
ctrlInfo:autoReOpenPort,0   关闭
ctrlInfo:autoReOpenCOM,1    启用

设置接收串口处理对齐数据，单位 4KByte。
ctrlInfo:setCOMalignedNum,5               提示：其中5可以自定义数量
对齐数据一般是操作系统为了各种考虑，将串口数据接缓存后给到我们，然后。
Win 系统通常会出现接收 4~8KByte ，Linux 系统则是 128（低速串口常见）、1024、4095（极少出现也会有）。
如果串口每次出数据都通过网络发送给客户端，客户端即便上了环形缓冲区，
在WIFI网络环境下也会常常丢数据，网线就不容易丢数据。
故此制作了这个命令用来缓存指定数量对齐数据后通过网络发送出去。
不能理解的话，简单的判断方法是：
如果是WIFI无线的情况下，可以设置成提供的最大值，网线的情况下可以设置成 0 或任意设置。
如果你的数据全是对齐数据包的话也建议设置成 0 即可。

## 高级指令（需要验证密码）
单纯的让客户端执行一条操作系统 CMD 命令。
这条指令算是一个漏洞，需要验证设置的密码才能使用
ctrlInfo:SystemCommands,<CMD>             执行一条命令
ctrlInfo:ExecuteSystemCommands,<CMD>      执行一条命令
ctrlInfo:SystemCommandsGetResult,<CMD>    执行一条命令并获取结果
ctrlInfo:ExecuteSystemCommandsGetResult,<CMD> 执行一条命令并获取结果

验证和设置密码，密码传递是MD5值，不传递明文。
默认密码是字符串 "COM2TCP" 进行MD5运算后的结果
cls 和 clear 可以不用验证密码
ctrlInfo:VerifyPassword,passwordMD5value
ctrlInfo:UpdatePassword,passwordMD5value


# 提示：
服务端与客户端的数据交互中，只有包含 ctrlInfo: 标识的内容会被解析为指令，其余所有数据都将直接转发给串口。
USB 串口设备的插入/拔出事件，会以 ctrlInfo:串口号异常关闭，结果代码 的格式通知所有客户端。

### 编译指南
环境依赖
构建工具：make、CMake、python

编译器：
Windows：MinGW (本人用的是 GCC 8.1.0，测试GCC 15.0.0 也可以)
Linux：GCC
Linux 依赖库：libudev-dev (用于设备信息获取与热插拔检测)

sudo apt-get install libudev-dev
子模块：项目依赖 uthash 和 mimIni，请确保在编译前拉取或放置在 third_party 目录下，需要手动拉取。

执行对应平台的 mk 脚本直接编译。
Linux环境还需要安装libudev开发包，实现PVID获取、设备插拔检测功能
sudo apt-get install libudev-dev

构建脚本 (使用的是 Python语言作为进行快速跨平台构建)
项目提供了跨平台统一的构建 mk.py，支持灵活的构建组合。
参数不区分先后顺序和大小写！

基础命令示例
./mk.py              # 默认调试构建（性能较好）
./mk.py debug        # 完整调试模式（含内存检测，仅 Linux）
./mk.py asan         # ASAN 内存检测模式（仅 Linux）
./mk.py release      # 发布版本（生产环境优化）

平台特定构建
./mk.py x86          # x86 平台本地编译
./mk.py arm          # ARM 平台交叉编译
./mk.py x86 release  # x86 平台发布版本

清理与管理
./mk.py rm           # 删除整个构建目录
./mk.py clean        # 清理构建产物（保留目录）
./mk.py cleanBuild   # 清理并重新构建（开发版）
灵活的参数组合

脚本支持不同参数的自由组合（平台、构建类型、清理命令），例如：
./mk.py x86 debug cleanBuild   # x86 平台，调试模式，清理并重建
./mk.py arm cleanBuild release # ARM 平台，发布模式，清理并重建
注意：Windows 环境下暂不支持交叉编译 ARM 版本。


## == 基础构建命令
./mk.py               # 默认调试构建 信号处理+符号解析（日常开发 性能较好）
./mk.py debug         # 完整调试（内存检测，比较吃性能）仅Linux支持，Win还是符号解析
./mk.py asan          # ASAN版本（同 debug 内存检测）
./mk.py release       # 发布版本（生产环境） 

## == 平台特定构建
./mk.py arm                     # ARM平台交叉编译
./mk.py x86                     # x86平台本地编译
./mk.py arm release             # ARM平台发布版本
./mk.py x86 release             # x86平台发布版本

## == 清理和管理命令
./mk.py rm                  # 删除构建目录
./mk.py clean               # 清理构建（保留目录）
./mk.py cleanBuild debug    # 清理并构建调试版本

## == 更多参数组合 
./mk.py arm clean debug           # ARM清理构建并构建完整调试版本
./mk.py x86 cleanBuild            # x86平台清理并构建开发版本
./mk.py arm cleanBuild debug      # ARM平台清理并构建调试版本
./mk.py x86 cleanBuild debug      # x86删除构建并构建调试版本
./mk.py x86 cleanBuild release    # x86平台清理并构建发布版本

## == 任意顺序组合，Win下面还不能实现构建ARM版本
./mk.py x86 debug cleanBuild      # 平台→类型→清理
./mk.py cleanBuild x86 debug      # 清理→平台→类型
./mk.py debug cleanBuild x86      # 类型→清理→平台
./mk.py x86 cleanBuild debug      # 平台→清理→类型

### 项目结构
src/ - 源代码目录
build/ - 预编译的可执行文件存放目录
测试工具/ - 包含网络调试助手等辅助工具
局域网搜索服务端/ - 服务端发现工具的使用说明和程序
third_party/ - 第三方依赖库 (uthash, mimIni)
mk.py - Python 构建脚本


已知限制与注意事项
串口独占：一个服务端进程同时只能打开一个串口。如需管理多个串口，请启动多个服务端实例。
安全警告：SystemCommands 指令存在系统命令执行风险，请务必修改默认密码并在安全网络环境下使用。

## 许可证
Copyright (c) 2026 wei_huan
本项目采用 [MIT License](LICENSE) 开源许可证，允许自由使用和商用。
> **免责声明**：本软件按“原样”提供，不提供任何形式的明示或默示担保。使用本软件所产生的任何风险由用户自行承担。

### 致谢
uthash - 哈希表实现
mimIni - INI 文件解析库

贡献与反馈
欢迎通过 Issues 提交 Bug 报告或功能建议。



### 茶水阅读 
## 向 DeepSeek 提出如下需求出现的初始版本。主要实现功能如下：
1. 服务端监听的串口号默认为9000，如果被占用自动加1，服务端使用一个宏来控制能接受多少个客户端连接。
2. 限制最大客户端连接数量，新的客户端连接后，踢掉最早连接的客户端，使用系统时间精确到ms的方法作为判断依据哪个是最早的，并告诉被踢下线的客户端他被踢下线了
3. 使用setsockopt函数 禁用Nagle算法。使用非阻塞式监听客户端连接，超时时间为1秒。
4. 用“crtlInfo:”字符串用于客户端控制服务器动作的头标识，结尾加“\n”，这些约定的内容用来作为控制信息，不发送给串口。
5. 客户端连接服务端后，服务端创建一个独立的线程接收客户端的数据，完成后向客户端发送“crtlInfo:OK! your indes x\n”
6. 获取Win系统下有效可用的串口列表，让客户端通过发送“crtlInfo:comliset”字符串后，服务端返回可用串口列表。
7. 比如串口列表里有 COM2和COM5，客户端通过发送 “crtlInfo:open,串口号,波特率,数据位,停止位,校验位\n” 来打开串口并设置相关参数，其中，串口号必填项，后面可以在不填入情况下，使用默认参数，默认波特率921600，数据位8位，停止位1位，无校验位。不管打开串口成功或失败，都将结果代码发送给客户端，格式为“crtlInfo: open [串口号,波特率,数据位,停止位,校验位] 结果 异常代码\n”
8. 可以的话用独立线程接收串口数据，接收到串口数据发给所有客户端（数据内容有不光有字符串，还有一般数据），任何客户端发来的数据直接发给串口。
9. 一个服务端只能打开一个串口。相应的，任何客户端也可以发送打开新的串口，但是要关闭之前打开的串口。
10. 串口可能出现热插拔或者异常关闭的问题，将这些信息发给所有客户端，格式为“crtlInfo:串口号异常关闭，结果代码\n”
11. 串口通信异步方式可能无法正常使用，暂且用同步方式。
12. 让程序支持在运行时通过传入参数，来修改指定端口号，比如输入 -p5000 就指定监听5000端口号，-p参数不区分大小写，规避前1024，返回参数为端口号。
如果该端口号被占用就自动加1，尝试10次。可能是启用IPv6和IPv4问题，第二次运行还是同一个端口号切不支持IPv6，只有第三次运行才会是新的端口号
13. USB设备插入或拔出通知所有客户端
最后给出使用MakeFile管理编译。
14. 串口异步发送能力。使用独立线程使用队列，主要任务是异步发送数据到串口。
15. 请让服务端实现被发现的能力，这个功能在子线程用UDP实现，使用19000端口
16. 获取串口列表增加PID和VID功能，可以用于识别产品
