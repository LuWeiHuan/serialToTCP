import socket
import time
import netifaces
import ipaddress

def get_local_networks():
    """获取本机的所有网络接口和对应的网段"""
    networks = []
    
    # 获取所有网络接口
    interfaces = netifaces.interfaces()
    
    for interface in interfaces:
        try:
            # 获取接口的地址信息
            addrs = netifaces.ifaddresses(interface)
            if netifaces.AF_INET in addrs:
                for addr_info in addrs[netifaces.AF_INET]:
                    if 'addr' in addr_info and 'netmask' in addr_info:
                        ip = addr_info['addr']
                        netmask = addr_info['netmask']
                        
                        # 跳过回环地址和无效地址
                        if ip == '127.0.0.1' or ip.startswith('169.254.'):
                            continue
                            
                        # 计算网络地址
                        network = ipaddress.IPv4Network(f"{ip}/{netmask}", strict=False)
                        networks.append({
                            'interface': interface,
                            'ip': ip,
                            'netmask': netmask,
                            'network': network,
                            'broadcast': str(network.broadcast_address)
                        })
        except Exception as e:
            print(f"获取接口 {interface} 信息时出错: {e}")
    
    return networks

def discover_servers():
    print("正在搜索COM2TCP服务器...")
    
    # 获取本机网络信息
    networks = get_local_networks()
    
    # 创建UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(2.0)  # 2秒超时
    
    servers = []
    discovery_msg = "DISCOVER_COM2TCP_SERVER"
    
    try:
        # 首先发送到全局广播地址 255.255.255.255
        print("发送广播请求到 255.255.255.255:19000 (全局广播)")
        try:
            sock.sendto(discovery_msg.encode(), ('255.255.255.255', 19000))
        except Exception as e:
            print(f"发送到 255.255.255.255 失败: {e}")
        
        # 对每个网络接口发送广播
        if networks:
            print(f"找到 {len(networks)} 个网络接口:")
            for net in networks:
                broadcast_addr = net['broadcast']
                print(f"发送广播请求到 {broadcast_addr}:19000 (接口: {net['interface']})")
                
                try:
                    sock.sendto(discovery_msg.encode(), (broadcast_addr, 19000))
                except Exception as e:
                    print(f"发送到 {broadcast_addr} 失败: {e}")
                    continue
        else:
            print("❌ 未找到有效的网络接口，仅使用全局广播")
        
        print("已发送所有发现请求")
        
        # 接收响应
        start_time = time.time()
        unique_servers = set()  # 用于去重的集合
        
        while time.time() - start_time < 5.0:  # 接收5秒
            try:
                data, addr = sock.recvfrom(256)
                response = data.decode().strip()
                
                if response.startswith("COM2TCP_Server"):
                    parts = response.split('|')
                    if len(parts) >= 6:
                        server_ip = parts[2]
                        server_port = int(parts[3])
                        
                        # 创建服务器唯一标识 (IP:端口)
                        server_id = f"{server_ip}:{server_port}"
                        
                        # 检查是否已存在
                        if server_id not in unique_servers:
                            unique_servers.add(server_id)
                            
                            server_info = {
                                'ip': server_ip,
                                'port': server_port,
                                'clients': int(parts[4]),
                                'max_clients': int(parts[5]),
                                'response': response  # 保存原始响应
                            }
                            servers.append(server_info)
                            print(f"发现新服务器: {server_ip}:{server_port}")
                        else:
                            print(f"收到重复服务器响应: {server_ip}:{server_port}")
                            
            except socket.timeout:
                # 正常的超时，不显示错误
                continue
            except ConnectionResetError as e:
                # Windows特有的连接重置错误，忽略它
                if "[WinError 10054]" in str(e):
                    continue
                else:
                    print(f"接收错误: {e}")
            except Exception as e:
                print(f"接收错误: {e}")
                continue
        
        return servers
        
    finally:
        sock.close()

def print_servers(servers):
    if not servers:
        print("\n❌ 未找到任何COM2TCP服务器")
        return
    
    print(f"\n✅ 找到 {len(servers)} 个服务器 (已去重):")
    print("=" * 60)
    
    for i, server in enumerate(servers, 1):
        print(f"服务器 #{i}:")
        print(f"  IP地址:     {server['ip']}")
        print(f"  端口:       {server['port']}")
        print(f"  客户端数:   {server['clients']}/{server['max_clients']}")
        print(f"  原始响应:   {server['response']}")
        print("-" * 40)

def create_connection_command(servers):
    if not servers:
        return
    
    print("\n📋 连接命令示例:")
    for i, server in enumerate(servers, 1):
        print(f"{i}. telnet {server['ip']} {server['port']}")
        print(f"   nc {server['ip']} {server['port']}")
        print(f"   或者使用网络调试助手连接 TCP {server['ip']}:{server['port']}")

# 主程序
if __name__ == "__main__":
    print("COM2TCP服务器搜索工具 (改进版)")
    print("=" * 40)
    
    try:
        servers = discover_servers()
        print_servers(servers)
        create_connection_command(servers)
    except Exception as e:
        print(f"程序执行出错: {e}")
    
    # 无论结果如何，延迟5秒退出
    print("\n程序将在5秒后退出...")
    time.sleep(5)