#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import re
import subprocess
import sys
import platform
import socket
import shutil
import time
from pathlib import Path
from typing import List

# 配置参数
IP = "192.168.1.10"
PORT = 19000
#MESSAGE = "discover_com2tcp_server"
MESSAGE = "ctrlInfo:exit"
TIMEOUT_MS = 100
# SSHFS 给根目录下的盘符文件夹加的私有区字符
PUA = '\uf03a'

# 目标复制路径
WIN_DEST_PATHS = [
    rf"R:\D{PUA}\器械部共享盘\软件包\远程工具\远程串口",
    rf"R:\D{PUA}\共享\软件包\远程工具\远程串口",
    # r"\\wh-share\D盘\远程串口",
]

LINUX_DEST_PATHS = [
    "/mnt/NFS",
    "/mnt/tftp"
]

# 颜色定义
CYAN = '\033[0;36m'
GREEN = '\033[0;32m'
RED = '\033[0;31m'
YELLOW = '\033[1;33m'
NC = '\033[0m'  # No Color


# 日志函数
def log_info(msg):
    print(f"{CYAN}[INFO]{NC} {msg}")


def log_success(msg):
    print(f"{GREEN}[SUCCESS]{NC} {msg}")


def log_warning(msg):
    print(f"{YELLOW}[WARNING]{NC} {msg}")


def log_error(msg):
    print(f"{RED}[ERROR]{NC} {msg}")


# 检查文件是否存在
def check_file(path):
    if not os.path.isfile(path):
        log_error(f"文件不存在: {path}")
        return False
    return True


# 检查目录是否存在
def check_directory(path):
    if not os.path.isdir(path):
        log_error(f"目录不存在: {path}")
        return False
    return True

# SSH复制函数
def ssh_copy(source_file, remote_path_dir, password_file, remote_host, remote_port=None):
    remote_path = f"{remote_path_dir}/{source_file}"
    ssh_host = remote_host
    ip_version = ""

    # 设置默认端口
    if not remote_port:
        remote_port = "22"

    if not check_file(password_file):
        log_warning("密码文件不存在，跳过SSH推送")
        return False
    
    # 判断IP版本
    ipv4_pattern = re.compile(r'^(\d{1,3}\.){3}\d{1,3}$')
    if ipv4_pattern.match(remote_host):
        ip_version = "-4"
        log_info(f"检测到IPv4地址: {remote_host}")
    elif ':' in remote_host:
        ip_version = "-6"
        ssh_host = f"[{remote_host}]"
        log_info(f"检测到IPv6地址: {remote_host}")
    else:
        ip_version = ""
        log_info(f"检测到可能是域名: {remote_host}")

    log_info(f"开始连接并推送文件 {remote_path} 远端主机 {remote_host} 端口 {remote_port} ...")

    # 构建 scp 命令
    scp_cmd = [
        "sshpass", "-f", password_file,
        "scp"
    ]
    if ip_version:
        scp_cmd.append(ip_version)
    scp_cmd += [
        "-P", remote_port,
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=/dev/null",
        "-o", "ConnectTimeout=2",
        source_file,
        f"root@{ssh_host}:{remote_path}.temp"
    ]

    try:
        result = subprocess.run(scp_cmd, capture_output=True, text=True, check=True)
        log_success("SCP推送成功")

        # 构建 ssh 命令
        ssh_cmd = [
            "sshpass", "-f", password_file,
            "ssh"
        ]
        if ip_version:
            ssh_cmd.append(ip_version)
        ssh_cmd += [
            "-p", remote_port,
            "-o", "StrictHostKeyChecking=no",
            "-o", "UserKnownHostsFile=/dev/null",
            f"root@{remote_host}",
            f"mv -f {remote_path}.temp {remote_path}"
        ]

        result = subprocess.run(ssh_cmd, capture_output=True, text=True, check=True)
        log_success("远程文件移动成功")
        return True
    except subprocess.CalledProcessError as e:
        log_error("SSH推送失败，请检查:")
        log_error(" 1. 网络是否连通")
        log_error(" 2. 目标主机是否运行SSH服务")
        log_error(" 3. 密码文件是否正确")
        log_error(" 4. 目标路径是否可写")
        log_error(f" 5. SSH端口是否正确 (当前: {remote_port})")
        if e.stdout:
            print(e.stdout)
        if e.stderr:
            print(e.stderr, file=sys.stderr)
        return False




def send_udp(ip: str, port: int, message: str, timeout_ms: int) -> None:
  
    timeout_s = timeout_ms / 1000.0
    udp_client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_client.settimeout(timeout_s)
    result = False
    try:
        data = message.encode("ascii")
        bytes_sent = udp_client.sendto(data, (ip, port))
        log_success(f"向 {ip}:{port} 成功发送 {bytes_sent} 字节数据，等待{timeout_ms}ms回复，发送内容: {message}")

        try:
            received_data, remote_endpoint = udp_client.recvfrom(4096)
            received_message = received_data.decode("ascii", errors="replace")
            log_success(f"收到回复来自 {remote_endpoint[0]} : {remote_endpoint[1]}")
            log_success(f"回复内容: {received_message}")
            result = True
        except socket.timeout:
            log_info(f"等待{timeout_ms}ms超时，未收到回复")
            result = False

    except OSError as e:
        log_error(f"错误: {e}")
        result = False
    finally:
        udp_client.close()
    return result

def copy_file(src: str, dest, use_shutil: bool = None) -> bool:
    src_path = Path(src)
    if not src_path.exists():
        log_error(f"源文件不存在: {src_path}")
        return False

    # 自动判断模式：未显式指定时，Windows 用 shutil，Linux 用 cp
    if use_shutil is None:
        use_shutil = platform.system() == "Windows"

    # 统一成列表处理
    dest_list = dest if isinstance(dest, (list, tuple)) else [dest]

    all_ok = True
    for d in dest_list:
        if use_shutil:
            # Windows: 先尝试 copy2（保留元数据），失败则降级为 copy
            try:
                shutil.copy2(src_path, d)
                log_success(f"复制到: {d} 完成！")
            except OSError as e:
                log_warning(f"shutil.copy2 复制到 {d} 失败: {e}，尝试降级为 shutil.copy...")
                try:
                    shutil.copy(src_path, d)
                    log_success(f"复制到: {d} 完成！（降级模式）")
                except OSError as e2:
                    log_error(f"复制到: {d} 失败: {e2}")
                    all_ok = False
        else:
            # Linux: 复制到目标目录，需先检查目录存在
            if not check_directory(d):
                all_ok = False
                continue
            dest_path = os.path.join(d, os.path.basename(src))
            log_info(f"复制到 {d}...")
            try:
                subprocess.run(["cp", src, dest_path], check=True)
                log_success(f"成功复制到 {d}")
            except subprocess.CalledProcessError:
                log_error(f"复制到 {d} 失败")
                all_ok = False

    return all_ok

# 主函数
def main():
    source_file = "com2tcp_server"
    log_info( f"当前为 {platform.system()} 系统，开始文件分发过程...")

    # 检查源文件
    if not check_file(source_file):
        sys.exit(1)

    # 本地复制
    # 环境检测：仅 Linux 执行本地复制
    if platform.system() == "Linux":
        copy_file(source_file, LINUX_DEST_PATHS)
    else:
        ret = send_udp(IP, PORT, MESSAGE, TIMEOUT_MS)
        if ret: 
          time.sleep(0.3)
        copy_file(rf".\{source_file}.exe", WIN_DEST_PATHS)

    # SSH复制 - 可指定端口，默认22
    # 使用方法: ssh_copy(源文件, 远端路径, 密码文件, 主机地址, 端口号)
    if platform.system() == "Linux":
        ssh_copy(source_file, "/home/cat/software", "password.txt", "192.168.1.40", "51022")
        ssh_copy(source_file, "/home/cat/software", "password.txt", "192.168.1.148", "51022")
        #ssh_copy(source_file, "~", "password.txt", "192.168.1.20", "51022")
        # 如果需要使用其他端口，例如:
        #ssh_copy(source_file, "/home/cat/software", "password.txt", "240e:3bb:a90:fd10:5cd8:9e61:7957:17", "51022")
        # ssh_copy(source_file, "/home/cat/software", "password.txt", "240e:3bb:a90:e620:5cd8:9e61:7957:7", "51022")
        # ssh_copy(source_file, "/home/cat/software", "password.txt", "192.168.1.100", "2222")
    log_success("文件分发完成")

if __name__ == "__main__":
    main()
