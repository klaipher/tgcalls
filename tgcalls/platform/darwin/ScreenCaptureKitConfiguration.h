#ifndef TGCALLS_SCREEN_CAPTURE_KIT_CONFIGURATION_H
#define TGCALLS_SCREEN_CAPTURE_KIT_CONFIGURATION_H

#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

NS_ASSUME_NONNULL_BEGIN

CGSize TGScreenCaptureOutputSize(CGSize sourcePixelSize, CGSize boundingSize);
SCStreamConfiguration *TGScreenCaptureStreamConfiguration(CGSize outputSize, double fps, BOOL showsCursor) API_AVAILABLE(macos(14.0));
BOOL TGScreenCaptureSampleBufferIsComplete(CMSampleBufferRef sampleBuffer) API_AVAILABLE(macos(14.0));
void TGScreenCaptureTagFrame(CVPixelBufferRef pixelBuffer) API_AVAILABLE(macos(12.0));

NS_ASSUME_NONNULL_END

#endif
