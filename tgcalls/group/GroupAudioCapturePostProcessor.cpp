#include "group/GroupAudioCapturePostProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "CallsRuntimeSwitches.h"
#include "common_audio/resampler/push_sinc_resampler.h"
#include "modules/audio_processing/audio_buffer.h"

#if USE_RNNOISE
#include "rnnoise.h"
#endif

namespace tgcalls {

VadHistory::VadHistory() {
    for (int i = 0; i < kLength; i++) {
        _vadResultHistory[i] = 0.0f;
    }
}

bool VadHistory::update(float vadProbability) {
    for (int i = 1; i < kLength; i++) {
        _vadResultHistory[i - 1] = _vadResultHistory[i];
    }
    _vadResultHistory[kLength - 1] = vadProbability;

    float movingAverage = 0.0f;
    for (int i = 0; i < kLength; i++) {
        movingAverage += _vadResultHistory[i];
    }
    movingAverage /= (float)kLength;

    bool vadResult = false;
    if (movingAverage > 0.8f) {
        vadResult = true;
    }

    return vadResult;
}

bool PeakSpeechDetector::update(float framePeak) {
    if (!_hasFloor || framePeak < _floor) {
        _floor = framePeak;
        _hasFloor = true;
    } else {
        _floor = std::max(_floor * kFloorRisePerFrame, 1.0f);
    }
    return framePeak >= std::max(kMinimumSpeechPeak, _floor * kSpeechToFloorRatio);
}

#if USE_RNNOISE

AudioCapturePostProcessor::AudioCapturePostProcessor(std::function<void(GroupLevelValue const &)> updated, std::shared_ptr<NoiseSuppressionConfiguration> noiseSuppressionConfiguration, std::vector<float> *externalAudioSamples, webrtc::Mutex *externalAudioSamplesMutex) :
_updated(updated),
_noiseSuppressionConfiguration(noiseSuppressionConfiguration),
_externalAudioSamples(externalAudioSamples),
_externalAudioSamplesMutex(externalAudioSamplesMutex) {
    int frameSize = rnnoise_get_frame_size();
    _frameSamples.resize(frameSize);

    _denoiseState = rnnoise_create(nullptr);
}

AudioCapturePostProcessor::~AudioCapturePostProcessor() {
    if (_denoiseState) {
        rnnoise_destroy(_denoiseState);
    }
}

void AudioCapturePostProcessor::Initialize(int sample_rate_hz, int num_channels) {
}

void AudioCapturePostProcessor::Process(webrtc::AudioBuffer *buffer) {
    if (!buffer) {
        return;
    }
    if (buffer->num_channels() != 1) {
        return;
    }
    if (!_denoiseState) {
        return;
    }

    size_t captureFrames = buffer->num_frames();
    size_t processingFrames = _frameSamples.size();
    if (captureFrames == processingFrames) {
        processAtProcessingRate(buffer->channels()[0]);
        return;
    }

    if (!_toProcessingRate || _resamplerCaptureFrames != captureFrames) {
        _toProcessingRate = std::make_unique<webrtc::PushSincResampler>(captureFrames, processingFrames);
        _fromProcessingRate = std::make_unique<webrtc::PushSincResampler>(processingFrames, captureFrames);
        _processingSamples.assign(processingFrames, 0.0f);
        _resamplerCaptureFrames = captureFrames;
    }
    _toProcessingRate->Resample(buffer->channels()[0], captureFrames, _processingSamples.data(), processingFrames);
    const bool modified = processAtProcessingRate(_processingSamples.data());
    // Narrowband capture (8/16 kHz, Bluetooth headsets) keeps the processed signal (always on; it used to be
    // dropped, only feeding the speech detector). While the round trip is on, every frame goes through both
    // resamplers (including the ones RNNoise skipped for being quiet), so the delay is constant and the
    // resampler history never goes stale.
    const bool roundTrip = modified || (_noiseSuppressionConfiguration && _noiseSuppressionConfiguration->isEnabled);
    if (roundTrip) {
        _fromProcessingRate->Resample(_processingSamples.data(), processingFrames, buffer->channels()[0], captureFrames);
    }
}

bool AudioCapturePostProcessor::processAtProcessingRate(float *samples) {
    size_t frames = _frameSamples.size();
    bool modified = false;

    float sourcePeak = 0.0f;
    for (size_t i = 0; i < frames; i++) {
        sourcePeak = std::max(std::fabs(samples[i]), sourcePeak);
    }

    if (_noiseSuppressionConfiguration) {
        float vadProbability = 0.0f;
        if (_noiseSuppressionConfiguration->isEnabled) {
            if (sourcePeak >= 0.01f) {
                vadProbability = rnnoise_process_frame(_denoiseState, _frameSamples.data(), samples);
                memcpy(samples, _frameSamples.data(), frames * sizeof(float));
                modified = true;
            }
        } else {
            vadProbability = _peakSpeechDetector.update(sourcePeak) ? 1.0f : 0.0f;
        }

        float peak = 0;
        for (size_t i = 0; i < frames; i++) {
            peak = std::max(std::fabs(samples[i]), peak);
        }

        bool vadStatus = _history.update(vadProbability);

        _peakCount += (int32_t)frames;
        if (_peak < peak) {
            _peak = peak;
        }
        if (_peakCount >= 4400) {
            float level = _peak / 4000.0f;
            _peak = 0;
            _peakCount = 0;

            _updated(GroupLevelValue{
                level,
                vadStatus,
            });
        }
    } else {
        float peak = sourcePeak;

        _peakCount += (int32_t)frames;
        if (_peak < peak) {
            _peak = peak;
        }
        if (_peakCount >= 1200) {
            float level = _peak / 8000.0f;
            _peak = 0;
            _peakCount = 0;

            _updated(GroupLevelValue{
                level,
                level >= 1.0f,
            });
        }
    }

    if (_externalAudioSamplesMutex && _externalAudioSamples) {
        _externalAudioSamplesMutex->Lock();
        if (!_externalAudioSamples->empty()) {
            size_t takenSamples = 0;
            for (size_t i = 0; i < _externalAudioSamples->size() && i < frames; i++) {
                float sample = (*_externalAudioSamples)[i];
                sample += samples[i];
                sample = std::min(sample, 32768.f);
                sample = std::max(sample, -32768.f);
                samples[i] = sample;
                takenSamples++;
            }
            if (takenSamples != 0) {
                _externalAudioSamples->erase(_externalAudioSamples->begin(), _externalAudioSamples->begin() + takenSamples);
                modified = true;
            }
        }
        _externalAudioSamplesMutex->Unlock();
    }

    return modified;
}

std::string AudioCapturePostProcessor::ToString() const {
    return "CustomPostProcessing";
}

void AudioCapturePostProcessor::SetRuntimeSetting(webrtc::AudioProcessing::RuntimeSetting setting) {
}

#endif

} // namespace tgcalls
