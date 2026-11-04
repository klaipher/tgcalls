#import <TgVoipWebrtc/TGCallsRuntimeOptionsBridge.h>

#include "CallsRuntimeSwitches.h"
#include "VideoCaptureInterfaceImpl.h"

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

+ (void)setThermalVideoLadderEnabled:(BOOL)enabled {
    tgcalls::CallsRuntimeSwitches::setThermalVideoLadder(enabled);
    tgcalls::VideoCaptureInterfaceImpl::reapplyOutputFormatForAll();
}

+ (void)setThermalLevel:(NSInteger)level {
    tgcalls::VideoCaptureInterfaceImpl::setThermalLevelForAll((int)level);
}

+ (void)setLivestreamVideoSuspended:(BOOL)suspended {
    tgcalls::CallsRuntimeSwitches::setLivestreamVideoSuspended(suspended);
}

@end
