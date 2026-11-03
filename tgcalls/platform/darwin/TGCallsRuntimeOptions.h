#ifndef TGCallsRuntimeOptions_h
#define TGCallsRuntimeOptions_h

#include <atomic>

namespace tgcalls {

struct RuntimeOptions {
    static std::atomic<bool> &livestreamHardwareDecode() {
        static std::atomic<bool> value{false};
        return value;
    }

    static std::atomic<bool> &cameraFormatV2() {
        static std::atomic<bool> value{true};
        return value;
    }

    static std::atomic<bool> &hardwareCrop() {
        static std::atomic<bool> value{false};
        return value;
    }

    static std::atomic<bool> &nativeDecodeFormat() {
        static std::atomic<bool> value{false};
        return value;
    }

    static std::atomic<bool> &threadQoS() {
        static std::atomic<bool> value{false};
        return value;
    }

    static std::atomic<bool> &livestreamVideoSuspended() {
        static std::atomic<bool> value{false};
        return value;
    }

    static std::atomic<int> &outgoingCameraMaxShortSide() {
        static std::atomic<int> value{0};
        return value;
    }

    static std::atomic<int> &outgoingCameraMaxFps() {
        static std::atomic<int> value{0};
        return value;
    }

    static std::atomic<bool> &screencastHardwareEncoder() {
        static std::atomic<bool> value{false};
        return value;
    }
};

}

#endif
