#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <memory>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

namespace media {

/**
 * @brief 解码器状态
 */
enum class DecoderState {
    Uninitialized,  // 未初始化
    Ready,          // 就绪
    Decoding,       // 解码中
    Flushing,       // 刷新缓冲区
    Error           // 错误状态
};

/**
 * @brief 解码器统计信息
 */
struct DecoderStats {
    uint64_t frames_decoded = 0;    // 已解码帧数
    uint64_t frames_dropped = 0;    // 丢弃帧数
    uint64_t decode_errors = 0;     // 解码错误数
    double avg_decode_time = 0.0;   // 平均解码时间（毫秒）
    double fps = 0.0;               // 实时解码帧率
};

/**
 * @brief 视频解码器接口
 */
class IVideoDecoder {
public:
    virtual ~IVideoDecoder() = default;
    
    // 帧回调函数类型
    using FrameCallback = std::function<void(AVFrame*)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    /**
     * @brief 初始化解码器
     * @param codecpar 编码参数
     * @return 是否成功
     */
    virtual bool initialize(const AVCodecParameters* codecpar) = 0;
    
    /**
     * @brief 发送数据包到解码器
     * @param packet 数据包
     * @return 是否成功
     */
    virtual bool sendPacket(AVPacket* packet) = 0;
    
    /**
     * @brief 从解码器接收帧
     * @param frame 输出帧
     * @return 是否成功接收到帧
     */
    virtual bool receiveFrame(AVFrame* frame) = 0;
    
    /**
     * @brief 刷新解码器缓冲区
     */
    virtual void flush() = 0;
    
    /**
     * @brief 获取解码器状态
     * @return 当前状态
     */
    virtual DecoderState getState() const = 0;
    
    /**
     * @brief 获取统计信息
     * @return 解码统计
     */
    virtual DecoderStats getStats() const = 0;
    
    /**
     * @brief 设置帧回调（异步模式）
     * @param callback 帧回调函数
     */
    virtual void setFrameCallback(FrameCallback callback) = 0;
    
    /**
     * @brief 设置错误回调
     * @param callback 错误回调函数
     */
    virtual void setErrorCallback(ErrorCallback callback) = 0;
    
    /**
     * @brief 是否支持硬件加速
     * @return 是否支持
     */
    virtual bool supportsHardwareAcceleration() const = 0;
    
    /**
     * @brief 启用硬件加速
     * @param device_type 设备类型
     * @return 是否成功
     */
    virtual bool enableHardwareAcceleration(AVHWDeviceType device_type) = 0;
};

/**
 * @brief 音频解码器接口 - src/media/decoder/audio_decoder.h
 */
class IAudioDecoder {
public:
    virtual ~IAudioDecoder() = default;
    
    // 与视频解码器类似的接口，但针对音频特化
    using FrameCallback = std::function<void(AVFrame*)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    virtual bool initialize(const AVCodecParameters* codecpar) = 0;
    virtual bool sendPacket(AVPacket* packet) = 0;
    virtual bool receiveFrame(AVFrame* frame) = 0;
    virtual void flush() = 0;
    virtual DecoderState getState() const = 0;
    virtual DecoderStats getStats() const = 0;
    virtual void setFrameCallback(FrameCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;
};

/**
 * @brief 解码器工厂
 */
class DecoderFactory {
public:
    /**
     * @brief 创建视频解码器
     * @param codecpar 编码参数
     * @return 解码器实例
     */
    static std::unique_ptr<IVideoDecoder> createVideoDecoder(const AVCodecParameters* codecpar);
    
    /**
     * @brief 创建音频解码器
     * @param codecpar 编码参数
     * @return 解码器实例
     */
    static std::unique_ptr<IAudioDecoder> createAudioDecoder(const AVCodecParameters* codecpar);
    
    /**
     * @brief 检查编码器支持情况
     * @param codec_id 编码器ID
     * @return 是否支持
     */
    static bool isCodecSupported(AVCodecID codec_id);
};

} // namespace media

#endif // VIDEO_DECODER_H
