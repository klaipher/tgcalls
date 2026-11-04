#import <TgVoipWebrtc/TGCallsRuntimeOptionsBridge.h>

#include "CallsRuntimeSwitches.h"
#include "VideoCaptureInterfaceImpl.h"
#include "StaticThreads.h"
#include "modules/audio_device/mac/audio_device_mac_options.h"

@implementation TGCallsRuntimeOptionsBridge

+ (void)setLivestreamHardwareDecodingEnabled:(BOOL)enabled {
    tgcalls::CallsRuntimeSwitches::setLivestreamHardwareDecoding(enabled);
}

+ (void)setHardwareCropEnabled:(BOOL)enabled {
    tgcalls::CallsRuntimeSwitches::setHardwareCrop(enabled);
}

+ (void)setNativeDecodeFormatEnabled:(BOOL)enabled {
    tgcalls::CallsRuntimeSwitches::setNativeDecodeFormat(enabled);
}

+ (void)setScreencastHardwareEncoderEnabled:(BOOL)enabled {
    tgcalls::CallsRuntimeSwitches::setScreencastHardwareEncoder(enabled);
}

+ (void)setGroupNoiseMode:(NSInteger)mode {
    tgcalls::CallsRuntimeSwitches::setGroupNoiseMode((int)mode);
}

+ (void)setAudioWorkgroupEnabled:(BOOL)enabled {
    webrtc::AudioDeviceMacOptions::joinDeviceWorkgroup().store(enabled);
}

+ (void)setThreadQoSEnabled:(BOOL)enabled {
    tgcalls::CallsRuntimeSwitches::setThreadQoS(enabled);
    tgcalls::StaticThreads::applyQoS();
}

+ (void)setThermalVideoLadderEnabled:(BOOL)enabled {
    tgcalls::CallsRuntimeSwitches::setThermalVideoLadder(enabled);
    tgcalls::VideoCaptureInterfaceImpl::setThermalLevelForAll(tgcalls::CallsRuntimeSwitches::thermalLevel());
}

+ (void)setThermalLevel:(NSInteger)level {
    tgcalls::VideoCaptureInterfaceImpl::setThermalLevelForAll((int)level);
}

+ (void)setLivestreamVideoSuspended:(BOOL)suspended {
    tgcalls::CallsRuntimeSwitches::setLivestreamVideoSuspended(suspended);
}

@end
