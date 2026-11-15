// Host-test stand-in for WebRTC's audio_processing.h: only the API voice_enhance.cc uses, ProcessStream copies the
// input. It lets the core and the RNNoise path run on a Mac without building WebRTC. Never used in an app build.
#pragma once
#include <memory>

namespace webrtc {

struct StreamConfig {
    StreamConfig(int rate, size_t channels) : rate(rate), channels(channels) {}
    int rate;
    size_t channels;
};

class AudioProcessing {
public:
    struct Config {
        struct NoiseSuppression {
            enum Level { kLow, kModerate, kHigh, kVeryHigh };
            bool enabled = false;
            Level level = kModerate;
        } noise_suppression;
        struct GainController2 {
            struct AdaptiveDigital {
                bool enabled = false;
                float headroom_db = 0, max_gain_db = 0, initial_gain_db = 0;
                float max_gain_change_db_per_second = 0, max_output_noise_level_dbfs = 0;
            } adaptive_digital;
            bool enabled = false;
        } gain_controller2;
    };

    void ApplyConfig(const Config &) {}

    int ProcessStream(const float *const *src, const StreamConfig &, const StreamConfig &, float *const *dst) {
        for (int i = 0; i < 480; i++) dst[0][i] = src[0][i];
        return 0;
    }
};

class AudioProcessingBuilder {
public:
    std::unique_ptr<AudioProcessing> Create() { return std::make_unique<AudioProcessing>(); }
};

}  // namespace webrtc
