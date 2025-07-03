#ifndef DEMUXER_H
#define DEMUXER_H

#include <vector>
#include <memory>
#include <functional>
#include "../input/input_source.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace media {

/**
 * @brief 流信息
 */
struct StreamInfo {
    int index = -1;                    // 流索引
    AVMediaType type = AVMEDIA_TYPE_UNKNOWN; // 流类型
    AVCodecID codec_id = AV_CODEC_ID_NONE;   // 编码器ID
    std::string codec_name;            // 编码器名称
    AVRational time_base = {0, 1};     // 时间基
    int64_t duration = -1;             // 流时长
    
    // 视频流特有信息
    int width = 0;
    int height = 0;
    AVPixelFormat pix_fmt = AV_PIX_FMT_NONE;
    AVRational frame_rate = {0, 1};
    
    // 音频流特有信息
    int sample_rate = 0;
    int channels = 0;
    AVSampleFormat sample_fmt = AV_SAMPLE_FMT_NONE;
    
    bool isVideo() const { return type == AVMEDIA_TYPE_VIDEO; }
    bool isAudio() const { return type == AVMEDIA_TYPE_AUDIO; }
    bool isValid() const { return index >= 0 && type != AVMEDIA_TYPE_UNKNOWN; }
};

/**
 * @brief 解封装器接口
 * 
 * 负责从输入源中提取音视频流和数据包
 */
class IDemuxer {
public:
    virtual ~IDemuxer() = default;
    
    // 数据包回调函数类型
    using PacketCallback = std::function<void(AVPacket*, int /* stream_index */)>;
    
    /**
     * @brief 初始化解封装器
     * @param input_source 输入源
     * @return 是否成功
     */
    virtual bool initialize(std::shared_ptr<IInputSource> input_source) = 0;
    
    /**
     * @brief 获取所有流信息
     * @return 流信息列表
     */
    virtual std::vector<StreamInfo> getStreamInfos() const = 0;
    
    /**
     * @brief 查找指定类型的流
     * @param type 媒体类型
     * @return 流索引，-1表示未找到
     */
    virtual int findStream(AVMediaType type) const = 0;
    
    /**
     * @brief 读取下一个数据包
     * @param packet 输出的数据包
     * @return 是否成功
     */
    virtual bool readPacket(AVPacket* packet) = 0;
    
    /**
     * @brief 执行seek操作
     * @param stream_index 流索引
     * @param timestamp 目标时间戳
     * @return 是否成功
     */
    virtual bool seek(int stream_index, int64_t timestamp) = 0;
    
    /**
     * @brief 获取指定流的参数
     * @param stream_index 流索引
     * @return 编码参数
     */
    virtual const AVCodecParameters* getCodecParameters(int stream_index) const = 0;
    
    /**
     * @brief 开始异步读取（可选功能）
     * @param callback 数据包回调
     */
    virtual void startAsyncRead(PacketCallback callback) = 0;
    
    /**
     * @brief 停止异步读取
     */
    virtual void stopAsyncRead() = 0;
};

} // namespace media

#endif // DEMUXER_H