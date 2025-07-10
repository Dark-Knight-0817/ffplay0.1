#include "rtsp_input.h"
#include "utils/network_detector.h"  // 添加这个头文件
#include <iostream>
#include <sstream>
#include <future>              // 添加用于 std::async

extern "C"
{
#include <libavutil/opt.h>
#include <libavutil/error.h>
}

namespace media
{
    RTSPInput::RTSPInput()
    {
        // 确保网络模块已初始化
        avformat_network_init();
        last_packet_time_ = std::chrono::steady_clock::now();  // 初始化时间戳

        // 在构造函数体中初始化（此时有完整定义）
        network_detector_ = std::make_unique<NetworkDetector>();
    }

    RTSPInput::~RTSPInput()
    {
        // 先关闭视频流
        close();
        avformat_network_deinit();
    }

    bool RTSPInput::open(const std::string &url)
    {
        std::cout << "RTSPInput::open() 开始，URL: " << url << std::endl;
        
        // 预检查网络连通性
        if (!preCheckNetworkConnectivity(url)) {
            std::cout << "RTSPInput::open() 失败：网络预检查未通过" << std::endl;
            return false;
        }
        
        // 检查状态（避免在锁内调用 changeState）
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (state_ != InputSourceState::Closed)
            {
                last_error_ = "输入源已经打开或正在打开";
                std::cout << "RTSPInput::open() 失败：状态错误" << std::endl;
                return false;
            }
        } // 锁在这里释放

        std::cout << "RTSPInput::open() 调用 changeState..." << std::endl;
        changeState(InputSourceState::Opening, "正在连接RTSP流...");

        // 分配格式上下文
        format_ctx_ = avformat_alloc_context();
        if (!format_ctx_)
        {
            std::string error_msg = "无法分配 AVFormatContext";
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                last_error_ = error_msg;
            }
            changeState(InputSourceState::Error, error_msg);
            return false;
        }

        // 设置 RTSP 选项（优化超时）
        AVDictionary *options = nullptr;
        if (!setupRTSPOptions(&options))
        {
            av_dict_free(&options);
            avformat_free_context(format_ctx_);
            format_ctx_ = nullptr;
            std::string error_msg = last_error_;  // 保存错误信息
            changeState(InputSourceState::Error, error_msg);
            return false;
        }

        // 格式化URL
        std::string final_url = formatRTSPUrl(url);
        std::cout << "RTSPInput::open() 尝试连接: " << final_url << std::endl;

        // 打开输入
        int ret = avformat_open_input(&format_ctx_, final_url.c_str(), nullptr, &options);
        av_dict_free(&options);
        
        std::cout << "RTSPInput::open() 连接结果: " << ret << std::endl;
        
        if (ret < 0)
        {
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            std::string error_msg = "无法打开RTSP流: " + std::string(error_buf);

            avformat_free_context(format_ctx_);
            format_ctx_ = nullptr;
            
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                last_error_ = error_msg;
            }
            changeState(InputSourceState::Error, error_msg);
            std::cout << "RTSPInput::open() 失败: " << error_msg << std::endl;
            return false;
        }

        // 启动连接监控
        startConnectionMonitor();

        changeState(InputSourceState::Opened, "RTSP流连接成功");
        std::cout << "RTSPInput::open() 成功" << std::endl;
        return true;
    }

    void RTSPInput::close()
    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (state_ == InputSourceState::Closed)
        {
            return;
        }

        // 停止监控线程
        stopConnectionMonitor();

        // 关闭 FFmpeg 上下文
        if (format_ctx_)
        {
            avformat_close_input(&format_ctx_);
        }

        changeState(InputSourceState::Closed, "RTSP流已关闭");
    }

    AVFormatContext *RTSPInput::getFormatContext()
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return format_ctx_;
    }

    InputSourceInfo RTSPInput::getSourceInfo() const
    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        InputSourceInfo info;
        info.type = InputSourceType::RTSP;

        if (!format_ctx_)
        {
            return info;
        }

        info.url = format_ctx_->url ? format_ctx_->url : "";
        info.duration = format_ctx_->duration;
        info.bit_rate = format_ctx_->bit_rate;
        info.is_seekable = false;
        info.format_name = format_ctx_->iformat ? format_ctx_->iformat->name : "";
        info.connection_timeout = connection_timeout_ms_;
        info.transport_protocol = transport_protocol_;

        return info;
    }

    InputSourceState RTSPInput::getState() const
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return state_;
    }

    bool RTSPInput::isSeekable() const
    {
        // RTSP实时流通常不支持seek
        return false;
    }

    bool RTSPInput::seek(int64_t timestamp)
    {
        // RTSP实时流不支持seek操作
        last_error_ = "RTSP实时流不支持seek操作";
        return false;
    }

    void RTSPInput::setStateCallback(StateCallback callback)
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_callback_ = callback;
    }

    std::string RTSPInput::getLastError() const
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return last_error_;
    }

    // RTSP特有配置方法
    void RTSPInput::setTransportProtocol(const std::string &protocol)
    {
        if (protocol == "tcp" || protocol == "udp")
        {
            transport_protocol_ = protocol;
        }
    }

    void RTSPInput::setConnectionTimeout(int timeout_ms)
    {
        connection_timeout_ms_ = timeout_ms;
    }

    void RTSPInput::setBufferSize(int size)
    {
        buffer_size_ = size;
    }

    void RTSPInput::setUserAgent(const std::string &user_agent)
    {
        user_agent_ = user_agent;
    }

    void RTSPInput::setCredentials(const std::string &username, const std::string &password)
    {
        username_ = username;
        password_ = password;
    }

    bool RTSPInput::setupRTSPOptions(AVDictionary **options)
    {
        if (!options) return false;
        
        // 核心超时设置（最重要）
        av_dict_set(options, "timeout", "3000000", 0);        // 3秒总超时
        av_dict_set(options, "stimeout", "3000000", 0);       // 3秒socket超时
        av_dict_set(options, "rw_timeout", "3000000", 0);     // 3秒读写超时
        
        // 连接设置
        av_dict_set(options, "rtsp_transport", "tcp", 0);     // 强制TCP（更可靠）
        av_dict_set(options, "rtsp_flags", "prefer_tcp", 0);  // 优先TCP
        av_dict_set(options, "buffer_size", "1048576", 0);    // 1MB缓冲区,网络接收缓冲区大小,减少网络抖动的影响
        
        // 性能优化
        av_dict_set(options, "analyzeduration", "500000", 0); // 0.5秒分析,减少可以加快连接速度，但可能获取不完整的流信息
        av_dict_set(options, "probesize", "500000", 0);       // 500KB探测,较小值可以减少延迟但可能影响稳定性
        av_dict_set(options, "max_delay", "500000", 0);       // 最大延迟0.5秒,较小值可以减少延迟但可能影响稳定性
        
        // 错误恢复
        av_dict_set(options, "reconnect", "1", 0);            // 允许重连
        av_dict_set(options, "reconnect_at_eof", "1", 0);     // EOF时重连
        av_dict_set(options, "reconnect_streamed", "1", 0);   // 流断开时重连
        av_dict_set(options, "reconnect_delay_max", "2", 0);  // 最大重连延迟2秒
        
        return true;
    }

    bool RTSPInput::preCheckNetworkConnectivity(const std::string& url) {
        std::cout << "RTSPInput: 开始网络连通性预检查..." << std::endl;
        
        // 解析URL
        UrlInfo url_info = NetworkDetector::parseUrl(url);
        if (!url_info.is_valid) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            last_error_ = "无效的RTSP URL格式";
            changeState(InputSourceState::Error, last_error_);
            return false;
        }

        // 快速socket连接测试
        NetworkTestResult socket_result = network_detector_->testSocketConnection(
            url_info.hostname, url_info.port, 3000);
        
        if (socket_result.success) {
            std::cout << "RTSPInput: Socket连接测试成功 (" 
                      << socket_result.response_time_ms << "ms)" << std::endl;
            return true;
        }

        std::cout << "RTSPInput: Socket连接失败，尝试ping测试..." << std::endl;
        
        // 如果socket失败，尝试ping测试
        NetworkTestResult ping_result = network_detector_->testPing(url_info.hostname, 3000);
        
        if (ping_result.success) {
            std::cout << "RTSPInput: Ping测试成功但端口不通 (" 
                      << ping_result.response_time_ms << "ms)" << std::endl;
            std::lock_guard<std::mutex> lock(state_mutex_);
            last_error_ = "网络连通但RTSP服务不可达 (端口" + std::to_string(url_info.port) + "不通)";
            changeState(InputSourceState::Error, last_error_);
            return false;
        }

        std::cout << "RTSPInput: 网络连通性测试完全失败" << std::endl;
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "网络不可达: " + socket_result.error_message;
        changeState(InputSourceState::Error, last_error_);
        return false;
    }

    void RTSPInput::monitorConnection(){
        while(!should_stop_monitor_.load()){
            std::this_thread::sleep_for(std::chrono::seconds(5));   // 心跳 5s 一次
            if(should_stop_monitor_.load()){    // 双重检查停止标志（优化）
                break;
            }
            
            // 使用增强的连接检测，而不是简单的 testConnection()
            if(!testConnectionEnhanced()){
                std::lock_guard<std::mutex> lock(state_mutex_);
                if(state_ == InputSourceState::Opened || state_ == InputSourceState::Reading){
                    connection_lost_.store(true);
                    changeState(InputSourceState::Disconnected, "RTSP连接丢失");
                }
                break;
            }
        }
    }

    bool RTSPInput::testConnection(){
        std::lock_guard<std::mutex> lock(state_mutex_);
        if(!format_ctx_ || state_ != InputSourceState::Opened){
            return false;            
        }

        // 基础FFmpeg状态检查
        if (!format_ctx_->pb || format_ctx_->pb->error != 0) {
            return false;
        }

        // 这里可以实现更复杂的连接检测逻辑
        // 例如发送RTSP PING 请求或尝试读取数据
        return true;
    }

    bool RTSPInput::testConnectionEnhanced() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        
        if (!format_ctx_ || state_ != InputSourceState::Opened) {
            return false;
        }
        
        // 1. 基础FFmpeg状态检查
        if (!format_ctx_->pb || format_ctx_->pb->error != 0) {
            return false;
        }
        
        // 2. 检查最后收到数据的时间
        auto now = std::chrono::steady_clock::now();
        auto time_since_last_packet = now - last_packet_time_;
        
        if (time_since_last_packet > std::chrono::seconds(30)) {
            // 30秒没有数据包，尝试读取验证
            if (!quickReadTest()) {
                // 读取失败，进行网络层诊断
                return performNetworkDiagnosis();
            }
        }
        
        return true;
    }

    bool RTSPInput::quickReadTest() {
        if (!format_ctx_) {
            return false;
        }
        
        AVPacket* packet = av_packet_alloc();  // 新的方式 - 返回指针
        if (!packet) {
            return false;
        }
        
        // 设置非阻塞模式
        int old_flags = format_ctx_->flags;
        format_ctx_->flags |= AVFMT_FLAG_NONBLOCK;
        
        int ret = av_read_frame(format_ctx_, packet);
        
        // 恢复原始标志
        format_ctx_->flags = old_flags;
        
        if (ret >= 0) {
            av_packet_free(&packet);
            last_packet_time_ = std::chrono::steady_clock::now();
            return true;
        }
        
        av_packet_free(&packet);  // 释放时需要传递地址
        return ret == AVERROR(EAGAIN);
    }

    bool RTSPInput::performNetworkDiagnosis() {
        if (!format_ctx_ || !format_ctx_->url) {
            return false;
        }
        
        std::string url = format_ctx_->url;
        
        // 异步进行网络诊断，避免阻塞监控线程
        auto future = std::async(std::launch::async, [this, url]() {
            UrlInfo url_info = NetworkDetector::parseUrl(url);
            if (!url_info.is_valid) {
                return;
            }
            
            // 测试socket连接
            NetworkTestResult socket_result = network_detector_->testSocketConnection(
                url_info.hostname, url_info.port, 2000);
            
            if (socket_result.success) {
                std::cout << "RTSPInput: 网络诊断 - Socket连接正常，可能是应用层问题" << std::endl;
            } else {
                // 测试ping
                NetworkTestResult ping_result = network_detector_->testPing(url_info.hostname, 2000);
                if (ping_result.success) {
                    std::cout << "RTSPInput: 网络诊断 - 网络层正常，RTSP端口不通" << std::endl;
                } else {
                    std::cout << "RTSPInput: 网络诊断 - 网络层断开" << std::endl;
                }
            }
        });

        // 可以选择等待或不等待
        // future.wait();  // 如果需要等待完成
        
        return false;  // 诊断期间认为连接有问题
    }

    // 新增方法：检查连接健康状态
    bool RTSPInput::isConnectionHealthy() const {
        auto now = std::chrono::steady_clock::now();
        auto silence = now - last_packet_time_;
        return silence < std::chrono::seconds(30);  // 30秒内有数据认为健康
    }

    void RTSPInput::changeState(InputSourceState new_state, const std::string &message){
        if(state_ != new_state){
            state_ = new_state;

            if(!message.empty()){
                std::cout << "RTSP Input: "<< message << std::endl;
            }

            if(state_callback_){
                // 在回调中不要持有锁,可能导致死锁
                auto callback = state_callback_;
                state_mutex_.unlock();
                callback(new_state, message);
                state_mutex_.lock();
            }
        }
    }

    void RTSPInput::startConnectionMonitor(){
        should_stop_monitor_.store(false);
        monitor_thread_ = std::thread(      // 创建新线程
            [this]()
            {
                monitorConnection();        // 在新线程中运行连接监控
            });
    }

    void RTSPInput::stopConnectionMonitor(){
        should_stop_monitor_.store(true);
        if(monitor_thread_.joinable()){
            monitor_thread_.join();
        }
    }

    std::string RTSPInput::formatRTSPUrl(const std::string& base_url){
        if(username_.empty() && password_.empty()){
            return base_url;
        }

        // 在 URL 中嵌入认证信息
        // rtsp://username:password@hostname:port/path
        size_t protocol_pos = base_url.find("://");
        if(protocol_pos == std::string::npos){
            return base_url;
        }

        std::string protocol = base_url.substr(0, protocol_pos + 3);
        std::string rest = base_url.substr(protocol_pos + 3);

        return protocol + username_ + ":" + password_ + "@" + rest;
    }

    // 新增方法：获取网络诊断信息
    std::string RTSPInput::getNetworkDiagnosticInfo() const {
        if (!format_ctx_ || !format_ctx_->url) {
            return "无法获取网络诊断信息";
        }
        
        std::string url = format_ctx_->url;
        UrlInfo url_info = NetworkDetector::parseUrl(url);
        
        if (!url_info.is_valid) {
            return "URL格式无效";
        }
        
        std::ostringstream oss;
        oss << "网络诊断信息:\n";
        oss << "主机: " << url_info.hostname << "\n";
        oss << "端口: " << url_info.port << "\n";
        
        // 同步进行快速测试
        NetworkDetector detector;
        NetworkTestResult result = detector.comprehensiveTest(url, 3000);
        
        oss << "连通性: " << (result.success ? "正常" : "异常") << "\n";
        oss << "响应时间: " << result.response_time_ms << "ms\n";
        oss << "测试方法: " << result.method_used << "\n";
        
        if (!result.success) {
            oss << "错误信息: " << result.error_message;
        }
        
        return oss.str();
    }

    // 新增方法：手动触发网络测试
    bool RTSPInput::manualNetworkTest() {
        if (!format_ctx_ || !format_ctx_->url) {
            return false;
        }
        
        std::string url = format_ctx_->url;
        NetworkTestResult result = network_detector_->comprehensiveTest(url, 5000);
        
        std::cout << "手动网络测试结果: " 
                  << (result.success ? "成功" : "失败") 
                  << " (" << result.response_time_ms << "ms, " 
                  << result.method_used << ")" << std::endl;
        
        if (!result.success) {
            std::cout << "错误信息: " << result.error_message << std::endl;
        }
        
        return result.success;
    }

}// namespace media