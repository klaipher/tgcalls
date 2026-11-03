#ifndef TGCALLS_CAPTURE_FRAME_POLICY_H
#define TGCALLS_CAPTURE_FRAME_POLICY_H

#include <cstdint>
#include <deque>

// Decisions of the iOS camera capturer, free of AVFoundation so a host test covers them.

namespace tgcalls {

enum class CaptureFramePlan {
    // Nothing downstream wants this frame (the camera is paused): no conversion, no crop.
    Drop,
    // Today's path: convert the NV12 camera buffer to I420 on the CPU.
    I420,
    // The CVPixelBuffer travels to the encoder as it is.
    Native
};

inline bool isBiPlanar420(uint32_t fourCC) {
    const uint32_t k420f = 0x34323066; // kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
    const uint32_t k420v = 0x34323076; // kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
    return fourCC == k420f || fourCC == k420v;
}

inline CaptureFramePlan planCameraFrame(bool paused, bool nativeEnabled, bool nativeFailed, uint32_t pixelFormat) {
    if (paused) {
        return CaptureFramePlan::Drop;
    }
    if (nativeEnabled && !nativeFailed && isBiPlanar420(pixelFormat)) {
        return CaptureFramePlan::Native;
    }
    return CaptureFramePlan::I420;
}

// Holding camera buffers while the encoder works on them can starve the camera's small buffer
// pool, which AVFoundation reports as dropped frames with reason "OutOfBuffers". A burst of them
// latches the native path off for the rest of the capture, back to the I420 copy.
class NativeCaptureHealth {
public:
    static const int kMaxStarvedFrames = 5;
    static const int64_t kWindowMs = 2000;

    // Returns true when this drop makes the native path fail.
    bool onFrameDropped(bool outOfBuffers, int64_t nowMs) {
        if (_failed || !outOfBuffers) {
            return false;
        }
        _recent.push_back(nowMs);
        while (!_recent.empty() && nowMs - _recent.front() > kWindowMs) {
            _recent.pop_front();
        }
        if ((int)_recent.size() >= kMaxStarvedFrames) {
            _failed = true;
            return true;
        }
        return false;
    }

    bool failed() const {
        return _failed;
    }

private:
    std::deque<int64_t> _recent;
    bool _failed = false;
};

// The camera runs at the rate the adapter wants when its format allows, so frames the adapter
// would drop are never produced. Never raises the rate above the one configured.
inline int chooseCaptureFps(int targetFps, int currentFps, double minSupportedFps, double maxSupportedFps) {
    if (targetFps <= 0 || targetFps >= currentFps) {
        return currentFps;
    }
    if (minSupportedFps <= (double)targetFps && (double)targetFps <= maxSupportedFps) {
        return targetFps;
    }
    return currentFps;
}

} // namespace tgcalls

#endif
