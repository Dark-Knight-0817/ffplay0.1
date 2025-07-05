#ifndef INPUT_SOURCE_H
#define INPUT_SOURCE_H

#include <string>
#include <memory>
#include <functional>

extern "C" {
#include <libavformat/avformat.h>
}

namespace media {

/**
 * @brief 输入源类型枚举
 */
enum class InputSourceType {
    LocalFile,      // 本地文件
    RTSP,          // RTSP网络流
    HTTP,          // HTTP流
    UDP,           // UDP流
    Unknown        // 未知类型
};

/**
 * @brief 输入源状态
 */
enum class InputSourceState {
    Closed,        // 关闭状态
    Opening,       // 正在打开
    Opened,        // 已打开
    Reading,       // 正在读取
    Disconnected,  // 网络断流
    EndOfStream,   // 流结束
    Error          // 错误状态
};

/**
 * @brief 输入源信息
 */
struct InputSourceInfo {
    InputSourceType type = InputSourceType::Unknown;
    std::string url;
    int64_t duration = -1;           // 时长（微秒），-1表示未知
    int64_t bit_rate = 0;            // 总码率
    int64_t file_size = 0;           // 文件大小（字节）
    bool is_seekable = false;        // 是否支持seek
    std::string format_name;         // 格式名称
    
    // 网络流特有信息
    int connection_timeout = 10000;  // 连接超时（毫秒）
    std::string transport_protocol;  // 传输协议（tcp/udp）
    
    bool isValid() const {
        return type != InputSourceType::Unknown && !url.empty();
    }
};

/**
 * @brief 输入源抽象接口
 * 
 * 定义了所有输入源的统一接口，屏蔽不同输入类型的差异
 */
class IInputSource {
public:
    virtual ~IInputSource() = default;
    
    // 状态回调函数类型
    using StateCallback = std::function<void(InputSourceState, const std::string&)>;
    
    /**
     * @brief 打开输入源
     * @param url 输入源地址
     * @return 是否成功
     */
    virtual bool open(const std::string& url) = 0;
    
    /**
     * @brief 关闭输入源
     */
    virtual void close() = 0;
    
    /**
     * @brief 获取FFmpeg格式上下文（用于后续处理）
     * @return AVFormatContext指针
     */
    virtual AVFormatContext* getFormatContext() = 0;
    
    /**
     * @brief 获取输入源信息
     * @return 输入源详细信息
     */
    virtual InputSourceInfo getSourceInfo() const = 0;
    
    /**
     * @brief 获取当前状态
     * @return 当前状态
     */
    virtual InputSourceState getState() const = 0;
    
    /**
     * @brief 检查是否支持seek操作
     * @return 是否支持seek
     */
    virtual bool isSeekable() const = 0;
    
    /**
     * @brief 执行seek操作
     * @param timestamp 目标时间戳（微秒）
     * @return 是否成功
     */
    virtual bool seek(int64_t timestamp) = 0;
    
    /**
     * @brief 设置状态变化回调
     * @param callback 回调函数
     */
    virtual void setStateCallback(StateCallback callback) = 0;
    
    /**
     * @brief 获取最后的错误信息
     * @return 错误描述
     */
    virtual std::string getLastError() const = 0;
};

/**
 * @brief 输入源工厂
 */
class InputSourceFactory {
public:
    /**
     * @brief 根据URL创建合适的输入源
     * @param url 输入地址
     * @return 输入源实例
     */
    static std::unique_ptr<IInputSource> create(const std::string& url);
    
    /**
     * @brief 检测输入源类型
     * @param url 输入地址
     * @return 输入源类型
     */
    static InputSourceType detectType(const std::string& url);
};

} // namespace media
#endif // INPUT_SOURCE_H