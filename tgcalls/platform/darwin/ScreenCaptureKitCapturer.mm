#import <TargetConditionals.h>
#if TARGET_OS_OSX

#import "ScreenCaptureKitCapturer.h"

#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <AppKit/AppKit.h>

#include "rtc_base/time_utils.h"
#include "pc/video_track_source_proxy.h"
#include "sdk/objc/native/src/objc_frame_buffer.h"
#import "base/RTCVideoFrame.h"
#import "components/video_frame_buffer/RTCCVPixelBuffer.h"

#import "ScreenCaptureKitConfiguration.h"
#include "ScreenShareResendPolicy.h"
#import "TGRTCCVPixelBuffer.h"
#include "DarwinVideoSource.h"

static NSString *const TGScreenCaptureLegacyDefaultsKey = @"tgcalls.useLegacyScreenCapture";
static const CGSize TGScreenCaptureBoundingSize = { 1920.0, 1080.0 };
static const double TGScreenCaptureFramesPerSecond = 25.0;

static int64_t TGScreenCaptureHostClockNs() {
    CMTime now = CMClockGetTime(CMClockGetHostTimeClock());
    return (int64_t)(CMTimeGetSeconds(now) * (double)rtc::kNumNanosecsPerSec);
}

static tgcalls::DarwinVideoTrackSource *getScreenCaptureVideoSource(const rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> nativeSource) {
    webrtc::VideoTrackSourceProxy *proxySource = static_cast<webrtc::VideoTrackSourceProxy *>(nativeSource.get());
    return static_cast<tgcalls::DarwinVideoTrackSource *>(proxySource->internal());
}

@interface ScreenCaptureKitCapturer () <SCStreamOutput, SCStreamDelegate>
@end

@implementation ScreenCaptureKitCapturer {
    rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> _trackSource;
    long long _sourceId;
    bool _sourceIsWindow;
    std::shared_ptr<rtc::VideoSinkInterface<webrtc::VideoFrame>> _uncroppedSink;
    std::function<void()> _onFatalError;
    std::function<void(bool)> _onPause;
    dispatch_queue_t _queue;
    SCStream *_stream;
    // Touched only on _queue: the stream whose frames are accepted. Samples that a stopped stream had already
    // queued on _queue arrive after the stop block and must not repopulate _lastVideoFrame or reach the encoder.
    SCStream *_activeStream;
    NSInteger _generation;
    bool _isRunning;
    bool _isPaused;
    // Touched on _queue only. ScreenCaptureKit sends a frame only when the screen changed, so the last one is
    // sent again while the screen stands still (see StillScreenRepeater).
    tgcalls::StillScreenRepeater _repeater;
    RTCVideoFrame *_lastVideoFrame;
    dispatch_source_t _repeatTimer;
}

+ (bool)isEnabled {
    return ![[NSUserDefaults standardUserDefaults] boolForKey:TGScreenCaptureLegacyDefaultsKey];
}

- (instancetype)initWithSource:(rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>)trackSource captureSource:(tgcalls::DesktopCaptureSource)captureSource {
    self = [super init];
    if (self != nil) {
        _trackSource = trackSource;
        _sourceId = captureSource.uniqueId();
        _sourceIsWindow = captureSource.isWindow();
        _queue = dispatch_queue_create("org.telegram.tgcalls.ScreenCaptureKitCapturer", DISPATCH_QUEUE_SERIAL);
    }
    return self;
}

- (void)dealloc {
    if (_repeatTimer != nil) {
        dispatch_source_cancel(_repeatTimer);
    }
    [_stream stopCaptureWithCompletionHandler:nil];
}

- (void)setOnFatalError:(std::function<void ()>)error {
    _onFatalError = error;
}

- (void)setOnPause:(std::function<void (bool)>)pause {
    _onPause = pause;
}

- (void)setUncroppedSink:(std::shared_ptr<rtc::VideoSinkInterface<webrtc::VideoFrame>>)sink {
    dispatch_async(_queue, ^{
        self->_uncroppedSink = sink;
    });
}

- (void)setPreferredCaptureAspectRatio:(float)aspectRatio {
}

- (void)setIsEnabled:(bool)isEnabled {
    if (_isPaused == !isEnabled) {
        return;
    }
    _isPaused = !isEnabled;
    if (isEnabled) {
        [self start];
    } else {
        [self stop];
    }
}

- (void)start {
    if (_isRunning) {
        return;
    }
    _isRunning = true;
    _generation += 1;
    NSInteger generation = _generation;
    long long uniqueId = _sourceId;
    bool isWindow = _sourceIsWindow;
    __weak ScreenCaptureKitCapturer *weakSelf = self;
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO onScreenWindowsOnly:NO completionHandler:^(SCShareableContent *content, NSError *error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            ScreenCaptureKitCapturer *strongSelf = weakSelf;
            if (strongSelf == nil || strongSelf->_generation != generation || !strongSelf->_isRunning) {
                return;
            }
            [strongSelf startWithContent:content uniqueId:uniqueId isWindow:isWindow];
        });
    }];
}

- (void)startWithContent:(SCShareableContent *)content uniqueId:(long long)uniqueId isWindow:(bool)isWindow {
    SCContentFilter *filter = nil;
    CGSize sourcePixelSize = CGSizeZero;
    if (isWindow) {
        for (SCWindow *window in content.windows) {
            if ((long long)window.windowID == uniqueId) {
                filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
                break;
            }
        }
    } else {
        NSMutableArray<SCRunningApplication *> *excluded = [[NSMutableArray alloc] init];
        pid_t ownPid = [NSProcessInfo processInfo].processIdentifier;
        for (SCRunningApplication *application in content.applications) {
            if (application.processID == ownPid) {
                [excluded addObject:application];
            }
        }
        for (SCDisplay *display in content.displays) {
            if ((long long)display.displayID == uniqueId) {
                filter = [[SCContentFilter alloc] initWithDisplay:display excludingApplications:excluded exceptingWindows:@[]];
                break;
            }
        }
    }
    if (filter == nil) {
        [self reportFatalError];
        return;
    }
    CGFloat scale = filter.pointPixelScale > 0.0 ? filter.pointPixelScale : 1.0;
    sourcePixelSize = CGSizeMake(filter.contentRect.size.width * scale, filter.contentRect.size.height * scale);
    CGSize outputSize = TGScreenCaptureOutputSize(sourcePixelSize, TGScreenCaptureBoundingSize);
    SCStreamConfiguration *configuration = TGScreenCaptureStreamConfiguration(outputSize, TGScreenCaptureFramesPerSecond, isWindow ? NO : YES);

    SCStream *stream = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:self];
    NSError *outputError = nil;
    if (![stream addStreamOutput:self type:SCStreamOutputTypeScreen sampleHandlerQueue:_queue error:&outputError]) {
        [self reportFatalError];
        return;
    }
    _stream = stream;
    [self startRepeatingForStream:stream];
    __weak ScreenCaptureKitCapturer *weakSelf = self;
    [stream startCaptureWithCompletionHandler:^(NSError *error) {
        if (error != nil) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [weakSelf reportFatalError];
            });
        }
    }];
}

- (void)stop {
    if (!_isRunning) {
        return;
    }
    _isRunning = false;
    _generation += 1;
    SCStream *stream = _stream;
    _stream = nil;
    [stream stopCaptureWithCompletionHandler:nil];
    [self stopRepeating];
}

- (void)startRepeatingForStream:(SCStream *)stream {
    dispatch_async(_queue, ^{
        // A new capture never starts from the picture before the pause.
        self->_activeStream = stream;
        self->_repeater.reset();
        self->_lastVideoFrame = nil;
        if (self->_repeatTimer != nil) {
            return;
        }
        dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, self->_queue);
        uint64_t interval = (uint64_t)tgcalls::StillScreenRepeater::kIntervalNs;
        dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, (int64_t)interval), interval, interval / 10);
        __weak ScreenCaptureKitCapturer *weakSelf = self;
        dispatch_source_set_event_handler(timer, ^{
            [weakSelf resendLastFrameIfStill];
        });
        self->_repeatTimer = timer;
        dispatch_resume(timer);
    });
}

- (void)stopRepeating {
    dispatch_async(_queue, ^{
        if (self->_repeatTimer != nil) {
            dispatch_source_cancel(self->_repeatTimer);
            self->_repeatTimer = nil;
        }
        self->_activeStream = nil;
        self->_repeater.reset();
        self->_lastVideoFrame = nil;
    });
}

// On _queue. Hands the last frame to the encoder again when the screen has not changed for an interval, so a
// viewer who joins or a keyframe request after loss still gets a picture.
- (void)resendLastFrameIfStill {
    if (_lastVideoFrame == nil) {
        return;
    }
    int64_t clockNs = TGScreenCaptureHostClockNs();
    int64_t timeStampNs = 0;
    if (!_repeater.takeResend(clockNs, &timeStampNs)) {
        return;
    }
    RTCVideoFrame *repeated = [[RTCVideoFrame alloc] initWithBuffer:_lastVideoFrame.buffer rotation:RTCVideoRotation_0 timeStampNs:timeStampNs];
    getScreenCaptureVideoSource(_trackSource)->OnCapturedFrame(repeated);
}

- (void)reportFatalError {
    [self stop];
    if (_onFatalError) {
        _onFatalError();
    }
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self->_stream == stream) {
            [self reportFatalError];
        }
    });
}

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
    if (type != SCStreamOutputTypeScreen || stream != _activeStream || !TGScreenCaptureSampleBufferIsComplete(sampleBuffer)) {
        return;
    }
    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    TGScreenCaptureTagFrame(pixelBuffer);
    int width = (int)CVPixelBufferGetWidth(pixelBuffer);
    int height = (int)CVPixelBufferGetHeight(pixelBuffer);
    TGRTCCVPixelBuffer *rtcPixelBuffer = [[TGRTCCVPixelBuffer alloc] initWithPixelBuffer:pixelBuffer adaptedWidth:width adaptedHeight:height cropWidth:width cropHeight:height cropX:0 cropY:0];
    int64_t timeStampNs = (int64_t)(CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sampleBuffer)) * rtc::kNumNanosecsPerSec);
    RTCVideoFrame *videoFrame = [[RTCVideoFrame alloc] initWithBuffer:rtcPixelBuffer rotation:RTCVideoRotation_0 timeStampNs:timeStampNs];

    if (_uncroppedSink) {
        rtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer(new rtc::RefCountedObject<webrtc::ObjCFrameBuffer>(videoFrame.buffer));
        _uncroppedSink->OnFrame(webrtc::VideoFrame::Builder()
                                .set_video_frame_buffer(buffer)
                                .set_rotation(webrtc::kVideoRotation_0)
                                .set_timestamp_us(timeStampNs / rtc::kNumNanosecsPerMicrosec)
                                .build());
    }
    getScreenCaptureVideoSource(_trackSource)->OnCapturedFrame(videoFrame);
    // Kept even when the frame adapter dropped this one: the next repeat then carries the newest picture.
    _lastVideoFrame = videoFrame;
    int64_t clockNs = TGScreenCaptureHostClockNs();
    _repeater.noteFrameSent(clockNs, timeStampNs);
}

@end

#endif // TARGET_OS_OSX
