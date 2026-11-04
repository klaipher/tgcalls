#ifndef TGRTC_SCREEN_VIDEO_RANGE_H
#define TGRTC_SCREEN_VIDEO_RANGE_H

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <VideoToolbox/VideoToolbox.h>

NS_ASSUME_NONNULL_BEGIN

OSType TGRTCI420CopyPixelFormat(void);
void TGRTCTagLimitedRangeBT601(CVPixelBufferRef pixelBuffer);
CFStringRef _Nullable TGRTCCopyFrameYCbCrMatrix(CVPixelBufferRef pixelBuffer) CF_RETURNS_RETAINED;
BOOL TGRTCUpdateSessionColorMatrix(VTCompressionSessionRef session, CVPixelBufferRef pixelBuffer, CFStringRef _Nullable * _Nonnull currentMatrix);

NS_ASSUME_NONNULL_END

#endif
