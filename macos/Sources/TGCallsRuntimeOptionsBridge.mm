#import <TgVoipWebrtc/TGCallsRuntimeOptionsBridge.h>

#include "platform/darwin/TGCallsRuntimeOptions.h"
#include "CallAudioPolicy.h"
#include "modules/audio_device/mac/audio_device_mac_options.h"

@implementation TGCallsRuntimeOptionsBridge

+ (void)setLivestreamHardwareDecodeEnabled:(BOOL)enabled {
    tgcalls::RuntimeOptions::livestreamHardwareDecode().store(enabled);
}

+ (void)setCameraFormatV2Enabled:(BOOL)enabled {
    tgcalls::RuntimeOptions::cameraFormatV2().store(enabled);
}

+ (void)setHardwareCropEnabled:(BOOL)enabled {
    tgcalls::RuntimeOptions::hardwareCrop().store(enabled);
}

+ (void)setNativeDecodeFormatEnabled:(BOOL)enabled {
    tgcalls::RuntimeOptions::nativeDecodeFormat().store(enabled);
}

+ (void)setScreencastHardwareEncoderEnabled:(BOOL)enabled {
    tgcalls::RuntimeOptions::screencastHardwareEncoder().store(enabled);
}

+ (void)setGroupNoiseMode:(NSInteger)mode {
    tgcalls::audiopolicy::RuntimeAudioOptions::groupNoiseMode().store((int)mode);
}

+ (void)setMicGainMode:(NSInteger)mode {
    tgcalls::audiopolicy::RuntimeAudioOptions::micGainMode().store((int)mode);
}

+ (void)setOpusComplexity:(NSInteger)complexity {
    tgcalls::audiopolicy::RuntimeAudioOptions::opusComplexity().store((int)complexity);
}

+ (void)setLowBitrateApmRateEnabled:(BOOL)enabled {
    tgcalls::audiopolicy::RuntimeAudioOptions::lowBitrateApmRate().store(enabled);
}

+ (void)setAudioWorkgroupEnabled:(BOOL)enabled {
    webrtc::AudioDeviceMacOptions::joinDeviceWorkgroup().store(enabled);
}

@end
