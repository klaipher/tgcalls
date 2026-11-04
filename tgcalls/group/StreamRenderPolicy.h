#ifndef TGCALLS_STREAM_RENDER_POLICY_H
#define TGCALLS_STREAM_RENDER_POLICY_H

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tgcalls {

// Scheduling decisions of the livestream render loop. Pure so they run in a host test; the loop
// itself (StreamingMediaContext.cpp) only applies them. Nothing here touches the wire.

// Capacity of the stereo audio ring buffer the loop fills (4800 * 8 samples at 48 kHz, 2 channels).
constexpr int kStreamAudioRingCapacityMs = 400;

enum class StreamRenderActivity {
    // No segment is playing: the tick only starts segment requests and promotes finished downloads.
    Idle,
    // Audio is queued into the ring buffer in bulk; frames are not needed.
    AudioOnly,
    // Video frames are picked by timestamp, so the tick follows the display.
    Video
};

inline StreamRenderActivity streamRenderActivity(bool audioPlaying, bool videoPlaying) {
    if (videoPlaying) {
        return StreamRenderActivity::Video;
    }
    return audioPlaying ? StreamRenderActivity::AudioOnly : StreamRenderActivity::Idle;
}

// Time until the next render tick. It used to be a fixed 8 ms (120 Hz) for as long as the stream existed.
// - Video: the tick follows the display. It wakes when the next decoded frame is due (`secondsToNextFrame`,
//   negative or not finite when unknown), within [8 ms, 20 ms]: fewer wake-ups than a fixed 16 ms at 30 fps and
//   no judder from a fixed grid.
// - Audio only: audio is refilled in bulk, so every 40 ms is enough.
// - Idle: the tick only starts segment requests and promotes finished downloads, every 20 ms.
// Whatever the activity, the delay stays within half of the ring buffer so a full ring cannot drain below half
// before the next refill.
constexpr int kStreamVideoFloorMs = 8;
constexpr int kStreamVideoCapMs = 20;

inline int streamRenderDelayMs(StreamRenderActivity activity, int ringCapacityMs, double secondsToNextFrame = -1.0) {
    int delay = 20;
    switch (activity) {
        case StreamRenderActivity::Idle:
            delay = 20;
            break;
        case StreamRenderActivity::AudioOnly:
            delay = 40;
            break;
        case StreamRenderActivity::Video:
            if (!(secondsToNextFrame >= 0.0) || !std::isfinite(secondsToNextFrame)) {
                delay = kStreamVideoFloorMs;
            } else {
                const double millis = std::ceil(secondsToNextFrame * 1000.0);
                delay = millis >= static_cast<double>(kStreamVideoCapMs) ? kStreamVideoCapMs : std::max(kStreamVideoFloorMs, static_cast<int>(millis));
            }
            break;
    }
    return std::max(1, std::min(delay, ringCapacityMs / 2));
}

// Frames are decoded only for a stream that has a live sink and is not suspended (the app reports a hidden
// window with CallsRuntimeSwitches::livestreamVideoSuspended while the views keep their sinks); hardware sessions
// and CPU are not spent on tiles nobody shows, and the render tick does not follow video that is not decoded.
// The decoder resumes at the next part, which starts on a keyframe.
inline bool shouldDecodeStreamVideo(size_t liveSinkCount, bool videoSuspended = false) {
    return liveSinkCount > 0 && !videoSuspended;
}

// Upper bound on frames decoded for one stream in one render tick. Playing at 60 fps needs one or two;
// the rest is catch-up after a tile was hidden (its part stops where it was) and shown again.
constexpr int kMaxStreamFramesDecodedPerTick = 6;

inline bool mayDecodeAnotherStreamFrame(int decodedThisTick) {
    return decodedThisTick < kMaxStreamFramesDecodedPerTick;
}

// Each part is decoded on its own: the decoder is flushed when a part has been read to its end, so a part
// has to start on a keyframe. A part that is skipped (nobody watches) never reaches that flush; the flag
// makes the decoder flush once before it is used again, so nothing from before the gap is referenced.
class SkippedPartFlag {
public:
    void noteSkipped() {
        _flushNeeded = true;
    }

    bool consumeFlushNeeded() {
        const bool result = _flushNeeded;
        _flushNeeded = false;
        return result;
    }

    // The flush is taken only when a part starts decoding. A part resumed in its middle still has its own
    // references in the decoder: flushing then makes the decoder drop every frame up to the next keyframe, so
    // the rest of the part is read in one call with nothing shown. The flag then waits for the next part.
    bool consumeFlushNeededAtPartStart(bool partStarting) {
        if (!partStarting) {
            return false;
        }
        return consumeFlushNeeded();
    }

private:
    bool _flushNeeded = false;
};

template<typename Sink>
size_t liveStreamSinkCount(std::map<std::string, std::vector<std::weak_ptr<Sink>>> const &sinks, std::string const &endpointId) {
    const auto it = sinks.find(endpointId);
    if (it == sinks.end()) {
        return 0;
    }
    size_t count = 0;
    for (const auto &sink : it->second) {
        if (!sink.expired()) {
            count++;
        }
    }
    return count;
}

// Removes sinks whose owner is gone, and endpoints left without any.
template<typename Sink>
void pruneExpiredStreamSinks(std::map<std::string, std::vector<std::weak_ptr<Sink>>> &sinks) {
    for (auto it = sinks.begin(); it != sinks.end();) {
        auto &list = it->second;
        list.erase(std::remove_if(list.begin(), list.end(), [](std::weak_ptr<Sink> const &sink) {
            return sink.expired();
        }), list.end());
        if (list.empty()) {
            it = sinks.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace tgcalls

#endif
