#!/usr/bin/env python3
# mk.py - 跨平台构建脚本
# 独立 UDP 测试脚本：发送线程 + 接收线程分离

import socket
import threading
import sys
import os

# ==================== 配置 ====================
IP = "255.255.255.255"
PORT = 19000
MESSAGE = "discover_com2tcp_server"   # 先测这条；可改成 ctrlInfo:exit
INTERVAL_S = 1.0                       # 每隔多少秒发一次
RECV_BUFSIZE = 4096
# =============================================


# 用于让接收线程能感知到"是否收到过任何包"
recv_count = 0
recv_lock = threading.Lock()


def recv_thread(sock: socket.socket, stop_event: threading.Event) -> None:
    """独立接收线程：一直等回复，收到就打印"""
    global recv_count
    print("[接收线程] 启动，等待回复...")
    sock.settimeout(0.5)  # 用短超时轮询，方便响应 stop_event

    while not stop_event.is_set():
        try:
            data, addr = sock.recvfrom(RECV_BUFSIZE)
            text = data.decode("ascii", errors="replace")
            with recv_lock:
                recv_count += 1
                n = recv_count
            print(f"[接收线程] #{n:<3} 来自 {addr[0]:<15}:{addr[1]}  内容: {text!r}")
        except socket.timeout:
            continue
        except OSError as e:
            if not stop_event.is_set():
                print(f"[接收线程] 错误: {e}")
            break

    print("[接收线程] 退出")

def clear_screen():
    print("\033[2J\033[H", end="")
def send_thread(sock: socket.socket, stop_event: threading.Event) -> None:
    """独立发送线程：每隔 INTERVAL_S 秒发一次"""
    print("[发送线程] 启动")
    data = MESSAGE.encode("ascii")
    seq = 0

    while not stop_event.is_set():
        try:
            seq += 1
            sent = sock.sendto(data, (IP, PORT))
            print(f"\n[发送线程] #{seq:<3} 向-> {IP}:{PORT} 发送 {sent} 字节，内容: {MESSAGE!r}")
        except OSError as e:
            print(f"[发送线程] 发送错误: {e}")

        # 可中断的 sleep
        stop_event.wait(INTERVAL_S)

    print("[发送线程] 退出")

def set_console_size_windows(cols=120, lines=40):
    if sys.platform.startswith("win"):
        os.system(f"mode con cols={cols} lines={lines}")
def main() -> int:
    set_console_size_windows(150, 40)
    print(f"=== UDP 测试开始 ===")
    print(f"目标: {IP}:{PORT}")
    print(f"消息: {MESSAGE!r}")
    print(f"间隔: {INTERVAL_S}s")
    print("按 Ctrl+C 结束\n")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    # 绑定到任意本地端口，方便接收回复
    sock.bind(("0.0.0.0", 0))
    local = sock.getsockname()
    print(f"[主线程] 本地绑定: {local[0]}:{local[1]}\n")

    stop_event = threading.Event()

    t_recv = threading.Thread(target=recv_thread, args=(sock, stop_event), daemon=True)
    t_send = threading.Thread(target=send_thread, args=(sock, stop_event), daemon=True)

    t_recv.start()
    t_send.start()

    try:
        # 主线程等待，直到 Ctrl+C
        while t_send.is_alive():
            t_send.join(timeout=0.5)
    except KeyboardInterrupt:
        print("\n[主线程] 收到 Ctrl+C，正在停止...")

    stop_event.set()
    t_send.join(timeout=2)
    t_recv.join(timeout=2)
    sock.close()

    with recv_lock:
        total = recv_count
    print(f"\n=== 测试结束，共收到 {total} 个回复包 ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())