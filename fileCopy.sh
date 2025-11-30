#!/bin/bash

# 颜色定义
CYAN='\033[0;36m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 日志函数
log_info() {
    echo -e "${CYAN}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查文件是否存在
check_file() {
    if [ ! -f "$1" ]; then
        log_error "文件不存在: $1"
        return 1
    fi
    return 0
}

# 检查目录是否存在
check_directory() {
    if [ ! -d "$1" ]; then
        log_error "目录不存在: $1"
        return 1
    fi
    return 0
}

# 本地复制函数
local_copy() {
    local source_file="$1"
    local dest_dir="$2"
    local dest_path="$dest_dir/com2tcp_server"
    
    if check_directory "$dest_dir"; then
        log_info "复制到 $dest_dir..."
        if cp "$source_file" "$dest_path"; then
            log_success "成功复制到 $dest_dir"
        else
            log_error "复制到 $dest_dir 失败"
            return 1
        fi
    else
        return 1
    fi
}

# SSH复制函数
ssh_copy() {
    local source_file="$1"
    local password_file="$2"
    local remote_host="192.168.1.40"
    local remote_path="/home/cat/software/com2TCP/com2tcp_server.new"
    
    if check_file "$password_file"; then
        log_info "通过SSH推送到远程主机..."
        
        # 先检查网络连通性
        if ping -c 1 -W 2 "$remote_host" &> /dev/null; then
            # 使用更安全的SSH选项
            if sshpass -f "$password_file" scp -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 \
                "$source_file" "root@$remote_host:$remote_path"; then
                log_success "SSH推送成功"
            else
                log_error "SSH推送失败"
                return 1
            fi
        else
            log_error "无法连接到远程主机 $remote_host"
            return 1
        fi
    else
        log_warning "密码文件不存在，跳过SSH推送"
        return 1
    fi
}

# 主函数
main() {
    local source_file="com2tcp_server"
    
    log_info "开始文件分发过程..."
    
    # 检查源文件
    if ! check_file "$source_file"; then
        exit 1
    fi
    
    # 本地复制
    local_copy "$source_file" "/mnt/NFS"
    local_copy "$source_file" "/mnt/tftp"
    
    # SSH复制
    ssh_copy "$source_file" "password.txt"
    
    log_success "文件分发完成"
}

# 执行主函数
main "$@"