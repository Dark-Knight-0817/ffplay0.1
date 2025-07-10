// network_detector.cpp
#include "network_detector.h"
#include <iostream>
#include <sstream>
#include <regex>
#include <thread>

#ifdef _WIN32
    #include <iphlpapi.h>
    #pragma comment(lib, "iphlpapi.lib")
#else
    #include <sys/select.h>
    #include <fcntl.h>
#endif

namespace media {

NetworkDetector::NetworkDetector() {
    network_initialized_ = initializeNetwork();
}

NetworkDetector::~NetworkDetector() {
    cleanupNetwork();
}

bool NetworkDetector::initializeNetwork() {
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    return result == 0;
#else
    return true;  // Unix系统不需要初始化
#endif
}

void NetworkDetector::cleanupNetwork() {
#ifdef _WIN32
    if (network_initialized_) {
        WSACleanup();
    }
#endif
}

UrlInfo NetworkDetector::parseUrl(const std::string& url) {
    UrlInfo info;
    
    // 正则表达式解析URL: protocol://hostname:port/path
    std::regex url_regex(R"(^(\w+)://([^:/]+)(?::(\d+))?(/.*)?$)");
    std::smatch match;
    
    if (std::regex_match(url, match, url_regex)) {
        info.protocol = match[1].str();
        info.hostname = match[2].str();
        
        if (match[3].matched) {
            info.port = std::stoi(match[3].str());
        } else {
            // 默认端口
            if (info.protocol == "rtsp") info.port = 554;
            else if (info.protocol == "http") info.port = 80;
            else if (info.protocol == "https") info.port = 443;
            else if (info.protocol == "ftp") info.port = 21;
        }
        
        info.path = match[4].matched ? match[4].str() : "/";
        info.is_valid = true;
    }
    
    return info;
}

NetworkTestResult NetworkDetector::testSocketConnection(const std::string& hostname, 
                                                       int port, 
                                                       int timeout_ms) {
    NetworkTestResult result;
    result.method_used = "socket";
    
    if (!network_initialized_) {
        result.error_message = "网络库未初始化";
        return result;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 创建socket
    int sock = createSocket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        result.error_message = "创建socket失败";
        return result;
    }
    
    // 设置非阻塞模式
    if (!setSocketNonBlocking(sock)) {
        closeSocket(sock);
        result.error_message = "设置非阻塞模式失败";
        return result;
    }
    
    // 解析主机名
    struct sockaddr_in server_addr;
    if (!resolveHostname(hostname, server_addr)) {
        closeSocket(sock);
        result.error_message = "解析主机名失败: " + hostname;
        return result;
    }
    server_addr.sin_port = htons(port);
    
    // 尝试连接
    int connect_result = connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    if (connect_result == 0) {
        // 立即连接成功
        result.success = true;
    } else {
#ifdef _WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
#else
        if (errno == EINPROGRESS) {
#endif
            // 连接正在进行，使用select等待
            fd_set write_set;
            FD_ZERO(&write_set);
            FD_SET(sock, &write_set);
            
            struct timeval timeout;
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_usec = (timeout_ms % 1000) * 1000;
            
            int select_result = select(sock + 1, nullptr, &write_set, nullptr, &timeout);
            
            if (select_result > 0 && FD_ISSET(sock, &write_set)) {
                // 检查连接是否真的成功
                int error = 0;
                socklen_t len = sizeof(error);
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &len) == 0 && error == 0) {
                    result.success = true;
                } else {
                    result.error_message = "连接失败，错误码: " + std::to_string(error);
                }
            } else if (select_result == 0) {
                result.error_message = "连接超时";
            } else {
                result.error_message = "select调用失败";
            }
        } else {
            result.error_message = "连接立即失败";
        }
    }
    
    closeSocket(sock);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    result.response_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    return result;
}

NetworkTestResult NetworkDetector::testPing(const std::string& hostname, int timeout_ms) {
    // 优先尝试系统ping命令（兼容性更好）
    NetworkTestResult result = systemPing(hostname, timeout_ms);
    
    if (!result.success) {
        // 如果系统ping失败，尝试原始ICMP（需要管理员权限）
        result = rawIcmpPing(hostname, timeout_ms);
    }
    
    return result;
}

NetworkTestResult NetworkDetector::systemPing(const std::string& hostname, int timeout_ms) {
    NetworkTestResult result;
    result.method_used = "ping";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::string cmd;
#ifdef _WIN32
    cmd = "ping -n 1 -w " + std::to_string(timeout_ms) + " " + hostname + " >nul 2>&1";
#else
    int timeout_sec = std::max(1, timeout_ms / 1000);
    cmd = "ping -c 1 -W " + std::to_string(timeout_sec) + " " + hostname + " >/dev/null 2>&1";
#endif

    int exit_code = system(cmd.c_str());
    
    auto end_time = std::chrono::high_resolution_clock::now();
    result.response_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    result.success = (exit_code == 0);
    if (!result.success) {
        result.error_message = "ping命令执行失败，退出码: " + std::to_string(exit_code);
    }
    
    return result;
}

NetworkTestResult NetworkDetector::rawIcmpPing(const std::string& hostname, int timeout_ms) {
    NetworkTestResult result;
    result.method_used = "icmp";
    result.error_message = "原始ICMP ping需要管理员权限，建议使用socket测试";
    return result;
}

NetworkTestResult NetworkDetector::testHttpConnection(const std::string& url, int timeout_ms) {
    NetworkTestResult result;
    result.method_used = "http";
    
    UrlInfo info = parseUrl(url);
    if (!info.is_valid) {
        result.error_message = "无效的URL格式";
        return result;
    }
    
    // 对于HTTP，先测试socket连接
    result = testSocketConnection(info.hostname, info.port, timeout_ms);
    if (result.success) {
        result.method_used = "http_socket";
    }
    
    return result;
}

NetworkTestResult NetworkDetector::comprehensiveTest(const std::string& url, int timeout_ms) {
    UrlInfo info = parseUrl(url);
    if (!info.is_valid) {
        NetworkTestResult result;
        result.error_message = "无效的URL格式";
        return result;
    }
    
    // 1. 首先尝试socket连接（最准确）
    NetworkTestResult socket_result = testSocketConnection(info.hostname, info.port, timeout_ms);
    if (socket_result.success) {
        return socket_result;
    }
    
    // 2. 如果socket失败，尝试ping（诊断网络层）
    NetworkTestResult ping_result = testPing(info.hostname, timeout_ms);
    if (ping_result.success) {
        // 网络通但端口不通
        NetworkTestResult result;
        result.method_used = "comprehensive";
        result.success = false;
        result.response_time_ms = ping_result.response_time_ms;
        result.error_message = "网络连通但服务端口不可达";
        return result;
    }
    
    // 3. 都失败了，返回socket的详细错误
    socket_result.method_used = "comprehensive";
    socket_result.error_message = "网络和服务都不可达: " + socket_result.error_message;
    return socket_result;
}

std::future<NetworkTestResult> NetworkDetector::testAsync(const std::string& url, int timeout_ms) {
    return std::async(std::launch::async, [this, url, timeout_ms]() {
        return comprehensiveTest(url, timeout_ms);
    });
}

int NetworkDetector::createSocket(int family, int type, int protocol) {
#ifdef _WIN32
    SOCKET sock = socket(family, type, protocol);
    return (sock == INVALID_SOCKET) ? -1 : (int)sock;
#else
    return socket(family, type, protocol);
#endif
}

bool NetworkDetector::setSocketNonBlocking(int socket_fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socket_fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(socket_fd, F_GETFL, 0);
    return fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void NetworkDetector::closeSocket(int socket_fd) {
#ifdef _WIN32
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
}

bool NetworkDetector::resolveHostname(const std::string& hostname, struct sockaddr_in& addr) {
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    
    // 尝试直接解析IP地址
    if (inet_pton(AF_INET, hostname.c_str(), &addr.sin_addr) == 1) {
        return true;
    }
    
    // 域名解析
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    int status = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
    if (status != 0) {
        return false;
    }
    
    struct sockaddr_in* addr_in = (struct sockaddr_in*)result->ai_addr;
    addr.sin_addr = addr_in->sin_addr;
    
    freeaddrinfo(result);
    return true;
}

// NetworkUtils 静态方法实现
bool NetworkUtils::isUrlReachable(const std::string& url) {
    NetworkDetector detector;
    NetworkTestResult result = detector.comprehensiveTest(url, 3000);
    return result.success;
}

bool NetworkUtils::isPortOpen(const std::string& hostname, int port) {
    NetworkDetector detector;
    NetworkTestResult result = detector.testSocketConnection(hostname, port, 3000);
    return result.success;
}

bool NetworkUtils::isNetworkAvailable() {
    // 测试几个知名的公共DNS服务器
    std::vector<std::string> test_hosts = {
        "8.8.8.8",      // Google DNS
        "1.1.1.1",      // Cloudflare DNS
        "114.114.114.114" // 114 DNS
    };
    
    NetworkDetector detector;
    for (const auto& host : test_hosts) {
        NetworkTestResult result = detector.testSocketConnection(host, 53, 2000);
        if (result.success) {
            return true;
        }
    }
    
    return false;
}

int NetworkUtils::getNetworkLatency(const std::string& hostname) {
    NetworkDetector detector;
    NetworkTestResult result = detector.testPing(hostname, 3000);
    return result.success ? result.response_time_ms : -1;
}

} // namespace media