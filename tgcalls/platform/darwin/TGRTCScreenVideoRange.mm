#import "TGRTCScreenVideoRange.h"

OSType TGRTCI420CopyPixelFormat(void) {
    return kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
}

void TGRTCTagLimitedRangeBT601(CVPixelBufferRef pixelBuffer) {
    CVBufferSetAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_601_4, kCVAttachmentMode_ShouldPropagate);
}

CFStringRef TGRTCCopyFrameYCbCrMatrix(CVPixelBufferRef pixelBuffer) {
    CFTypeRef matrix = NULL;
    if (@available(macOS 12.0, *)) {
        matrix = CVBufferCopyAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, NULL);
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        matrix = CVBufferGetAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, NULL);
#pragma clang diagnostic pop
        if (matrix != NULL) {
            CFRetain(matrix);
        }
    }
    if (matrix == NULL) {
        return NULL;
    }
    if (CFGetTypeID(matrix) != CFStringGetTypeID()) {
        CFRelease(matrix);
        return NULL;
    }
    return (CFStringRef)matrix;
}

BOOL TGRTCUpdateSessionColorMatrix(VTCompressionSessionRef session, CVPixelBufferRef pixelBuffer, CFStringRef *currentMatrix) {
    CFStringRef matrix = TGRTCCopyFrameYCbCrMatrix(pixelBuffer);
    if (matrix == NULL) {
        return NO;
    }
    if (*currentMatrix != NULL && CFEqual(*currentMatrix, matrix)) {
        CFRelease(matrix);
        return NO;
    }
    VTSessionSetProperty(session, kVTCompressionPropertyKey_YCbCrMatrix, matrix);
    BOOL changed = *currentMatrix != NULL;
    if (*currentMatrix != NULL) {
        CFRelease(*currentMatrix);
    }
    *currentMatrix = matrix;
    return changed;
}
