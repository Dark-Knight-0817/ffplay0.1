#include "rtsp_input.h"
#include <iostream>
#include <sstream>

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

        // 添加更严格的超时控制
        av_dict_set(&options, "timeout", "3000000", 0);       // 3秒连接超时（微秒）
        av_dict_set(&options, "stimeout", "3000000", 0);      // 3秒socket超时
        av_dict_set(&options, "rw_timeout", "3000000", 0);    // 3秒读写超时
        av_dict_set(&options, "rtsp_transport", "tcp", 0);    // 强制使用TCP
        av_dict_set(&options, "analyzeduration", "500000", 0); // 0.5秒分析时间
        av_dict_set(&options, "probesize", "500000", 0);      // 500KB探测大小

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
        av_dict_set(options, "buffer_size", "1048576", 0);    // 1MB缓冲区
        
        // 性能优化
        av_dict_set(options, "analyzeduration", "500000", 0); // 0.5秒分析
        av_dict_set(options, "probesize", "500000", 0);       // 500KB探测
        av_dict_set(options, "max_delay", "500000", 0);       // 最大延迟0.5秒
        
        // 错误恢复
        av_dict_set(options, "reconnect", "1", 0);            // 允许重连
        av_dict_set(options, "reconnect_at_eof", "1", 0);     // EOF时重连
        av_dict_set(options, "reconnect_streamed", "1", 0);   // 流断开时重连
        av_dict_set(options, "reconnect_delay_max", "2", 0);  // 最大重连延迟2秒
        
        return true;
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
        monitor_thread_ = std::thread(
            [this]()
            {
                monitorConnection();
            });
    }

    void RTSPInput::stopConnectionMonitor(){
        should_stop_monitor_.store(true);
        if(monitor_thread_.joinable()){
            monitor_thread_.join();
        }
    }

    void RTSPInput::monitorConnection(){
        while(!should_stop_monitor_.load()){
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if(should_stop_monitor_.load()){
                break;
            }
            // 简单的连接检测,尝试读取一个包
            if(!testConnection()){
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

        // 这里可以实现更复杂的连接检测逻辑
        // 例如发送RTSP PING 请求或尝试读取数据
        return true;
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

}// namespace media