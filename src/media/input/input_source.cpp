#include "input_source.h"
#include "file_input.h"
#include "rtsp_input.h"

namespace media {

    std::unique_ptr<IInputSource> InputSourceFactory::create(const std::string& url) {
        InputSourceType type = detectType(url);

        switch (type)
        {
        case InputSourceType::RTSP:
            return std::make_unique<RTSPInput>();
        case InputSourceType::LocalFile:
            return std::make_unique<FileInput>();
        
        default:
            return nullptr;
        }
    }

    InputSourceType InputSourceFactory::detectType(const std::string& url) {
        if(url.find("rtsp://") == 0){
            return InputSourceType::RTSP;
        }else if(url.find("http://") == 0 || url.find("https://") == 0){
            return InputSourceType::HTTP;
        }else if(url.find("udp://") == 0){
            return InputSourceType::UDP;
        }else{
            // 假设是本地文件
            return InputSourceType::LocalFile;
        }
    }
} // namespace media