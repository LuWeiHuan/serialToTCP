/******************************************************************************
  * @file    文件 hostConnect.c 
  * @author  作者 
  * @version 版本 V1.0
  * @date    日期 2025-08-17
  * @brief   简介 连接远端服务器，域名解析，获取本机IP
  ******************************************************************************
  * @attention 注意
  *
  *
  *******************************************************************************
*/

/*================== 头文件包含     =========================================*/
#include "hostConnect.h"
#include "log.h"
#include "commonUtils.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
// 必须在包含头文件之前定义 Windows 版本
#define _WIN32_WINNT 0x0600  // Windows Vista 或更高版本
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <fcntl.h>

#include <net/if.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#endif

/*================== 本地数据类型   =========================================*/
/*================== 本地宏定义     =========================================*/
/*================== 全局共享变量   =========================================*/
/*================== 本地常量声明   =========================================*/
/*================== 本地变量声明   =========================================*/
/*================== 本地函数声明   =========================================*/
static int8_t trueResolveHostDomainName(const char* hostname, char* ipBuffer, uint8_t ipBufferSize, 
                          int *retErr, bool preferIPv4);

/*================== 外部函数声明   =========================================*/
/*================== 外部变量声明   =========================================*/

/**
 * @brief 域名解析函数 - 自动IPv4/IPv6
 * @param hostname 域名或IP地址
 * @param ipBuffer 存储解析结果的缓冲区
 * @param ipBufferSize 缓冲区大小
 * @param retErr 具体错误值
 * @param preferIPv6 优先使用IPv6 (true) 还是 自动 (false)
 * @return   0 成功，会返回主机名对应的IP地址
 *          -1 域名解析失败
 *          -2 没有为主机名找到有效的IP地址
 */
int8_t resolveHostDomainName(const char* hostname, char* ipBuffer, uint8_t ipBufferSize, 
                          int *retErr, bool preferIPv6)
{
  if( hostname == NULL || ipBuffer == NULL || ipBufferSize <= 0 )
    return -1;

  int identifyRet = hostStringIdentify(hostname, false);
  if( identifyRet == 0)
    return -2;
  if( identifyRet == 1 || identifyRet == 2 ){
    memcpy(ipBuffer, hostname, strlen(hostname));
    return 0;
  }
  
  int8_t ret = trueResolveHostDomainName(hostname, ipBuffer, ipBufferSize, retErr, preferIPv6);
  if( ret == -1 ){
    SafePrintf("IPv%d Domain resolution failed: %s\n", preferIPv6? 6:4, gai_strerror(*retErr));
    ret = trueResolveHostDomainName(hostname, ipBuffer, ipBufferSize, retErr, !preferIPv6);
    if( ret == -1 )  
      SafePrintf("IPv%d Domain resolution failed: %s\n", preferIPv6? 4:6, gai_strerror(*retErr)); 
  }
  return ret;
}

/**
 * @brief 真域名解析函数 - 支持IPv4/IPv6
 * @param hostname 域名或IP地址
 * @param ipBuffer 存储解析结果的缓冲区
 * @param ipBufferSize 缓冲区大小
 * @param retErr 具体错误值
 * @param preferIPv4 优先使用IPv6 (true) 还是 IPv4 (false)
 * @return   0 成功
 *          -1 域名解析失败
 *          -2 没有为主机名找到有效的IP地址
 */
static int8_t trueResolveHostDomainName(const char* hostname, char* ipBuffer, uint8_t ipBufferSize, 
                          int *retErr, bool preferIPv6)
{
    struct addrinfo hints, *result = NULL, *ptr = NULL;
    char ipstr[INET6_ADDRSTRLEN] = {0};

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;     // 支持IPv4和IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    
    // 解析域名
    int ret = getaddrinfo(hostname, NULL, &hints, &result);
    if (ret != 0) {
        if (retErr) 
          *retErr = ret;
        return -1;
    }

    // 遍历结果
    for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
        void* addr = NULL;
        
        if (ptr->ai_family == AF_INET) {
            struct sockaddr_in* ipv4 = (struct sockaddr_in*)ptr->ai_addr;
            addr = &(ipv4->sin_addr);
        } else if (ptr->ai_family == AF_INET6) {
            struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)ptr->ai_addr;
            addr = &(ipv6->sin6_addr);
        } else {
            continue;
        }
        
        inet_ntop(ptr->ai_family, addr, ipstr, sizeof(ipstr));
        
        // 根据偏好选择地址
        if (!preferIPv6 && ptr->ai_family == AF_INET) {
            strncpy(ipBuffer, ipstr, ipBufferSize - 1);
            ipBuffer[ipBufferSize - 1] = '\0';
            freeaddrinfo(result);
            return 0;
        }
        if (preferIPv6 && ptr->ai_family == AF_INET6) {
            strncpy(ipBuffer, ipstr, ipBufferSize - 1);
            ipBuffer[ipBufferSize - 1] = '\0';
            freeaddrinfo(result);
            return 0;
        }
    }
    
    // 如果偏好地址未找到，使用第一个可用地址
    if (result != NULL) {
        void* addr = NULL;
        int family = result->ai_family;
        if (family == AF_INET) {
            struct sockaddr_in* ipv4 = (struct sockaddr_in*)result->ai_addr;
            addr = &(ipv4->sin_addr);
        } else if (family == AF_INET6) {
            struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)result->ai_addr;
            addr = &(ipv6->sin6_addr);
        }
        
        if (addr) {
            inet_ntop(family, addr, ipBuffer, ipBufferSize - 1);
            ipBuffer[ipBufferSize - 1] = '\0';
            freeaddrinfo(result);
            return 0;
        }
    }
    
    freeaddrinfo(result);
    if (retErr) *retErr = -2;
    return -2;
}

/**
 * @brief 开始以阻塞状态连接到服务器 - 支持IPv4/IPv6
 * @param host 域名或IP地址
 * @param port 端口号
 * @param timeoutMs 连接超时时间，单位 ms
 * @param retSocket 成功后这里会返回套接字
 * @param retIP 成功后这里会返回具体IP地址
 * @param retAddrFamily 返回地址族 (AF_INET/AF_INET6)
 * @return 成功返回真，失败返回假。
 */
bool startConnectToServer(const char* host, uint16_t port, uint16_t timeoutMs, 
                            socket_t *retSocket, char *retIP, int *retAddrFamily)
{
  char resolvedIP[INET6_ADDRSTRLEN] = {0};
  struct addrinfo hints, *result = NULL, *ptr = NULL;

  if (retSocket == NULL || retIP == NULL)
      return false;
  
  // 解析域名或IP地址
  int ret = resolveHostDomainName(host, resolvedIP, sizeof resolvedIP, NULL, false);
  if ( ret != 0 )
      return false;
  
  // 创建socket - 使用getaddrinfo获取地址信息
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  
  char portStr[8];
  snprintf(portStr, sizeof portStr, "%d", port);
  
  int addrInfoRet = getaddrinfo(resolvedIP, portStr, &hints, &result);
  if (addrInfoRet != 0) {
    SafePrintf("getaddrinfo failed: %s\n", gai_strerror(addrInfoRet));
    return false;
  }
  
  // 查找可用地址
  socket_t sock = INVALID_SOCKET_VALUE;
  for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
    sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if (sock == INVALID_SOCKET_VALUE)
        continue;
    
    // 设置非阻塞模式
#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket(sock, FIONBIO, &mode) != 0) 
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) 
#endif
    {
      closeSocket(sock);
      sock = INVALID_SOCKET_VALUE;
      continue;
    }
    
    // 连接
    int connectRet = connect(sock, ptr->ai_addr, (socklen_t)ptr->ai_addrlen);
    if (connectRet == SOCKET_ERROR) {
      int error = GetLastError();
#ifdef _WIN32
      if (error != WSAEWOULDBLOCK) 
#else
      if (error != EINPROGRESS) 
#endif
      {
        closeSocket(sock);
        sock = INVALID_SOCKET_VALUE;
        continue;
      }
    }
    
    // 处理非阻塞连接
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sock, &writefds);
    
    struct timeval timeout;
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    
    int selectResult = select(sock + 1, NULL, &writefds, NULL, &timeout);
    if (selectResult <= 0) {
      closeSocket(sock);
      sock = INVALID_SOCKET_VALUE;
      continue;
    }
    
    // 检查连接是否成功
    int error = 0;
    socklen_t errorLen = sizeof error;
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &errorLen) == SOCKET_ERROR || error != 0) {
      closeSocket(sock);
      sock = INVALID_SOCKET_VALUE;
      continue;
    }
    
    // 连接成功
    break;
  }
  
  freeaddrinfo(result);
  
  if (sock == INVALID_SOCKET_VALUE) {
      return false;
  }
  
  *retSocket = sock;
  strcpy(retIP, resolvedIP);
  if (retAddrFamily) {
      *retAddrFamily = sock;  // 实际上无法从socket获取family
  }
  
  return true;
}



#ifdef __linux__
// 通过ioctl获取指定IP对应的子网掩码
bool getSubnetMaskForIP(const char* ipStr, uint32_t* subnetMask)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return false;
    
    //struct ifreq ifr;
    //struct sockaddr_in* sin = (struct sockaddr_in*)&ifr.ifr_addr;
    
    // 获取所有网络接口
    struct ifconf ifc;
    char buf[1024];
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    
    if (ioctl(sock, SIOCGIFCONF, &ifc) < 0) {
        close(sock);
        return false;
    }
    
    int numInterfaces = ifc.ifc_len / sizeof(struct ifreq);
    struct ifreq* ifrArray = ifc.ifc_req;
    
    for (int i = 0; i < numInterfaces; i++) {
        struct ifreq* currentIfr = &ifrArray[i];
        
        // 获取接口IP地址
        if (ioctl(sock, SIOCGIFADDR, currentIfr) < 0)
            continue;
        
        struct sockaddr_in* addr = (struct sockaddr_in*)&currentIfr->ifr_addr;
        char currentIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, currentIP, sizeof(currentIP));
        
        // 检查是否匹配
        if (strcmp(ipStr, currentIP) == 0) {
            // 获取子网掩码
            if (ioctl(sock, SIOCGIFNETMASK, currentIfr) < 0) {
                close(sock);
                return false;
            }
            
            struct sockaddr_in* mask = (struct sockaddr_in*)&currentIfr->ifr_netmask;
            *subnetMask = ntohl(mask->sin_addr.s_addr);
            close(sock);
            return true;
        }
    }
    
    close(sock);
    return false;
}
#endif

#ifdef _WIN32
bool getSubnetMaskForIP(const char* ipStr, uint32_t* subnetMask)
{
    DWORD dwSize = 0;
    PMIB_IPADDRTABLE pIPAddrTable = NULL;
    
    // 获取所需缓冲区大小
    if (GetIpAddrTable(NULL, &dwSize, FALSE) == ERROR_INSUFFICIENT_BUFFER) {
        pIPAddrTable = (PMIB_IPADDRTABLE)malloc(dwSize);
        if (pIPAddrTable == NULL)
            return false;
    } else {
        return false;
    }
    
    // 获取IP地址表
    if (GetIpAddrTable(pIPAddrTable, &dwSize, FALSE) != NO_ERROR) {
        free(pIPAddrTable);
        return false;
    }
    
    uint32_t targetIP = inet_addr(ipStr);
    bool found = false;
    
    for (DWORD i = 0; i < pIPAddrTable->dwNumEntries; i++) {
        if (pIPAddrTable->table[i].dwAddr == targetIP) {
            *subnetMask = pIPAddrTable->table[i].dwMask;
            found = true;
            break;
        }
    }
    
    free(pIPAddrTable);
    return found;
}
#endif

// 获取与客户端相同网段的IP地址 - 支持IPv6
// 获取与客户端相同网段的IP地址 - 改进版
const char *GetMatchingSubnetIP(struct sockaddr_in* clientAddr)
{
    static char retMyIP[INET6_ADDRSTRLEN];
    memset(retMyIP, 0, sizeof(retMyIP));
    
    if (clientAddr == NULL) {
        strcpy(retMyIP, "127.0.0.1");
        return retMyIP;
    }
    
    uint8_t ipCount = 0;
    char localIPs[25][INET6_ADDRSTRLEN];
    memset(localIPs, 0, sizeof(localIPs));
    
    // 获取所有本地IP地址
    getAllLocalIPs(localIPs, &ipCount, 25, false);
    
    if (ipCount == 0) {
        strcpy(retMyIP, "127.0.0.1");
        return retMyIP;
    }
    
    // 如果只有一个IP，直接使用
    if (ipCount == 1) {
        strcpy(retMyIP, localIPs[0]);
        return retMyIP;
    }
    
    // 获取客户端IP地址字符串
    char clientIPStr[INET_ADDRSTRLEN];
#ifdef _WIN32
    const char* clientIP = inet_ntoa(clientAddr->sin_addr);
    if (clientIP == NULL) {
        strcpy(retMyIP, "127.0.0.1");
        return retMyIP;
    }
    strncpy(clientIPStr, clientIP, sizeof(clientIPStr) - 1);
#else
    if (inet_ntop(AF_INET, &clientAddr->sin_addr, clientIPStr, sizeof(clientIPStr)) == NULL) {
        strcpy(retMyIP, "127.0.0.1");
        return retMyIP;
    }
#endif
    clientIPStr[sizeof(clientIPStr) - 1] = '\0';
    
    // 获取客户端IP和子网掩码（尝试通过路由表获取） 
    uint32_t clientSubnetMask = 0;

    // 方法1：使用系统路由表获取与客户端IP匹配的子网掩码
    bool subnetFound = getSubnetMaskForIP(clientIPStr, &clientSubnetMask);

    if (subnetFound) {
        // 将客户端IP转换为网络字节序的uint32_t
        uint32_t clientIPNet = ntohl(clientAddr->sin_addr.s_addr);
        
        // 遍历本地IP，检查是否在同一子网
        for (int i = 0; i < ipCount; i++) {
            struct sockaddr_in localAddr;
            if (inet_pton(AF_INET, localIPs[i], &localAddr.sin_addr) != 1)
                continue;
            
            uint32_t localIPNet = ntohl(localAddr.sin_addr.s_addr);
            
            // 检查是否在同一子网（使用子网掩码）
            if ((clientIPNet & clientSubnetMask) == (localIPNet & clientSubnetMask)) {
                // 如果是回环地址则跳过
                if (strcmp(localIPs[i], "127.0.0.1") == 0)
                    continue;
                strcpy(retMyIP, localIPs[i]);
                return retMyIP;
            }
        }
    }
    
    // 方法2：如果路由表查询失败，使用简单的网段匹配（/24）
    // 提取客户端IP的前三段（网段）
    char clientSubnet[16] = {0};
    char* dot = strrchr(clientIPStr, '.');
    if (dot) {
        strncpy(clientSubnet, clientIPStr, dot - clientIPStr);
        
        // 寻找匹配网段的本地IP
        for (int i = 0; i < ipCount; i++) {
            // 跳过回环地址
            if (strcmp(localIPs[i], "127.0.0.1") == 0)
                continue;
                
            char localSubnet[16] = {0};
            dot = strrchr(localIPs[i], '.');
            if (dot == NULL) 
                continue;
            
            strncpy(localSubnet, localIPs[i], dot - localIPs[i]);
            if (strcmp(clientSubnet, localSubnet) == 0) {
                strcpy(retMyIP, localIPs[i]);
                return retMyIP;
            }
        }
    }
    
    // 方法3：尝试使用getsockname获取（原方法，作为备选）
    struct sockaddr_storage tempAddr;
    socklen_t tempAddrLen = sizeof(tempAddr);
    
    socket_t tempSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (tempSocket != INVALID_SOCKET_VALUE) {
        int retConnect = connect(tempSocket, (struct sockaddr*)clientAddr, sizeof(*clientAddr));
        if (retConnect != SOCKET_ERROR) {
            int ret = getsockname(tempSocket, (struct sockaddr*)&tempAddr, &tempAddrLen);
            closeSocket(tempSocket);
            
            if (ret == 0 && tempAddr.ss_family == AF_INET) {
                struct sockaddr_in* addr = (struct sockaddr_in*)&tempAddr;
                inet_ntop(AF_INET, &addr->sin_addr, retMyIP, sizeof(retMyIP));
                return retMyIP;
            }
        } else {
            closeSocket(tempSocket);
        }
    }
    
    // 所有方法都失败，返回第一个非回环IP
    for (int i = 0; i < ipCount; i++) {
        if (strcmp(localIPs[i], "127.0.0.1") != 0) {
            strcpy(retMyIP, localIPs[i]);
            return retMyIP;
        }
    }
    
    strcpy(retMyIP, "127.0.0.1");
    return retMyIP;
}

// 新增：IPv6 本机地址选择（简化版：取第一个非回环、非链路本地优先）
const char* GetMatchingSubnetIPv6(struct sockaddr_in6* clientAddr6)
{
  static char retMyIP[INET6_ADDRSTRLEN];
  memset(retMyIP, 0, sizeof retMyIP);

  // 方法：用临时 UDP socket 连接客户端地址，再 getsockname 获取本端地址
  socket_t tempSocket = socket(AF_INET6, SOCK_DGRAM, 0);
  if (tempSocket != INVALID_SOCKET_VALUE) {
    struct sockaddr_storage tempAddr;
    socklen_t tempAddrLen = sizeof(tempAddr);

    if (connect(tempSocket, (struct sockaddr*)clientAddr6,
                sizeof(*clientAddr6)) != SOCKET_ERROR) {
      if (getsockname(tempSocket, (struct sockaddr*)&tempAddr,
                      &tempAddrLen) == 0 &&
          tempAddr.ss_family == AF_INET6) {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&tempAddr;
        inet_ntop(AF_INET6, &addr6->sin6_addr, retMyIP, sizeof retMyIP);
        closeSocket(tempSocket);
        return retMyIP;
      }
    }
    closeSocket(tempSocket);
  }

  strcpy(retMyIP, "::1");
  return retMyIP;
}

// 选择与客户端相同网段的IP
const char *SelectMatchingSubnetIP(const char *clientAddr)
{ 
  uint8_t ipCount = 0;
  char localIPs[25][INET6_ADDRSTRLEN];
  static char retMyIP[20];
  memset(localIPs, 0, sizeof localIPs);
  memset(retMyIP, 0, sizeof retMyIP);

  getAllLocalIPs(localIPs, &ipCount, 25, false);
  
  if (ipCount == 0) 
      return "127.0.0.1" ;  

  // 如果只有一个IP，直接使用
  if (ipCount == 1) {
      strcpy(retMyIP, localIPs[0]);
      return retMyIP;
  }
  
  // 获取客户端IP的网段
  char clientIP[16];
  strcpy(clientIP, clientAddr);
  
  // 提取客户端IP的前三段（网段）
  char clientSubnet[16] = {0};
  char* dot = strrchr(clientIP, '.');
  if (dot) 
    strncpy(clientSubnet, clientIP, dot - clientIP);
  
  // 寻找匹配网段的本地IP
  for (int i = 0; i < ipCount; i++) {
      char localSubnet[16] = {0};
      dot = strrchr(localIPs[i], '.');
      if (dot == 0) 
        continue;

      strncpy(localSubnet, localIPs[i], dot - localIPs[i]);
      if (strcmp(clientSubnet, localSubnet) == 0) {
          strcpy(retMyIP, localIPs[i]);
          return retMyIP;
      }
  }
  
  // 如果没有找到匹配网段的IP，使用第一个非回环IP
  strcpy(retMyIP, localIPs[0]);

  return retMyIP;
}

bool getSockfdPeerInfo(int sockfd, char *retIPstr, uint16_t *retPort) 
{
  struct sockaddr_in peer_addr;
  socklen_t addr_len = sizeof(peer_addr);
  int ret = getpeername(sockfd, (struct sockaddr*)&peer_addr, &addr_len);
 
  if( ret == 0 && retIPstr)
    inet_ntop(AF_INET, &peer_addr.sin_addr, retIPstr, INET6_ADDRSTRLEN);
  if( ret == 0 && retPort)
    *retPort = ntohs(peer_addr.sin_port);
  return ret == 0? true:false;
}




// 判断是否为IPv4地址
static bool isIPv4(const char *str) {
  struct sockaddr_in sa;
  int result = inet_pton(AF_INET, str, &(sa.sin_addr));
  return result == 1;
}

// 判断是否为IPv6地址
static bool isIPv6(const char *str) {
  struct sockaddr_in6 sa6;
  int result = inet_pton(AF_INET6, str, &(sa6.sin6_addr));
  return result == 1;
}

// 判断是否为域名
static bool isDomain(const char *str) {
  int len = strlen(str);
  if (len == 0 || len > 253) 
    return false;
  
  // 域名不能以点开头或结尾
  if (str[0] == '.' || str[len-1] == '.') 
    return false;
  
  int label_len = 0;
  bool has_alpha = false;
  
  for (int i = 0; i < len; i++) {
    char c = str[i];
    
    if (c == '.') {
      // 检查标签长度
      if (label_len == 0 || label_len > 63) 
        return false;
      label_len = 0;
      continue;
    }
    
    // 域名允许的字符：字母、数字、连字符
    if (!isalnum(c) && c != '-') 
        return false;
    
    
    // 检查是否有字母（顶级域名至少有一个字母）
    if (isalpha(c)) has_alpha = true;
    
    label_len++;
    
    // 检查标签长度
    if (label_len > 63) 
      return false;
  }
  
  // 最后一段（顶级域名）不能全为数字，且长度至少为2
  if (label_len < 2) return false;
  if (!has_alpha) return false;
  
  return true;
}

// 主识别函数
int8_t hostStringIdentify(const char *str, bool print) {
  if (str == NULL) 
    return 0;
  if (print)
    SafePrintf("主机: %-20s 识别结果：", str);
  
  if (isIPv4(str)) {
    if (print)
      SafePrintf("IPv4地址\n");
    return 1;
  } 
  else if (isIPv6(str)) {
    if (print)
      SafePrintf("IPv6地址\n");
    return 2;
  }
  else if (isDomain(str)) {
    if (print)
      SafePrintf("域名\n");
    return 3;
  }
  else {
    if (print)
      SafePrintf("未知格式（既不是域名也不是IP地址）\n");
    return 0;
  } 
}
