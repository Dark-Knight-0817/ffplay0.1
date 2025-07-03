#ifndef RTSP_INPUT_H
#define RTSP_INPUT_H

#include "input_source.h"
#include <thread>
#include <atomic>
#include <mutex>

namespace media {

/**
 * @brief RTSP输入源实现
 * 
 * 专门处理RTSP协议的网络流输入
 */
class RTSPInput : public IInputSource {
public:
    RTSPInput();
    ~RTSPInput() override;
    
    // 实现IInputSource接口
    bool open(const std::string& url) override;
    void close() override;
    AVFormatContext* getFormatContext() override;
    InputSourceInfo getSourceInfo() const override;
    InputSourceState getState() const override;
    bool isSeekable() const override;
    bool seek(int64_t timestamp) override;
    void setStateCallback(StateCallback callback) override;
    std::string getLastError() const override;
    
    // RTSP特有配置
    void setTransportProtocol(const std::string& protocol); // "tcp" or "udp"
    void setConnectionTimeout(int timeout_ms);
    void setBufferSize(int size);
    void setUserAgent(const std::string& user_agent);
    void setCredentials(const std::string& username, const std::string& password);

private:
    // FFmpeg上下文
    AVFormatContext* format_ctx_ = nullptr;
    
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
    
    // 连接管理
    std::atomic<bool> connection_lost_{false};
    std::thread monitor_thread_;
    std::atomic<bool> should_stop_monitor_{false};
    
    // 内部方法
    bool setupRTSPOptions(AVDictionary** options);
    void changeState(InputSourceState new_state, const std::string& message = "");
    void startConnectionMonitor();
    void stopConnectionMonitor();
    void monitorConnection();
    bool testConnection();
    std::string formatRTSPUrl(const std::string& base_url);
};

} // namespace media

#endif // RTSP_INPUT_H
