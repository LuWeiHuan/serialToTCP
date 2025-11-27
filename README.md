## 快速使用方法：
1. 选择一台x86架构运行WinPC运行 com2tcp_server.exe 或 x86架构 LinuxPC 运行 com2tcp_server 
2. 进入到"测试工具"文件夹运行 NetAssist.exe 网络调试助手，点击 快捷指令
3. 快捷指令里的 获取串口列表和打开串口是常用功能，其它的可以慢慢摸索

## 提示：
一个服务端只能打开一个串口，要想打开多个串口可以运行多个服务端
客户端发送的数据里没有 “ctrlInfo:” 的任何内容都直接发给串口
搜索局域网内的服务端请直接进入 【局域网搜索服务端】文件夹，里面有完成的使用方法

AI 平台：DeepSeek
运行系统：Win_x86 Linux_x86
编译工具：MinGW GCC
工程管理：Cmake
Linux引用库：libudev-dev
引用外部开源代码：uthash

实现将串口数据转到TCP收发的能力
环境Win平台，使用C语言编写一个服务端程序，接受任何网段连接该服务器

## 主要实现功能如下：
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


## 控制服务器动作指令介绍：  所有指令不区分大小写
获取系统内可用的串口列表
ctrlInfo:comlist      只有COM口号
ctrlInfo:comlistID    有COM口号的同时增加PID、VID和REV等信息，可用于识别是不是之家的产品
ctrlInfo:comlistVPID  和 comlistID 命令一样

打开串口并使用默认参数。默认波特率921600，数据位8位，停止位1位，无校验位
ctrlInfo:open,COM4              打开串口其他参数默认
ctrlInfo:open,COM4,115200       打开串口并设置波特率，其他默认
ctrlInfo:open,COM1,9600,8,1,0   打开串口并设置全部参数


## 剩下的是一些扩展命令：可根据自己情况需要去去使用

让服务端退出仅此而已。服务器如果运行在 10000 端口的话，会拒绝这个指令会，用于复活服务端
ctrlInfo:exit

让服务端运行再运行一个新的服务端并不指定端口号，端口号被占用会递增加1
ctrlInfo:runNewServer                不指定端口号
ctrlInfo:runNewServer,-p 10000       指定端口号


单纯的让客户端执行一条操作系统 CMD 命令
ctrlInfo:SystemCommands,cls           “cls”是执行内容
ctrlInfo:ExecuteSystemCommands,cls    “cls”是执行内容


串口独占
ctrlInfo:setCOMdata,Send,my    只有发这个指令的客户端能：给串口发数据
ctrlInfo:setCOMdata,Send,all   所有客户端都能：          给串口发数据
ctrlInfo:setCOMdata,Recv,my    只有发这个指令的客户端能：接收串口数据
ctrlInfo:setCOMdata,Recv,all   所有客户端能：            接收串口数据


设置客户端发给串口的数据，是否用异步队列缓存起来后，再发给串口
ctrlInfo:setCOMasyncSend,0
ctrlInfo:setCOMasyncSend,100    其中 "100" 是队列数量，失败会有回复限定数量

设置接受到串口的数据，是否用异步队列缓存起来后，逐一发给所有客户端
ctrlInfo:setCOMasyncRecv,0
ctrlInfo:setCOMasyncRecv,100    其中 "100" 是队列数量，失败会有回复限定数量


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




## 编译
在 openSrc 文件夹引用了开源 uthash 库进行哈希值计算，可能需要您手动拉取一下。
确保系统已安装make、Cmake工具，Win系统还要安装MinGW
执行对应平台的 mk 脚本直接编译。
Linux环境还需要安装libudev开发包，实现PVID获取、设备插拔检测功能
sudo apt-get install libudev-dev



## ============== 基础构建命令
./mk.sh               # 默认调试构建 信号处理+符号解析（日常开发 性能较好）
./mk.sh debug         # 完整调试（内存检测，比较吃性能）仅Linux支持，Win还是符号解析
./mk.sh asan          # ASAN版本（同 debug 内存检测）
./mk.sh release       # 发布版本（生产环境） 

## ============== 平台特定构建
./mk.sh arm                     # ARM平台交叉编译
./mk.sh x86                     # x86平台本地编译
./mk.sh arm release             # ARM平台发布版本
./mk.sh x86 release             # x86平台发布版本

## ============== 清理和管理命令
./mk.sh rm                  # 删除构建目录
./mk.sh clean               # 清理构建（保留目录）
./mk.sh cleanBuild debug    # 清理并构建调试版本

## ============== 更多参数组合 
./mk.sh arm clean debug           # ARM清理构建并构建完整调试版本
./mk.sh x86 cleanBuild            # x86平台清理并构建开发版本
./mk.sh arm cleanBuild debug      # ARM平台清理并构建调试版本
./mk.sh x86 cleanBuild debug      # x86删除构建并构建调试版本
./mk.sh x86 cleanBuild release    # x86平台清理并构建发布版本

## ============== 任意顺序组合，Win下面还不能实现构建ARM版本
./mk.sh x86 debug cleanBuild      # 平台→类型→清理
./mk.sh cleanBuild x86 debug      # 清理→平台→类型
./mk.sh debug cleanBuild x86      # 类型→清理→平台
./mk.sh x86 cleanBuild debug      # 平台→清理→类型

 
