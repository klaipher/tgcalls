#ifndef TGCALLS_GROUP_AUDIO_CAPTURE_POST_PROCESSOR_H
#define TGCALLS_GROUP_AUDIO_CAPTURE_POST_PROCESSOR_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "group/GroupInstanceImpl.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "rtc_base/synchronization/mutex.h"

#ifndef USE_RNNOISE
#define USE_RNNOISE 1
#endif

struct DenoiseState;

namespace webrtc {
class AudioBuffer;
class PushSincResampler;
} // namespace webrtc

namespace tgcalls {

// Moved verbatim out of GroupInstanceCustomImpl.cpp's anonymous namespace, with
// two deliberate exceptions, neither of which changes behavior:
//   - AudioCapturePostProcessor carried an unused `SparseVad _vad;` member. It
//     is dropped. SparseVad itself stays in GroupInstanceCustomImpl.cpp, where
//     it is now unreferenced.
//   - VadHistory's empty user-declared destructor is dropped; the implicit one
//     is equivalent.
// The file-scope constant kVadResultHistoryLength became VadHistory::kLength.

// Moving average of the last 8 RNNoise VAD probabilities, thresholded at 0.8.
class VadHistory {
public:
    VadHistory();
    bool update(float vadProbability);

private:
    static const int kLength = 8;
    float _vadResultHistory[kLength];
};

class PeakSpeechDetector {
public:
    bool update(float framePeak);

private:
    static constexpr float kMinimumSpeechPeak = 1000.0f;
    static constexpr float kSpeechToFloorRatio = 2.0f;
    static constexpr float kFloorRisePerFrame = 1.005f;

    bool _hasFloor = false;
    float _floor = 0.0f;
};

struct NoiseSuppressionConfiguration {
    NoiseSuppressionConfiguration(bool isEnabled_) :
    isEnabled(isEnabled_) {

    }

    bool isEnabled = false;
};

#if USE_RNNOISE
// Capture-side APM post-processor: computes the local audio level and speech
// state (reported through `updated`) and optionally applies RNNoise denoising.
// `externalAudioSamples`/`externalAudioSamplesMutex` may be null; when set, the
// samples are mixed into the capture buffer (screencast audio injection).
class AudioCapturePostProcessor : public webrtc::CustomProcessing {
public:
    AudioCapturePostProcessor(std::function<void(GroupLevelValue const &)> updated,
                              std::shared_ptr<NoiseSuppressionConfiguration> noiseSuppressionConfiguration,
                              std::vector<float> *externalAudioSamples,
                              webrtc::Mutex *externalAudioSamplesMutex);
    ~AudioCapturePostProcessor() override;

private:
    void Initialize(int sample_rate_hz, int num_channels) override;
    void Process(webrtc::AudioBuffer *buffer) override;
    std::string ToString() const override;
    void SetRuntimeSetting(webrtc::AudioProcessing::RuntimeSetting setting) override;

    bool processAtProcessingRate(float *samples);

    std::function<void(GroupLevelValue const &)> _updated;
    std::shared_ptr<NoiseSuppressionConfiguration> _noiseSuppressionConfiguration;

    DenoiseState *_denoiseState = nullptr;
    std::unique_ptr<webrtc::PushSincResampler> _toProcessingRate;
    std::unique_ptr<webrtc::PushSincResampler> _fromProcessingRate;
    size_t _resamplerCaptureFrames = 0;
    std::vector<float> _processingSamples;
    std::vector<float> _frameSamples;
    int32_t _peakCount = 0;
    float _peak = 0;
    VadHistory _history;
    PeakSpeechDetector _peakSpeechDetector;

    std::vector<float> *_externalAudioSamples = nullptr;
    webrtc::Mutex *_externalAudioSamplesMutex = nullptr;
};
#endif

} // namespace tgcalls

#endif
