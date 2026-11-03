#ifndef TGCALLS_STREAM_RENDER_POLICY_H
#define TGCALLS_STREAM_RENDER_POLICY_H

#include <algorithm>
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

// Time until the next render tick. It used to be a fixed 8 ms (120 Hz) for as long as the stream
// existed. Video never exceeds 60 fps, so one display refresh is enough; audio is refilled in
// bulk, so a few tens of milliseconds are enough. Whatever the activity, the delay stays within
// half of the ring buffer so a full ring cannot drain below half before the next refill.
inline int streamRenderDelayMs(StreamRenderActivity activity, int ringCapacityMs) {
    int delay = 20;
    switch (activity) {
        case StreamRenderActivity::Idle:
            delay = 20;
            break;
        case StreamRenderActivity::AudioOnly:
            delay = 40;
            break;
        case StreamRenderActivity::Video:
            delay = 16;
            break;
    }
    return std::max(1, std::min(delay, ringCapacityMs / 2));
}

// Frames are decoded only for a stream that has a live sink; hardware sessions and CPU are not
// spent on tiles nobody shows. The decoder resumes at the next part, which starts on a keyframe.
inline bool shouldDecodeStreamVideo(size_t liveSinkCount) {
    return liveSinkCount > 0;
}

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
