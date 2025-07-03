#include "rtsp_input.h"
#include <iostream>
#include <sstream>

extern "C"{
    #include <libavutil/opt.h>
    #include <libavutil/error.h>
}

namespace media{
    RTSPInput::RTSPInput(){
        // 确保网络模块已初始化
        avformat_network_init();
    }

    RTSPInput::~RTSPInput(){
        // 先关闭视频流
        close();
        avformat_network_deinit();
    }

    bool RTSPInput::open(const std::string& url){
        std::lock_guard<std::mutex> lock(state_mutex);

        if(state_ != InputSourceState::Closed){
            last_error_ = "输入源已经打开或正在打开";
            return false;
        }

        changedState(InputSourceState::Opening,"正在连接RTSP流...");

        // 分配格式上下文
        format_ctx_ = avformat_alloc_context();
        if(!format_ctx_){
            last_error_ = "无法分配 AVFormatContext";
            changedState(InputSourceState::Error,last_error_);
            return false;
        }

        // 设置 RSTP 流
        AVDictionary* options = nullptr;
        if(!setupRTSPOptions(&options)){
            av_dict_free(&options);
            avformat_free_context(format_ctx_);
            format_ctx_ = nullptr;
            changeStated(InputSourceState::Error,last_error_);
            return false;
        }

        // 格式化URL (添加认证信息)
        std::string final_url = formatRTSPUrl(url);

        // 打开输入
        int ret = avformat_open_input(&format_ctx_, final_url.c_str(), nullptr, &options);
        av_dict_free(&options);
        if(ret < 0){
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret,error_buf,sizeof(error_buf));
            last_error_ = "无法打开RTSP流: " + std::string(error_buf);

            avformat_free_context(format_ctx_);
            format_ctx_ = nullptr;
            changeState(InputSourceState::Error,last_error_);
            return false;
        }

        // 启动连接监控
        startConnectionMonitor();

        changeState(InputSourceState::Opened,"RTSP流连接成功");
        return true;
    }
}