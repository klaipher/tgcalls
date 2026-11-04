#include "DarwinInterface.h"
#include "DarwinCodecSupport.h"
#include "CallsRuntimeSwitches.h"
#include "rtc_base/logging.h"

#include "VideoCapturerInterfaceImpl.h"
#include "sdk/objc/native/src/objc_video_track_source.h"
#include "sdk/objc/native/api/network_monitor_factory.h"

#include "media/base/media_constants.h"
#include "TGRTCDefaultVideoEncoderFactory.h"
#include "TGRTCDefaultVideoDecoderFactory.h"
#include "sdk/objc/native/api/video_encoder_factory.h"
#include "sdk/objc/native/api/video_decoder_factory.h"
#include "pc/video_track_source_proxy.h"
#import "base/RTCLogging.h"
#include "AudioDeviceModuleIOS.h"
#include "AudioDeviceModuleMacos.h"
#include "DarwinVideoSource.h"
#include "objc_video_encoder_factory.h"
#include "objc_video_decoder_factory.h"
#include "sdk/objc/native/src/objc_frame_buffer.h"
#import "sdk/objc/components/video_frame_buffer/RTCCVPixelBuffer.h"

#import "DarwinFFMpeg.h"

#ifdef WEBRTC_IOS
#include "platform/darwin/iOS/RTCAudioSession.h"
#include "platform/darwin/iOS/RTCAudioSessionConfiguration.h"
#import <UIKit/UIKit.h>
#endif // WEBRTC_IOS

#import <AVFoundation/AVFoundation.h>

#include <sys/sysctl.h>

namespace tgcalls {

std::unique_ptr<webrtc::VideoDecoderFactory> CustomObjCToNativeVideoDecoderFactory(
    id<RTC_OBJC_TYPE(RTCVideoDecoderFactory)> objc_video_decoder_factory) {
    return std::make_unique<webrtc::CustomObjCVideoDecoderFactory>(objc_video_decoder_factory);
}

static DarwinVideoTrackSource *getObjCVideoSource(const webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> nativeSource) {
    webrtc::VideoTrackSourceProxy *proxy_source =
    static_cast<webrtc::VideoTrackSourceProxy *>(nativeSource.get());
    return static_cast<DarwinVideoTrackSource *>(proxy_source->internal());
}

[[maybe_unused]] static NSString *getPlatformInfo() {
    const char *typeSpecifier = "hw.machine";
    
    size_t size;
    sysctlbyname(typeSpecifier, NULL, &size, NULL, 0);
    
    char *answer = (char *)malloc(size);
    sysctlbyname(typeSpecifier, answer, &size, NULL, 0);
    
    NSString *results = [NSString stringWithCString:answer encoding:NSUTF8StringEncoding];
    
    free(answer);
    return results;
}

std::unique_ptr<rtc::NetworkMonitorFactory> DarwinInterface::createNetworkMonitorFactory() {
    return webrtc::CreateNetworkMonitorFactory();
}

void DarwinInterface::configurePlatformAudio(int numChanels) {
}

std::unique_ptr<webrtc::VideoEncoderFactory> DarwinInterface::makeVideoEncoderFactory(bool preferHardwareEncoding, bool isScreencast) {
    auto nativeFactory = std::make_unique<webrtc::CustomObjCVideoEncoderFactory>([[TGRTCDefaultVideoEncoderFactory alloc] initWithPreferHardwareH264:preferHardwareEncoding preferX264:false]);
    if (!preferHardwareEncoding) {
        bool hardwareForSecondary = !isScreencast;
#if TARGET_OS_OSX
        if (isScreencast && CallsRuntimeSwitches::screencastHardwareEncoder()) {
            hardwareForSecondary = true;
            RTC_LOG(LS_INFO) << "Screen share video uses the VideoToolbox encoder (debug option)";
        }
#endif
        auto nativeHardwareFactory = std::make_unique<webrtc::CustomObjCVideoEncoderFactory>([[TGRTCDefaultVideoEncoderFactory alloc] initWithPreferHardwareH264:hardwareForSecondary preferX264:false]);
        return std::make_unique<webrtc::SimulcastVideoEncoderFactory>(std::move(nativeFactory), std::move(nativeHardwareFactory));
    }
    return nativeFactory;
}

std::unique_ptr<webrtc::VideoDecoderFactory> DarwinInterface::makeVideoDecoderFactory() {
    return CustomObjCToNativeVideoDecoderFactory([[TGRTCDefaultVideoDecoderFactory alloc] init]);
}

// Which codecs this device offers a legacy (v1) peer as encoders. The decision is DarwinCodecSupport.h (host-tested in
// tests/darwin_codec_support): H.264 is always offered, which makes v1 calls on arm64 use the hardware VideoToolbox
// encoder (the old rule said NO on arm64, a leftover of early M1 Macs that leaked to iOS); VP8 and VP9 stay offered.
bool DarwinInterface::supportsEncoding(const std::string &codecName) {
    DarwinEncodingFacts facts;
#ifdef WEBRTC_IOS
    facts.isIos = true;
#ifndef WEBRTC_DISABLE_H265
    if (codecName == cricket::kH265CodecName) {
        // Only asked for H.265: the export preset list is not free to build.
        facts.hevcPresetAvailable = [[AVAssetExportSession allExportPresets] containsObject:AVAssetExportPresetHEVCHighestQuality];
    }
#endif
#endif
#ifdef __x86_64__
    facts.isX86_64 = true;
#endif
#ifndef WEBRTC_DISABLE_H265
    facts.h265Compiled = true;
#endif
    return darwinSupportsEncoding(codecName, facts);
}

webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> DarwinInterface::makeVideoSource(rtc::Thread *signalingThread, rtc::Thread *workerThread) {
    webrtc::scoped_refptr<tgcalls::DarwinVideoTrackSource> objCVideoTrackSource(new rtc::RefCountedObject<tgcalls::DarwinVideoTrackSource>());
    return webrtc::VideoTrackSourceProxy::Create(signalingThread, workerThread, objCVideoTrackSource);
}

void DarwinInterface::adaptVideoSource(webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> videoSource, int width, int height, int fps) {
    getObjCVideoSource(videoSource)->OnOutputFormatRequest(width, height, fps);
}

std::unique_ptr<VideoCapturerInterface> DarwinInterface::makeVideoCapturer(webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> source, std::string deviceId, std::function<void(VideoState)> stateUpdated, std::function<void(PlatformCaptureInfo)> captureInfoUpdated, std::shared_ptr<PlatformContext> platformContext, std::pair<int, int> &outResolution) {
    return std::make_unique<VideoCapturerInterfaceImpl>(source, deviceId, stateUpdated, captureInfoUpdated, outResolution);
}

webrtc::scoped_refptr<WrappedAudioDeviceModule> DarwinInterface::wrapAudioDeviceModule(webrtc::scoped_refptr<webrtc::AudioDeviceModule> module) {
#ifdef WEBRTC_IOS
    return rtc::make_ref_counted<AudioDeviceModuleIOS>(module);
#else
    return rtc::make_ref_counted<AudioDeviceModuleMacos>(module);
#endif
}

void DarwinInterface::setupVideoDecoding(AVCodecContext *codecContext) {
    return setupDarwinVideoDecoding(codecContext);
}

bool DarwinInterface::supportsHardwareVideoDecoding() {
    return darwinSupportsHardwareVideoDecoding();
}

bool DarwinInterface::setupHardwareVideoDecoding(AVCodecContext *codecContext) {
    return setupDarwinHardwareVideoDecoding(codecContext);
}

webrtc::scoped_refptr<webrtc::VideoFrameBuffer> DarwinInterface::createPlatformFrameFromData(AVFrame const *frame) {
    return createDarwinPlatformFrameFromData(frame);
}

std::unique_ptr<PlatformInterface> CreatePlatformInterface() {
	return std::make_unique<DarwinInterface>();
}

DarwinVideoFrame::DarwinVideoFrame(CVPixelBufferRef pixelBuffer) {
    _pixelBuffer = CVPixelBufferRetain(pixelBuffer);
}

DarwinVideoFrame::~DarwinVideoFrame() {
    if (_pixelBuffer) {
        CVPixelBufferRelease(_pixelBuffer);
    }
}

} // namespace tgcalls
