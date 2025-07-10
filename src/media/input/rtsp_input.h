#ifndef RTSP_INPUT_H
#define RTSP_INPUT_H

#include "input_source.h"
#include <thread>
#include <atomic>
#include <mutex>


namespace media
{

    // 前向声明要在 media 命名空间内
    class NetworkDetector;
    struct UrlInfo;
    struct NetworkTestResult;

    /**
     * @brief RTSP输入源实现
     *
     * 专门处理RTSP协议的网络流输入
     */
    class RTSPInput : public IInputSource
    {
    public:
        RTSPInput();
        ~RTSPInput() override;

        // 实现IInputSource接口
        bool open(const std::string &url) override;
        void close() override;
        AVFormatContext *getFormatContext() override;
        InputSourceInfo getSourceInfo() const override;
        InputSourceState getState() const override;
        bool isSeekable() const override;
        bool seek(int64_t timestamp) override;
        void setStateCallback(StateCallback callback) override;
        std::string getLastError() const override;

        // RTSP特有配置
        void setTransportProtocol(const std::string &protocol); // "tcp" or "udp"
        void setConnectionTimeout(int timeout_ms);
        void setBufferSize(int size);
        void setUserAgent(const std::string &user_agent);
        void setCredentials(const std::string &username, const std::string &password);

        // 网络检测和诊断功能
        /**
         * @brief 获取详细的网络诊断信息
         * @return 包含连通性、响应时间等信息的字符串
         */
        std::string getNetworkDiagnosticInfo() const;

        /**
         * @brief 手动触发网络连通性测试
         * @return 测试是否成功
         */
        bool manualNetworkTest();

        /**
         * @brief 获取最后接收数据包的时间
         * @return 时间点，用于判断连接活跃度
         */
        std::chrono::steady_clock::time_point getLastPacketTime() const
        {
            return last_packet_time_;
        }

        /**
         * @brief 检查连接是否健康
         * @return 连接状态是否正常
         */
        bool isConnectionHealthy() const;

    private:
        // FFmpeg上下文
        AVFormatContext *format_ctx_ = nullptr;

        // 配置参数
        std::string transport_protocol_ = "tcp";
        int connection_timeout_ms_ = 10000;
        int buffer_size_ = 1048576; // 1MB
        std::string user_agent_ = "FFplay0.1";
        std::string username_;
        std::string password_;

        // 状态管理
        mutable std::mutex state_mutex_;
        InputSourceState state_ = InputSourceState::Closed;
        std::string last_error_;
        StateCallback state_callback_;

        // 连接管理和监控
        std::atomic<bool> connection_lost_{false};
        std::thread monitor_thread_;
        std::atomic<bool> should_stop_monitor_{false};
        std::chrono::steady_clock::time_point last_packet_time_;

        // 网络检测器（使用智能指针避免头文件依赖）
        std::unique_ptr<NetworkDetector> network_detector_;

        // 内部方法 - FFmpeg相关
        bool setupRTSPOptions(AVDictionary **options);
        std::string formatRTSPUrl(const std::string &base_url);

        // 内部方法 - 状态管理
        void changeState(InputSourceState new_state, const std::string &message = "");

        // 内部方法 - 连接监控
        void startConnectionMonitor();
        void stopConnectionMonitor();
        void monitorConnection();

        // 内部方法 - 连接测试（多个层次）
        bool testConnection();          // 原始简单测试（保持兼容性）
        bool testConnectionEnhanced();  // 增强的测试方法
        bool quickReadTest();           // 快速数据读取测试
        bool performNetworkDiagnosis(); // 网络层诊断

        // 内部方法 - 网络预检查
        bool preCheckNetworkConnectivity(const std::string &url);
    };

    /**
     * @brief RTSP输入源工厂类
     *
     * 提供便捷的创建方法和预配置选项
     */
    class RTSPInputFactory
    {
    public:
        /**
         * @brief 创建标准RTSP输入源实例
         * @param url RTSP URL（可选，也可以后续通过open传入）
         * @return 配置好的输入源实例
         */
        static std::unique_ptr<RTSPInput> create(const std::string &url = "")
        {
            auto input = std::make_unique<RTSPInput>();

            // 设置推荐的默认配置
            input->setTransportProtocol("tcp"); // TCP更可靠
            input->setConnectionTimeout(10000); // 10秒超时
            input->setBufferSize(1048576);      // 1MB缓冲区
            input->setUserAgent("RTSPClient/1.0");

            return input;
        }

        /**
         * @brief 创建带认证的RTSP输入源
         * @param url RTSP URL
         * @param username 用户名
         * @param password 密码
         * @return 配置好认证信息的输入源实例
         */
        static std::unique_ptr<RTSPInput> createWithAuth(const std::string &url,
                                                         const std::string &username,
                                                         const std::string &password)
        {
            auto input = create(url);
            input->setCredentials(username, password);
            return input;
        }

        /**
         * @brief 创建低延迟优化的RTSP输入源
         * @param url RTSP URL
         * @return 为低延迟优化的输入源实例
         */
        static std::unique_ptr<RTSPInput> createLowLatency(const std::string &url = "")
        {
            auto input = create(url);

            // 低延迟优化配置
            input->setTransportProtocol("tcp");
            input->setConnectionTimeout(3000); // 3秒快速超时
            input->setBufferSize(65536);       // 64KB小缓冲区

            return input;
        }

        /**
         * @brief 创建高可靠性的RTSP输入源
         * @param url RTSP URL
         * @return 为稳定性优化的输入源实例
         */
        static std::unique_ptr<RTSPInput> createHighReliability(const std::string &url = "")
        {
            auto input = create(url);

            // 高可靠性配置
            input->setTransportProtocol("tcp");
            input->setConnectionTimeout(15000); // 15秒充足超时
            input->setBufferSize(2097152);      // 2MB大缓冲区

            return input;
        }
    };

} // namespace media

#endif // RTSP_INPUT_H
