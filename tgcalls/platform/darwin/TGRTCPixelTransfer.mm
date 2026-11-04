#import <TargetConditionals.h>
#if TARGET_OS_OSX

#import "TGRTCPixelTransfer.h"

#import <VideoToolbox/VideoToolbox.h>
#import <IOSurface/IOSurface.h>

@implementation TGRTCPixelTransfer {
    VTPixelTransferSessionRef _session;
}

- (void)dealloc {
    [self invalidate];
}

- (void)invalidate {
    if (_session) {
        VTPixelTransferSessionInvalidate(_session);
        CFRelease(_session);
        _session = nullptr;
    }
}

- (BOOL)ensureSession {
    if (_session) {
        return YES;
    }
    if (VTPixelTransferSessionCreate(kCFAllocatorDefault, &_session) != noErr || !_session) {
        _session = nullptr;
        return NO;
    }
    VTSessionSetProperty(_session, kVTPixelTransferPropertyKey_ScalingMode, kVTScalingMode_CropSourceToCleanAperture);
    VTSessionSetProperty(_session, kVTPixelTransferPropertyKey_RealTime, kCFBooleanTrue);
    return YES;
}

- (BOOL)cropAndScaleSource:(CVPixelBufferRef)source cropX:(int)cropX cropY:(int)cropY cropWidth:(int)cropWidth cropHeight:(int)cropHeight destination:(CVPixelBufferRef)destination {
    tgcalls::pixeltransfer::CleanAperture aperture;
    if (!tgcalls::pixeltransfer::computeCleanAperture((int)CVPixelBufferGetWidth(source), (int)CVPixelBufferGetHeight(source), cropX, cropY, cropWidth, cropHeight, aperture)) {
        return NO;
    }
    if (![self ensureSession]) {
        return NO;
    }
    IOSurfaceRef surface = CVPixelBufferGetIOSurface(source);
    CVPixelBufferRef view = nullptr;
    if (surface) {
        if (CVPixelBufferCreateWithIOSurface(kCFAllocatorDefault, surface, nullptr, &view) != kCVReturnSuccess) {
            view = nullptr;
        }
    }
    if (!view) {
        return NO;
    }
    NSDictionary *clean = @{
        (__bridge NSString *)kCVImageBufferCleanApertureWidthKey: @(aperture.width),
        (__bridge NSString *)kCVImageBufferCleanApertureHeightKey: @(aperture.height),
        (__bridge NSString *)kCVImageBufferCleanApertureHorizontalOffsetKey: @(aperture.horizontalOffset),
        (__bridge NSString *)kCVImageBufferCleanApertureVerticalOffsetKey: @(aperture.verticalOffset)
    };
    CVBufferSetAttachment(view, kCVImageBufferCleanApertureKey, (__bridge CFDictionaryRef)clean, kCVAttachmentMode_ShouldNotPropagate);
    OSStatus status = VTPixelTransferSessionTransferImage(_session, view, destination);
    CVBufferRelease(view);
    return status == noErr;
}

@end

#endif // TARGET_OS_OSX
