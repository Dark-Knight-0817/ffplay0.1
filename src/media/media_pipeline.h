#ifndef MEDIA_PIPELINE_H
#define MEDIA_PIPELINE_H

#include "input/input_source.h"
#include "demux/demuxer.h" 
#include "decoder/video_decoder.h"
#include "decoder/audio_decoder.h"
#include "converter/video_converter.h"
#include "converter/audio_converter.h"

namespace media {

/**
 * @brief 媒体处理管道
 * 
 * 协调各个模块的工作，提供统一的接口
 */
class MediaPipeline {
public:
    MediaPipeline();
    ~MediaPipeline();
    
    // 回调函数类型
    using VideoFrameCallback = std::function<void(AVFrame*)>;
    using AudioFrameCallback = std::function<void(AVFrame*)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    /**
     * @brief 打开媒体源
     * @param url 媒体地址
     * @return 是否成功
     */
    bool open(const std::string& url);
    
    /**
     * @brief 开始处理
     * @return 是否成功
     */
    bool start();
    
    /**
     * @brief 停止处理
     */
    void stop();
    
    /**
     * @brief 关闭管道
     */
    void close();
    
    /**
     * @brief 设置视频帧回调
     * @param callback 回调函数
     */
    void setVideoFrameCallback(VideoFrameCallback callback);
    
    /**
     * @brief 设置音频帧回调
     * @param callback 回调函数
     */
    void setAudioFrameCallback(AudioFrameCallback callback);
    
    /**
     * @brief 设置错误回调
     * @param callback 回调函数
     */
    void setErrorCallback(ErrorCallback callback);
    
    /**
     * @brief 获取视频流信息
     * @return 流信息
     */
    StreamInfo getVideoStreamInfo() const;
    
    /**
     * @brief 获取音频流信息
     * @return 流信息
     */
    StreamInfo getAudioStreamInfo() const;

private:
    // 各个模块的实例
    std::shared_ptr<IInputSource> input_source_;
    std::unique_ptr<IDemuxer> demuxer_;
    std::unique_ptr<IVideoDecoder> video_decoder_;
    std::unique_ptr<IAudioDecoder> audio_decoder_;
    std::unique_ptr<IVideoConverter> video_converter_;
    std::unique_ptr<IAudioConverter> audio_converter_;
    
    // 内部状态
    bool is_running_ = false;
    std::string last_error_;
    
    // 回调函数
    VideoFrameCallback video_callback_;
    AudioFrameCallback audio_callback_;
    ErrorCallback error_callback_;
    
    // 内部方法
    bool setupVideo();
    bool setupAudio();
    void processPackets();
    void handleError(const std::string& error);
};

} // namespace media

#endif // MEDIA_PIPELINE_H