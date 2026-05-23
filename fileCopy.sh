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
    local remote_host="$3"
    local remote_path="/home/cat/software/com2tcp_server"
    local temp_path="${remote_path}.tmp"
    local ssh_host="$remote_host"
    local ip_version=""  # 用于存储 -4 或 -6 选项

    if check_file "$password_file"; then
        log_info "通过SSH推送到远程主机..."
        
        # 判断IP版本
        if [[ "$remote_host" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            # IPv4地址
            ip_version="-4"
            log_info "检测到IPv4地址: $remote_host"
        elif [[ "$remote_host" =~ .*:.* ]]; then
            # IPv6地址
            ip_version="-6"
            ssh_host="[$remote_host]"
            log_info "检测到IPv6地址: $remote_host"
            log_info "SSH连接格式: $ssh_host"
        else
            # 域名，不指定版本
            ip_version=""
            log_info "检测到域名: $remote_host"
        fi
        
        log_info "正在连接并推送文件..."
        
        # 使用检测到的IP版本
        if sshpass -f "$password_file" scp $ip_version -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
              -o ConnectTimeout=5 "$source_file" "root@$ssh_host:$temp_path" 2>&1; then
            
            log_success "SCP推送成功"
            
            # SSH连接也使用相同的IP版本
            if sshpass -f "$password_file" ssh $ip_version -o StrictHostKeyChecking=no \
                -o UserKnownHostsFile=/dev/null "root@$remote_host" \
                "mv -f $temp_path $remote_path" 2>&1; then
                log_success "远程文件移动成功"
            else
                log_error "远程文件移动失败"
                return 1
            fi
        else
            log_error "SSH推送失败，请检查:"
            log_error "  1. 网络是否连通"
            log_error "  2. 目标主机是否运行SSH服务"
            log_error "  3. 密码文件是否正确"
            log_error "  4. 目标路径是否可写"
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
    ssh_copy "$source_file" "password.txt" "192.168.1.166"
    
    log_success "文件分发完成"
}

# 执行主函数
main "$@"
