#import "ScreenCaptureKitConfiguration.h"

#include <cmath>

CGSize TGScreenCaptureOutputSize(CGSize sourcePixelSize, CGSize boundingSize) {
    if (sourcePixelSize.width <= 0.0 || sourcePixelSize.height <= 0.0) {
        return CGSizeMake(2.0, 2.0);
    }
    CGFloat scale = MIN(1.0, MIN(boundingSize.width / sourcePixelSize.width, boundingSize.height / sourcePixelSize.height));
    CGFloat width = MAX(2.0, std::floor(sourcePixelSize.width * scale / 2.0) * 2.0);
    CGFloat height = MAX(2.0, std::floor(sourcePixelSize.height * scale / 2.0) * 2.0);
    return CGSizeMake(width, height);
}

SCStreamConfiguration *TGScreenCaptureStreamConfiguration(CGSize outputSize, double fps, BOOL showsCursor) {
    SCStreamConfiguration *configuration = [[SCStreamConfiguration alloc] init];
    configuration.width = (size_t)outputSize.width;
    configuration.height = (size_t)outputSize.height;
    configuration.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    configuration.colorMatrix = kCGDisplayStreamYCbCrMatrix_ITU_R_601_4;
    configuration.colorSpaceName = kCGColorSpaceSRGB;
    configuration.minimumFrameInterval = CMTimeMake(1, (int32_t)MAX(1.0, std::round(fps)));
    configuration.queueDepth = 4;
    configuration.showsCursor = showsCursor;
    configuration.scalesToFit = YES;
    return configuration;
}

BOOL TGScreenCaptureSampleBufferIsComplete(CMSampleBufferRef sampleBuffer) {
    if (!CMSampleBufferIsValid(sampleBuffer) || CMSampleBufferGetImageBuffer(sampleBuffer) == NULL) {
        return NO;
    }
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    if (attachments == NULL || CFArrayGetCount(attachments) == 0) {
        return NO;
    }
    NSDictionary *info = (__bridge NSDictionary *)CFArrayGetValueAtIndex(attachments, 0);
    NSNumber *status = info[SCStreamFrameInfoStatus];
    return status != nil && status.integerValue == SCFrameStatusComplete;
}

void TGScreenCaptureTagFrame(CVPixelBufferRef pixelBuffer) {
    CFTypeRef matrix = CVBufferCopyAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, NULL);
    if (matrix != NULL) {
        CFRelease(matrix);
        return;
    }
    CVBufferSetAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_601_4, kCVAttachmentMode_ShouldPropagate);
}
