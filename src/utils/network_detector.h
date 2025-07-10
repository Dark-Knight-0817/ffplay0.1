// network_detector.h
#ifndef NETWORK_DETECTOR_H
#define NETWORK_DETECTOR_H

#include <string>
#include <chrono>
#include <memory>
#include <atomic>
#include <future>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <errno.h>
#endif

namespace media {

/**
 * @brief 网络检测结果
 */
struct NetworkTestResult {
    bool success = false;
    int response_time_ms = -1;
    std::string error_message;
    std::string method_used;  // "socket", "ping", "http"
};

/**
 * @brief URL信息解析结果
 */
struct UrlInfo {
    std::string protocol;     // rtsp, http, etc.
    std::string hostname;     // IP或域名
    int port = -1;           // 端口号
    std::string path;        // 路径
    bool is_valid = false;
};

/**
 * @brief 网络连接检测器
 * 
 * 提供多种网络连接检测方法，从底层socket到应用层ping
 */
class NetworkDetector {
public:
    NetworkDetector();
    ~NetworkDetector();

    /**
     * @brief 解析URL获取连接信息
     * @param url 完整URL (如: rtsp://192.168.1.100:554/stream)
     * @return URL解析结果
     */
    static UrlInfo parseUrl(const std::string& url);

    /**
     * @brief 快速socket连接测试
     * @param hostname 主机名或IP
     * @param port 端口号
     * @param timeout_ms 超时时间(毫秒)
     * @return 测试结果
     */
    NetworkTestResult testSocketConnection(const std::string& hostname, 
                                         int port, 
                                         int timeout_ms = 3000);

    /**
     * @brief ICMP ping测试
     * @param hostname 主机名或IP
     * @param timeout_ms 超时时间(毫秒)
     * @return 测试结果
     */
    NetworkTestResult testPing(const std::string& hostname, 
                              int timeout_ms = 3000);

    /**
     * @brief HTTP连接测试
     * @param url HTTP URL
     * @param timeout_ms 超时时间(毫秒)
     * @return 测试结果
     */
    NetworkTestResult testHttpConnection(const std::string& url, 
                                        int timeout_ms = 3000);

    /**
     * @brief 综合网络测试
     * @param url 目标URL
     * @param timeout_ms 每个测试的超时时间
     * @return 最佳测试结果
     */
    NetworkTestResult comprehensiveTest(const std::string& url, 
                                       int timeout_ms = 3000);

    /**
     * @brief 异步测试（不阻塞）
     * @param url 目标URL
     * @param timeout_ms 超时时间
     * @return future对象，可获取异步结果
     */
    std::future<NetworkTestResult> testAsync(const std::string& url, 
                                            int timeout_ms = 3000);

private:
    // 网络初始化状态
    bool network_initialized_ = false;
    
    // 初始化网络库（Windows需要）
    bool initializeNetwork();
    void cleanupNetwork();

    // 底层socket操作
    int createSocket(int family, int type, int protocol);
    bool setSocketTimeout(int socket_fd, int timeout_ms);
    bool setSocketNonBlocking(int socket_fd);
    void closeSocket(int socket_fd);

    // 地址解析
    bool resolveHostname(const std::string& hostname, struct sockaddr_in& addr);
    
    // 系统ping命令调用
    NetworkTestResult systemPing(const std::string& hostname, int timeout_ms);
    
    // 原始ICMP ping实现
    NetworkTestResult rawIcmpPing(const std::string& hostname, int timeout_ms);
};

/**
 * @brief 网络工具类 - 提供便捷的静态方法
 */
class NetworkUtils {
public:
    /**
     * @brief 快速检测URL连通性
     * @param url 目标URL
     * @return 是否连通
     */
    static bool isUrlReachable(const std::string& url);

    /**
     * @brief 检测主机端口是否开放
     * @param hostname 主机名
     * @param port 端口
     * @return 是否开放
     */
    static bool isPortOpen(const std::string& hostname, int port);

    /**
     * @brief 获取本机网络状态
     * @return 网络是否可用
     */
    static bool isNetworkAvailable();

    /**
     * @brief 获取网络延迟
     * @param hostname 目标主机
     * @return 延迟时间(ms)，-1表示失败
     */
    static int getNetworkLatency(const std::string& hostname);
};

} // namespace media

#endif // NETWORK_DETECTOR_H