#ifndef TGCALLS_CALL_SYSTEM_POLICY_H
#define TGCALLS_CALL_SYSTEM_POLICY_H

// Pure, platform-neutral policy helpers for batch C (system, timers and power). Header only so host tests can include
// it without WebRTC. Nothing here is negotiated or sent on the wire: it only decides how often local timers fire and how
// large the locally captured camera frames are before they reach the encoder.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tgcalls {
namespace systempolicy {

// Cadence of the per-call stats sampling (signal bars and the end-of-call bitrate log). It was 1000 ms.
constexpr int kStatsIntervalMs = 2000;

// How long after entering broadcast mode the engine reports the broadcast as connected. This is the delay the removed
// perpetual 500 ms network-status timer used to introduce, kept so the connecting state shows for the same time.
constexpr int kBroadcastConnectedDelayMs = 500;

// Livestream render loop. The loop used to tick at 120 Hz regardless of what was playing.
struct RenderSchedule {
    static constexpr int kFloorMs = 8;
    static constexpr int kCapMs = 20;

    // hasVideo: a video part is currently being played. secondsToNextFrame: time until the next decoded frame is due,
    // negative when unknown. Audio only needs a refill about every 20 ms (the ring buffer holds 800 ms).
    static int nextDelayMs(bool hasVideo, double secondsToNextFrame) {
        if (!hasVideo) {
            return kCapMs;
        }
        if (!(secondsToNextFrame >= 0.0) || !std::isfinite(secondsToNextFrame)) {
            return kFloorMs;
        }
        const double millis = std::ceil(secondsToNextFrame * 1000.0);
        if (millis >= static_cast<double>(kCapMs)) {
            return kCapMs;
        }
        return std::max(kFloorMs, static_cast<int>(millis));
    }
};

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

} // namespace systempolicy
} // namespace tgcalls

#endif
