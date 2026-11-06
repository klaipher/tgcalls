#ifndef TGCALLS_CALL_SYSTEM_POLICY_H
#define TGCALLS_CALL_SYSTEM_POLICY_H

// Pure, platform-neutral policy helpers for the system, timers and power of a call (one file for both apps; it
// replaced iOS CallPowerPolicy.h and the Mac RenderSchedule). Header only so host tests can include it without
// WebRTC. Nothing here is negotiated or sent on the wire: it only decides how often local timers fire, how large and
// how fast the locally captured camera frames are before they reach the encoder, and which quality of service the
// engine threads get.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tgcalls {
namespace systempolicy {

// The outgoing camera frame rate the engine sends. The Mac camera capture rate is derived from it.
constexpr int kOutgoingCameraFps = 25;

// Cadence of the per-call stats sampling of a 1:1 call (it feeds the signal bars, so it stays at one second until
// a measurement says otherwise) and of the group-call bitrate log (nothing visible reads it).
constexpr int kStatsIntervalMs = 1000;
constexpr int kGroupStatsIntervalMs = 2000;

// How long after entering broadcast mode the engine reports the broadcast as connected. This is the delay the removed
// perpetual 500 ms network-status timer used to introduce, kept so the connecting state shows for the same time.
constexpr int kBroadcastConnectedDelayMs = 500;

// Local cap on the outgoing camera video, set by the thermal/battery ladder. Zero means no limit.
struct OutgoingVideoSize {
    int width = 0;
    int height = 0;
    int fps = 0;

    bool operator==(const OutgoingVideoSize &other) const {
        return width == other.width && height == other.height && fps == other.fps;
    }
};

inline int evenAtLeastTwo(double value) {
    int rounded = static_cast<int>(std::floor(value + 0.5));
    rounded -= rounded % 2;
    return std::max(rounded, 2);
}

// The short side is the "height" of the cap, so the cap works for both landscape and portrait capture. Frames are only
// ever made smaller or slower, never larger or faster, and the aspect ratio is kept.
inline OutgoingVideoSize limitOutgoingVideo(int width, int height, int fps, int maxShortSide, int maxFps) {
    OutgoingVideoSize result;
    result.width = width;
    result.height = height;
    result.fps = fps;
    if (width <= 0 || height <= 0) {
        return result;
    }
    if (maxShortSide > 0) {
        const int shortSide = std::min(width, height);
        if (shortSide > maxShortSide) {
            const double scale = static_cast<double>(maxShortSide) / static_cast<double>(shortSide);
            result.width = evenAtLeastTwo(static_cast<double>(width) * scale);
            result.height = evenAtLeastTwo(static_cast<double>(height) * scale);
        }
    }
    if (maxFps > 0 && fps > maxFps) {
        result.fps = maxFps;
    }
    return result;
}

// Thermal video ladder. The app maps the system thermal state, Low Power Mode and the camera's system pressure to a
// level: 0 nominal, 1 elevated (UI only, no video change), 2 high, 3 critical. With CallsRuntimeSwitches::thermalVideoLadder
// on, the ladder lowers the size and frame rate the encoder is fed. The size is a cap on the SHORT side, so it works for
// portrait and landscape capture alike. A screen share keeps its size (text must stay legible) and only loses frame
// rate: 15 fps from level 2, 10 fps at level 3 (see videoLadderStep with isScreen). The app holds a level for 30 s after it
// improves (each resolution change reconfigures the encoder); UI-only effects release after 5 s.
struct VideoLadderStep {
    int maxShortSide = 0;
    int maxFps = 0;
};

inline VideoLadderStep videoLadderStep(int level, bool isScreen = false) {
    if (isScreen) {
        switch (level) {
            case 2:
                return { 0, 15 };
            case 3:
                return { 0, 10 };
            default:
                return { 0, 0 };
        }
    }
    switch (level) {
        case 2:
            return { 540, 20 };
        case 3:
            return { 360, 15 };
        default:
            return { 0, 0 };
    }
}

inline OutgoingVideoSize videoLadderFormat(int level, int width, int height, bool enabled, bool isScreen = false) {
    if (!enabled) {
        return limitOutgoingVideo(width, height, kOutgoingCameraFps, 0, 0);
    }
    const auto step = videoLadderStep(std::max(0, std::min(3, level)), isScreen);
    return limitOutgoingVideo(width, height, kOutgoingCameraFps, step.maxShortSide, step.maxFps);
}

// The frame rate the camera itself should run at for a thermal level; 0 means no cap (the rate the capture started
// with). Lowering the rate at the source saves the camera and its image processing, not just the encode, and avoids the
// uneven frame drops of an adapter working from a faster source.
inline int thermalCameraFpsCap(int level, bool enabled, bool isScreen = false) {
    if (!enabled || level < 2) {
        return 0;
    }
    return videoLadderStep(std::min(3, level), isScreen).maxFps;
}

// Remote video is dropped locally (never signalled to the peer) while the app is in the background and Picture in
// Picture is not showing it. Always on.
inline bool shouldDropRemoteVideo(bool appInBackground, bool pictureInPictureActive) {
    return appInBackground && !pictureInPictureActive;
}

} // namespace systempolicy
} // namespace tgcalls

#endif
