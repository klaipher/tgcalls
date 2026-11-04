#include "DarwinFFMpeg.h"

#import <TargetConditionals.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavcodec/avcodec.h>
}

#import <CoreVideo/CoreVideo.h>

#include "rtc_base/logging.h"

#import "ExtractCVPixelBuffer.h"
#include "group/LivestreamDecodePolicy.h"

namespace tgcalls {

#if !TARGET_OS_SIMULATOR
// FFmpeg offers VideoToolbox first when the hwaccel can be set up. When it cannot (no session
// left, a profile VideoToolbox does not decode), FFmpeg asks again without it; the list then
// holds software formats only, and answering anything outside the list fails the decoder and
// blanks the stream. So VideoToolbox is chosen only if offered, and FFmpeg's own software
// choice otherwise.
static enum AVPixelFormat getDarwinPreferredPixelFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    for (const enum AVPixelFormat *format = pix_fmts; *format != AV_PIX_FMT_NONE; format++) {
        if (*format == AV_PIX_FMT_VIDEOTOOLBOX) {
            return *format;
        }
    }
    return avcodec_default_get_format(ctx, pix_fmts);
}
#endif

void setupDarwinVideoDecoding(AVCodecContext *codecContext) {
    // Software decoding needs no setup. Hardware decoding is opt-in per stream, see
    // setupDarwinHardwareVideoDecoding.
    (void)codecContext;
}

bool darwinSupportsHardwareVideoDecoding() {
#if TARGET_OS_SIMULATOR
    return false;
#else
    return true;
#endif
}

bool setupDarwinHardwareVideoDecoding(AVCodecContext *codecContext) {
#if TARGET_OS_SIMULATOR
    (void)codecContext;
    return false;
#else
    if (!codecContext) {
        return false;
    }
    AVBufferRef *deviceContext = nullptr;
    int result = av_hwdevice_ctx_create(&deviceContext, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0);
    if (result < 0 || !deviceContext) {
        return false;
    }
    // The codec context owns this reference and releases it in avcodec_free_context.
    codecContext->hw_device_ctx = deviceContext;
    codecContext->get_format = getDarwinPreferredPixelFormat;
    return true;
#endif
}

webrtc::scoped_refptr<webrtc::VideoFrameBuffer> createDarwinPlatformFrameFromData(AVFrame const *frame) {
    if (!frame) {
        return nullptr;
    }
    if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX && frame->data[3]) {
        CVPixelBufferRef pixelBuffer = (CVPixelBufferRef)(void *)frame->data[3];
        OSType pixelFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
        bool is8Bit420 = pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange || pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
        if (!isAcceptableOutputBuffer((int)CVPixelBufferGetPlaneCount(pixelBuffer), is8Bit420 ? 8 : 0, is8Bit420)) {
            RTC_LOG(LS_WARNING) << "Livestream hardware decode produced an unsupported pixel format: " << pixelFormat;
            return nullptr;
        }
        CFTypeRef matrix = CVBufferCopyAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, nullptr);
        if (matrix) {
            CFRelease(matrix);
        } else {
            CVBufferSetAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, frame->height >= 720 ? kCVImageBufferYCbCrMatrix_ITU_R_709_2 : kCVImageBufferYCbCrMatrix_ITU_R_601_4, kCVAttachmentMode_ShouldPropagate);
        }
        return extractCVPixelBuffer((void *)pixelBuffer);
    } else {
        return nullptr;
    }
}

}
