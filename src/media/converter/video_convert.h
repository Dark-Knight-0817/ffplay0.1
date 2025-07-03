#ifndef VIDEO_CONVERTER_H
#define VIDEO_CONVERTER_H

#include <memory>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace media {

/**
 * @brief 视频转换参数
 */
struct VideoConvertParams {
    // 输入参数
    int src_width = 0;
    int src_height = 0;
    AVPixelFormat src_format = AV_PIX_FMT_NONE;
    
    // 输出参数
    int dst_width = 0;
    int dst_height = 0;
    AVPixelFormat dst_format = AV_PIX_FMT_NONE;
    
    // 转换参数
    int sws_flags = SWS_BILINEAR;  // 缩放算法
    
    bool isValid() const {
        return src_width > 0 && src_height > 0 && 
               dst_width > 0 && dst_height > 0 &&
               src_format != AV_PIX_FMT_NONE &&
               dst_format != AV_PIX_FMT_NONE;
    }
};

/**
 * @brief 视频格式转换器接口
 */
class IVideoConverter {
public:
    virtual ~IVideoConverter() = default;
    
    /**
     * @brief 初始化转换器
     * @param params 转换参数
     * @return 是否成功
     */
    virtual bool initialize(const VideoConvertParams& params) = 0;
    
    /**
     * @brief 转换帧
     * @param src_frame 源帧
     * @param dst_frame 目标帧
     * @return 是否成功
     */
    virtual bool convert(const AVFrame* src_frame, AVFrame* dst_frame) = 0;
    
    /**
     * @brief 获取输出帧大小
     * @return 字节数
     */
    virtual size_t getOutputFrameSize() const = 0;
    
    /**
     * @brief 重新配置转换器
     * @param params 新的转换参数
     * @return 是否成功
     */
    virtual bool reconfigure(const VideoConvertParams& params) = 0;
};

/**
 * @brief 音频格式转换器接口 - src/media/converter/audio_converter.h
 */
struct AudioConvertParams {
    // 输入参数
    int src_sample_rate = 0;
    int src_channels = 0;
    AVSampleFormat src_format = AV_SAMPLE_FMT_NONE;
    
    // 输出参数
    int dst_sample_rate = 0;
    int dst_channels = 0;
    AVSampleFormat dst_format = AV_SAMPLE_FMT_NONE;
    
    bool isValid() const {
        return src_sample_rate > 0 && src_channels > 0 &&
               dst_sample_rate > 0 && dst_channels > 0 &&
               src_format != AV_SAMPLE_FMT_NONE &&
               dst_format != AV_SAMPLE_FMT_NONE;
    }
};

class IAudioConverter {
public:
    virtual ~IAudioConverter() = default;
    
    virtual bool initialize(const AudioConvertParams& params) = 0;
    virtual bool convert(const AVFrame* src_frame, AVFrame* dst_frame) = 0;
    virtual size_t getOutputFrameSize() const = 0;
    virtual bool reconfigure(const AudioConvertParams& params) = 0;
};

} // namespace media

#endif // VIDEO_CONVERTER_H