#import <TgVoipWebrtc/TGCallsRuntimeOptionsBridge.h>

#include "platform/darwin/TGCallsRuntimeOptions.h"

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

@end
