#ifndef TGCALLS_SCREEN_CAPTURE_KIT_CAPTURER_H
#define TGCALLS_SCREEN_CAPTURE_KIT_CAPTURER_H
#ifndef WEBRTC_IOS
#import <Foundation/Foundation.h>

#import "api/media_stream_interface.h"
#import "tgcalls/platform/darwin/VideoCameraCapturerMac.h"
#import "tgcalls/desktop_capturer/DesktopCaptureSource.h"

API_AVAILABLE(macos(14.0))
@interface ScreenCaptureKitCapturer : NSObject<CapturerInterface>

+ (bool)isEnabled;

- (instancetype)initWithSource:(rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>)trackSource captureSource:(tgcalls::DesktopCaptureSource)captureSource;

@end

#endif
#endif
