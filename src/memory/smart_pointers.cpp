#include "smart_pointers.h"

namespace ffmpeg {

// 全局帧池实例定义
FramePool<32> g_video_frame_pool;
FramePool<16> g_audio_frame_pool;

} // namespace ffmpeg
