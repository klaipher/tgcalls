#ifndef TGCALLS_DARWIN_FFMPEG_H
#define TGCALLS_DARWIN_FFMPEG_H

#include "platform/PlatformInterface.h"

namespace tgcalls {

void setupDarwinVideoDecoding(AVCodecContext *codecContext);
bool darwinSupportsHardwareVideoDecoding();
bool setupDarwinHardwareVideoDecoding(AVCodecContext *codecContext);
webrtc::scoped_refptr<webrtc::VideoFrameBuffer> createDarwinPlatformFrameFromData(AVFrame const *frame);

} // namespace tgcalls

#endif
