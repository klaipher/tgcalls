#ifndef TGCALLS_VIDEO_DECODE_FALLBACK_H
#define TGCALLS_VIDEO_DECODE_FALLBACK_H

namespace tgcalls {

enum class VideoDecodeStage {
    Open,
    Send,
    Receive,
    FrameConversion
};

class VideoDecodeFallback {
public:
    bool isSoftwareOnly() const {
        return _softwareOnly;
    }

    bool noteHardwareFailure(VideoDecodeStage stage) {
        if (_softwareOnly) {
            return false;
        }
        _softwareOnly = true;
        _failedStage = stage;
        return true;
    }

    VideoDecodeStage failedStage() const {
        return _failedStage;
    }

private:
    bool _softwareOnly = false;
    VideoDecodeStage _failedStage = VideoDecodeStage::Open;
};

}

#endif
