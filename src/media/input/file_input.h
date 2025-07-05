#ifndef FILE_INPUT_H
#define FILE_INPUT_H
#include "input_source.h"
#include <mutex>

namespace media
{
    /**
     * @brief 本地文件输入源实现
     */
    class FileInput : public media::IInputSource
    {
    public:
        FileInput();
        ~FileInput() override;

        // 实现 IInputSource 接口
        bool open(const std::string &url) override;
        void close() override;
        AVFormatContext *getFormatContext() override;
        media::InputSourceInfo getSourceInfo() const override;
        media::InputSourceState getState() const override;
        bool isSeekable() const override;
        bool seek(int64_t timestamp) override;
        void setStateCallback(StateCallback callback) override;
        std::string getLastError() const override;

    private:
        AVFormatContext *format_ctx_ = nullptr;
        mutable std::mutex state_mutex_;
        mutable media::InputSourceState state_ = media::InputSourceState::Closed; 
        mutable std::string last_error_;
        mutable StateCallback state_callback_;

        void changeState(media::InputSourceState new_state, const std::string &message = "");
    };
}

#endif // FILE_INPUT_H