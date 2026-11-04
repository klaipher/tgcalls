#ifndef TGCALLS_SYSTEM_VOICE_PROCESSING_FALLBACK_H
#define TGCALLS_SYSTEM_VOICE_PROCESSING_FALLBACK_H

#include "AudioCallPolicy.h"

#include <atomic>
#include <memory>

#include "modules/audio_processing/include/audio_processing.h"
#include "call/audio_state.h"
#include "rtc_base/logging.h"
#include "rtc_base/thread.h"

namespace tgcalls {

// A call that dropped WebRTC's noise suppression because the system voice-processing unit was expected to
// do the work has no noise suppression at all if that unit then fails (it cannot be initialised, gives up
// after its retries, or runs in bypass). The unit starts after the channel options are chosen, so the
// failure arrives too late for the options. This gives the software stage back on the live call: it
// subscribes to SystemVoiceProcessingLatch and, on a failure, re-enables noise suppression in the audio
// processing module on the worker thread.
//
// Create it only when the options dropped the software stage. Destroy it before the audio state goes away;
// the destructor waits for the worker thread, so no task touches the audio state afterwards.
class SoftwareNoiseSuppressionFallback {
public:
    SoftwareNoiseSuppressionFallback(rtc::Thread *workerThread, webrtc::AudioState *audioState, const char *callKind) :
    _workerThread(workerThread),
    _alive(std::make_shared<std::atomic<bool>>(true)) {
        auto alive = _alive;
        std::string kind = callKind;
        _observerId = SystemVoiceProcessingLatch::addObserver([workerThread, audioState, alive, kind]() {
            workerThread->PostTask([audioState, alive, kind]() {
                if (!alive->load()) {
                    return;
                }
                webrtc::AudioProcessing *processing = audioState ? audioState->audio_processing() : nullptr;
                if (!processing) {
                    return;
                }
                auto config = processing->GetConfig();
                if (config.noise_suppression.enabled) {
                    return;
                }
                config.noise_suppression.enabled = true;
                config.noise_suppression.level = webrtc::AudioProcessing::Config::NoiseSuppression::Level::kHigh;
                processing->ApplyConfig(config);
                RTC_LOG(LS_WARNING) << kind << " audio: the system voice-processing unit failed, software noise suppression is back on";
            });
        });
    }

    ~SoftwareNoiseSuppressionFallback() {
        SystemVoiceProcessingLatch::removeObserver(_observerId);
        // Anything already posted runs before this returns; after it the flag is false.
        auto alive = _alive;
        _workerThread->BlockingCall([alive]() {
            alive->store(false);
        });
    }

    SoftwareNoiseSuppressionFallback(const SoftwareNoiseSuppressionFallback &) = delete;
    SoftwareNoiseSuppressionFallback &operator=(const SoftwareNoiseSuppressionFallback &) = delete;

private:
    rtc::Thread *_workerThread = nullptr;
    std::shared_ptr<std::atomic<bool>> _alive;
    int _observerId = 0;
};

} // namespace tgcalls

#endif
