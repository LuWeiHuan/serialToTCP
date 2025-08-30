import socket
import time

def discover_servers():
    print("正在搜索COM2TCP服务器...")
    print("发送广播请求到255.255.255.255:8888")
    
    # 创建UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(3.0)  # 3秒超时
    
    try:
        # 发送发现请求
        discovery_msg = "DISCOVER_COM2TCP_SERVER"
        sock.sendto(discovery_msg.encode(), ('255.255.255.255', 19000))
        print("已发送发现请求")
        
        # 接收响应
        servers = []
        start_time = time.time()
        
        while time.time() - start_time < 3.0:  # 接收3秒
            try:
                data, addr = sock.recvfrom(256)
                response = data.decode().strip()
                print(f"收到响应: {response}")
                
                if response.startswith("COMTCP_SERVER_V1.0"):
                    parts = response.split('|')
                    if len(parts) >= 6:
                        server_info = {
                            'ip': parts[1],
                            'port': int(parts[2]),
                            'name': parts[3],
                            'clients': int(parts[4]),
                            'max_clients': int(parts[5]),
                            'response': response  # 保存原始响应
                        }
                        servers.append(server_info)
                        
            except socket.timeout:
                break
            except Exception as e:
                print(f"接收错误: {e}")
                break
        
        return servers
        
    finally:
        sock.close()

def print_servers(servers):
    if not servers:
        print("\n❌ 未找到任何COM2TCP服务器")
        return
    
    print(f"\n✅ 找到 {len(servers)} 个服务器:")
    print("=" * 60)
    
    for i, server in enumerate(servers, 1):
        print(f"服务器 #{i}:")
        print(f"  IP地址:     {server['ip']}")
        print(f"  端口:       {server['port']}")
        print(f"  名称:       {server['name']}")
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
    time.sleep(3)

# 主程序
if __name__ == "__main__":
    print("COM2TCP服务器搜索工具")
    print("=" * 40)
    
    servers = discover_servers()
    print_servers(servers)
    create_connection_command(servers)