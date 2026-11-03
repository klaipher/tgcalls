#include "DarwinFFMpeg.h"

#include <TargetConditionals.h>

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
#include "TGLivestreamDecode.h"
#include "TGCallsRuntimeOptions.h"

namespace tgcalls {

#if TARGET_OS_SIMULATOR
#else

namespace {

static enum AVPixelFormat getDarwinPreferredPixelFormat(__unused AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    for (const enum AVPixelFormat *format = pix_fmts; format && *format != AV_PIX_FMT_NONE; format++) {
        if (*format == AV_PIX_FMT_VIDEOTOOLBOX) {
            return AV_PIX_FMT_VIDEOTOOLBOX;
        }
    }
    for (const enum AVPixelFormat *format = pix_fmts; format && *format != AV_PIX_FMT_NONE; format++) {
        AVPixFmtDescriptor const *descriptor = av_pix_fmt_desc_get(*format);
        if (descriptor && !(descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL)) {
            return *format;
        }
    }
    return pix_fmts ? pix_fmts[0] : AV_PIX_FMT_NONE;
}

static void releaseHardwareSession(__unused AVHWDeviceContext *context) {
    livestreamdecode::sharedSessionCounter().release();
}

static livestreamdecode::StreamInfo streamInfoFromContext(AVCodecContext const *codecContext) {
    livestreamdecode::StreamInfo info;
    switch (codecContext->codec_id) {
        case AV_CODEC_ID_H264:
            info.codec = livestreamdecode::Codec::H264;
            if (codecContext->profile >= 110) {
                info.bitDepth = 10;
            }
            break;
        case AV_CODEC_ID_HEVC:
            info.codec = livestreamdecode::Codec::HEVC;
            if (codecContext->profile > 1) {
                info.bitDepth = 10;
            }
            break;
        default:
            break;
    }
    info.width = codecContext->width;
    info.height = codecContext->height;
    info.progressive = codecContext->field_order == AV_FIELD_PROGRESSIVE || codecContext->field_order == AV_FIELD_UNKNOWN;
    AVPixFmtDescriptor const *descriptor = codecContext->pix_fmt != AV_PIX_FMT_NONE ? av_pix_fmt_desc_get(codecContext->pix_fmt) : nullptr;
    if (descriptor) {
        info.bitDepth = descriptor->comp[0].depth;
        info.chroma420 = descriptor->log2_chroma_w == 1 && descriptor->log2_chroma_h == 1;
    }
    return info;
}

}

#endif

void setupDarwinVideoDecoding(AVCodecContext *codecContext) {
#if TARGET_OS_SIMULATOR
    (void)codecContext;
#else
    if (!codecContext || !RuntimeOptions::livestreamHardwareDecode().load()) {
        return;
    }
    if (!livestreamdecode::isEligible(streamInfoFromContext(codecContext))) {
        return;
    }
    if (!livestreamdecode::sharedSessionCounter().tryAcquire()) {
        RTC_LOG(LS_INFO) << "Livestream hardware decode skipped: session limit reached";
        return;
    }
    AVBufferRef *deviceReference = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VIDEOTOOLBOX);
    if (!deviceReference) {
        livestreamdecode::sharedSessionCounter().release();
        return;
    }
    AVHWDeviceContext *deviceContext = (AVHWDeviceContext *)deviceReference->data;
    deviceContext->free = releaseHardwareSession;
    if (av_hwdevice_ctx_init(deviceReference) < 0) {
        av_buffer_unref(&deviceReference);
        RTC_LOG(LS_WARNING) << "Livestream hardware decode unavailable: VideoToolbox device init failed";
        return;
    }
    codecContext->hw_device_ctx = deviceReference;
    codecContext->get_format = getDarwinPreferredPixelFormat;
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
        if (!livestreamdecode::isAcceptableOutputBuffer((int)CVPixelBufferGetPlaneCount(pixelBuffer), is8Bit420 ? 8 : 0, is8Bit420)) {
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
