#ifndef TGCALLS_CALL_POWER_POLICY_H
#define TGCALLS_CALL_POWER_POLICY_H

#include <algorithm>
#include <cmath>

namespace tgcalls {

// Pure decisions behind the call power and heat optimisations. All of them are local: none changes
// what is negotiated or signalled to the peer, and every one is behind a CallsRuntimeSwitches flag
// whose default is today's behaviour.

// Thermal video ladder. The app maps the system thermal state, Low Power Mode and the camera's
// system pressure to a level: 0 nominal, 1 elevated (UI only), 2 high, 3 critical. The ladder only
// lowers the size and frame rate the encoder is fed, through the same output-format request the
// receiver's aspect adaptation already uses; the encoder, codec and bitrate negotiation are untouched.
struct VideoLadderFormat {
    int width = 0;
    int height = 0;
    int fps = 25;
};

constexpr int kVideoLadderDefaultFps = 25;

inline VideoLadderFormat videoLadderFormat(int level, int width, int height, bool enabled) {
    VideoLadderFormat result;
    result.width = width;
    result.height = height;
    result.fps = kVideoLadderDefaultFps;
    if (!enabled || width <= 0 || height <= 0) {
        return result;
    }
    level = std::max(0, std::min(3, level));
    int longSideCap = 0;
    switch (level) {
        case 2:
            longSideCap = 640;
            result.fps = 20;
            break;
        case 3:
            longSideCap = 480;
            result.fps = 15;
            break;
        default:
            return result;
    }
    const int longSide = std::max(width, height);
    if (longSide > longSideCap) {
        const double scale = (double)longSideCap / (double)longSide;
        auto even = [](double value) {
            const int rounded = (int)std::lround(value);
            return std::max(2, rounded - (rounded % 2));
        };
        result.width = even((double)width * scale);
        result.height = even((double)height * scale);
    }
    return result;
}

// The frame rate the camera itself should run at for a thermal level; 0 means no cap (the rate the
// capture started with). Lowering the rate at the source saves the camera and its image processing, not
// just the encode, and avoids the uneven frame drops of an adapter working from a faster source.
inline int thermalCameraFpsCap(int level, bool enabled) {
    if (!enabled || level < 2) {
        return 0;
    }
    return videoLadderFormat(level, 1280, 720, true).fps;
}

// Engine thread quality of service.
enum class EngineThreadRole {
    Network,
    Media,
    Worker
};

enum class EngineThreadQos {
    // Leave the thread at the system default.
    Unchanged,
    UserInitiated
};

// The call threads move packets and frames on a deadline; with the switch on they are not queued
// behind default-priority app work. Audio capture and playback run on the system's real-time
// threads and are not touched.
inline EngineThreadQos engineThreadQos(EngineThreadRole role, bool enabled) {
    (void)role;
    return enabled ? EngineThreadQos::UserInitiated : EngineThreadQos::Unchanged;
}

// Remote video is dropped locally (never signalled to the peer) while the app is in the background
// and Picture in Picture is not showing it.
inline bool shouldDropRemoteVideo(bool appInBackground, bool pictureInPictureActive, bool enabled) {
    return enabled && appInBackground && !pictureInPictureActive;
}

} // namespace tgcalls

#endif
