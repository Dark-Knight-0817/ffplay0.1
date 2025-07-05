#include "file_input.h"
#include <iostream>
#include <fstream> 

extern "C"{
    #include <libavutil/error.h>
    #include <libavformat/avformat.h>
}

namespace media{
    FileInput::FileInput() = default;

    FileInput::~FileInput() { close(); }

    bool FileInput::open(const std::string& url){
        std::cout << "FileInput::open() 开始，URL: " << url << std::endl;
        
        // 检查当前状态
        if(state_ != InputSourceState::Closed){
            last_error_ = "文件已经打开";
            std::cout << "FileInput::open() 失败：文件已经打开" << std::endl;
            return false;
        }
        
        std::cout << "FileInput::open() 调用 changeState..." << std::endl;
        changeState(InputSourceState::Opening, "正在打开文件:" + url);
        std::cout << "FileInput::open() changeState 调用完成" << std::endl;

        // 检查本地文件是否存在
        if (url.find("://") == std::string::npos) {
            std::cout << "FileInput::open() 检查本地文件..." << std::endl;
            
            FILE* file_check = fopen(url.c_str(), "rb");
            if (!file_check) {
                std::cout << "FileInput::open() 文件不存在: " << url << std::endl;
                last_error_ = "文件不存在或无法访问: " + url;
                changeState(InputSourceState::Error, last_error_);
                return false;
            }
            fclose(file_check);
            std::cout << "FileInput::open() 文件存在，继续处理..." << std::endl;
        }

        std::cout << "FileInput::open() 设置 FFmpeg 选项..." << std::endl;
        
        // 设置 FFmpeg 选项
        AVDictionary* options = nullptr;
        av_dict_set(&options, "timeout", "5000000", 0); // 5秒超时
        av_dict_set(&options, "analyzeduration", "1000000", 0); // 1秒分析时间
        av_dict_set(&options, "probesize", "1000000", 0); // 1MB探测大小

        std::cout << "FileInput::open() 调用 avformat_open_input..." << std::endl;
        
        // 打开文件
        int ret = avformat_open_input(&format_ctx_, url.c_str(), nullptr, &options);
        av_dict_free(&options);
        
        std::cout << "FileInput::open() avformat_open_input 返回: " << ret << std::endl;
        
        if(ret < 0){
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            last_error_ = "无法打开文件: " + std::string(error_buf);
            changeState(InputSourceState::Error, last_error_);
            std::cout << "FileInput::open() avformat_open_input 失败: " << last_error_ << std::endl;
            return false;   
        }

        std::cout << "FileInput::open() 调用 avformat_find_stream_info..." << std::endl;
        
        // 获取流信息
        ret = avformat_find_stream_info(format_ctx_, nullptr);
        
        std::cout << "FileInput::open() avformat_find_stream_info 返回: " << ret << std::endl;
        
        if(ret < 0){
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            last_error_ = "无法获取流信息: " + std::string(error_buf);

            avformat_close_input(&format_ctx_);
            changeState(InputSourceState::Error, last_error_);
            std::cout << "FileInput::open() avformat_find_stream_info 失败: " << last_error_ << std::endl;
            return false;
        }

        changeState(InputSourceState::Opened, "文件打开成功");
        std::cout << "FileInput::open() 成功完成" << std::endl;
        return true;
    }

    void FileInput::close(){
        std::cout << "FileInput::close() 开始" << std::endl;
        
        // 先处理 FFmpeg 资源清理
        AVFormatContext* temp_format_ctx = nullptr;
        InputSourceState current_state;
        
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            current_state = state_;
            
            if(format_ctx_){
                temp_format_ctx = format_ctx_;
                format_ctx_ = nullptr;  // 立即设为 nullptr
            }
            
            // 清除错误信息
            last_error_.clear();
        } // 锁在这里释放
        
        // 在锁外清理 FFmpeg 资源
        if(temp_format_ctx) {
            std::cout << "FileInput::close() 清理 FFmpeg 资源" << std::endl;
            avformat_close_input(&temp_format_ctx);
        }
        
        // 只有在状态不是 Closed 时才改变状态
        if(current_state != InputSourceState::Closed){
            std::cout << "FileInput::close() 调用 changeState" << std::endl;
            changeState(InputSourceState::Closed, "文件关闭");
        } else {
            std::cout << "FileInput::close() 已经是关闭状态，跳过状态变更" << std::endl;
        }
        
        std::cout << "FileInput::close() 完成" << std::endl;
    }

    AVFormatContext* FileInput::getFormatContext(){
        std::lock_guard<std::mutex> lock(state_mutex_);
        return format_ctx_;
    }

    InputSourceInfo FileInput::getSourceInfo() const{
        std::lock_guard<std::mutex> lock(state_mutex_);

        InputSourceInfo info;
        info.type = InputSourceType::LocalFile;

        if(!format_ctx_){
            return info;
        }

        info.url = format_ctx_->url ? format_ctx_->url : "";
        info.duration = format_ctx_->duration;
        info.bit_rate = format_ctx_->bit_rate;
        info.file_size = avio_size(format_ctx_->pb);
        info.is_seekable = true; // 本地文件支持 seek
        info.format_name = format_ctx_->iformat ? format_ctx_->iformat->name : "";

        return info;
    }

    InputSourceState FileInput::getState() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return state_;
    }

    std::string FileInput::getLastError() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return last_error_;
    }

    bool FileInput::isSeekable() const {
        return true; // 本地文件支持seek
    }

    bool FileInput::seek(int64_t timestamp){
        std::lock_guard<std::mutex> lock(state_mutex_);

        if(!format_ctx_ || state_ != InputSourceState::Opened){
            last_error_ = "文件未打开";
            return false;
        }

        int ret = av_seek_frame(format_ctx_, -1, timestamp, AVSEEK_FLAG_BACKWARD);
        if(ret < 0){
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            last_error_ = "Seek失败: " + std::string(error_buf);
            return false;
        }

        return true;
    }

    void FileInput::setStateCallback(StateCallback callback){
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_callback_ = callback;
    }

    void FileInput::changeState(InputSourceState new_state, const std::string& message){
        std::cout << "changeState() 开始，新状态: " << static_cast<int>(new_state) << std::endl;
        
        // 防止递归调用
        static thread_local bool in_change_state = false;
        if (in_change_state) {
            std::cout << "changeState() 检测到递归调用，直接返回" << std::endl;
            return;
        }
        
        StateCallback callback;
        bool state_changed = false;
        
        // 使用局部作用域的锁
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            
            if(state_ == new_state) {
                std::cout << "changeState() 状态没有变化，直接返回" << std::endl;
                return;
            }
            
            std::cout << "changeState() 更新状态从 " << static_cast<int>(state_) << " 到 " << static_cast<int>(new_state) << std::endl;
            state_ = new_state;
            state_changed = true;
            
            // 复制回调函数（如果有的话）
            callback = state_callback_;
        } // 锁在这里自动释放
        
        if(!message.empty()){
            std::cout << "File Input: " << message << std::endl;
        }
        
        // 在锁外调用回调函数，避免死锁
        if(state_changed && callback) {
            std::cout << "changeState() 调用回调函数..." << std::endl;
            in_change_state = true;
            try {
                callback(new_state, message);
            } catch (...) {
                std::cout << "changeState() 回调函数异常" << std::endl;
            }
            in_change_state = false;
            std::cout << "changeState() 回调函数完成" << std::endl;
        }
        
        std::cout << "changeState() 完成" << std::endl;
    }

} // namespace media
