#import "ScreenCaptureKitSources.h"

#import <CoreVideo/CoreVideo.h>
#import <IOSurface/IOSurface.h>

static const NSTimeInterval TGScreenCaptureContentLifetime = 1.0;

@implementation TGScreenCaptureContentCache {
    NSLock *_lock;
    SCShareableContent *_content;
    CFAbsoluteTime _timestamp;
    NSMutableArray<dispatch_block_t> *_pending;
    BOOL _loading;
    void (^_fetcher)(void (^)(SCShareableContent *));
}

+ (instancetype)shared {
    static TGScreenCaptureContentCache *shared = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        shared = [[TGScreenCaptureContentCache alloc] init];
    });
    return shared;
}

- (instancetype)init {
    return [self initWithFetcher:^(void (^completion)(SCShareableContent *)) {
        [SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:YES completionHandler:^(SCShareableContent *content, NSError *error) {
            completion(content);
        }];
    }];
}

- (instancetype)initWithFetcher:(void (^)(void (^)(SCShareableContent *)))fetcher {
    self = [super init];
    if (self != nil) {
        _lock = [[NSLock alloc] init];
        _pending = [[NSMutableArray alloc] init];
        _fetcher = [fetcher copy];
    }
    return self;
}

- (void)refreshWithCompletion:(dispatch_block_t)completion {
    [_lock lock];
    [_pending addObject:[completion copy]];
    if (_loading) {
        [_lock unlock];
        return;
    }
    _loading = YES;
    [_lock unlock];
    __weak TGScreenCaptureContentCache *weakSelf = self;
    _fetcher(^(SCShareableContent *content) {
        TGScreenCaptureContentCache *strongSelf = weakSelf;
        if (strongSelf == nil) {
            return;
        }
        [strongSelf->_lock lock];
        if (content != nil) {
            strongSelf->_content = content;
        }
        strongSelf->_timestamp = CFAbsoluteTimeGetCurrent();
        strongSelf->_loading = NO;
        NSArray<dispatch_block_t> *pending = [strongSelf->_pending copy];
        [strongSelf->_pending removeAllObjects];
        [strongSelf->_lock unlock];
        for (dispatch_block_t block in pending) {
            block();
        }
    });
}

- (SCShareableContent *)contentWaitingAtMost:(NSTimeInterval)timeout {
    [_lock lock];
    SCShareableContent *content = _content;
    BOOL stale = content == nil || CFAbsoluteTimeGetCurrent() - _timestamp > TGScreenCaptureContentLifetime;
    [_lock unlock];
    if (!stale) {
        return content;
    }
    if (content != nil || [NSThread isMainThread]) {
        [self refreshWithCompletion:^{}];
        return content;
    }
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    [self refreshWithCompletion:^{
        dispatch_semaphore_signal(semaphore);
    }];
    dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout * NSEC_PER_SEC)));
    [_lock lock];
    content = _content;
    [_lock unlock];
    return content;
}

- (NSArray<TGScreenCaptureListedSource *> *)displaysWaitingAtMost:(NSTimeInterval)timeout {
    SCShareableContent *content = [self contentWaitingAtMost:timeout];
    NSMutableArray<TGScreenCaptureListedSource *> *result = [[NSMutableArray alloc] init];
    if (content == nil) {
        uint32_t count = 0;
        if (CGGetActiveDisplayList(0, NULL, &count) == kCGErrorSuccess && count > 0) {
            CGDirectDisplayID *displays = (CGDirectDisplayID *)calloc(count, sizeof(CGDirectDisplayID));
            if (CGGetActiveDisplayList(count, displays, &count) == kCGErrorSuccess) {
                for (uint32_t index = 0; index < count; index++) {
                    [result addObject:[[TGScreenCaptureListedSource alloc] initWithUniqueId:displays[index] title:@"Screen"]];
                }
            }
            free(displays);
        }
        return result;
    }
    for (SCDisplay *display in content.displays) {
        [result addObject:[[TGScreenCaptureListedSource alloc] initWithUniqueId:display.displayID title:@"Screen"]];
    }
    return result;
}

- (NSArray<TGScreenCaptureListedSource *> *)windowsWaitingAtMost:(NSTimeInterval)timeout {
    SCShareableContent *content = [self contentWaitingAtMost:timeout];
    NSMutableArray<TGScreenCaptureWindowCandidate *> *candidates = [[NSMutableArray alloc] init];
    for (SCWindow *window in content.windows) {
        [candidates addObject:[[TGScreenCaptureWindowCandidate alloc] initWithWindowID:window.windowID layer:window.windowLayer onScreen:window.isOnScreen title:window.title ownerName:window.owningApplication.applicationName ownerPid:window.owningApplication.processID]];
    }
    return TGScreenCaptureListedWindows(candidates);
}

@end

@interface TGScreenCapturePreviewView () <SCStreamOutput, SCStreamDelegate>
@end

@implementation TGScreenCapturePreviewView {
    long long _sourceId;
    BOOL _isWindow;
    CGSize _boxPixelSize;
    double _fps;
    BOOL _showsCursor;
    BOOL _running;
    NSInteger _generation;
    NSTimer *_timer;
    SCStream *_stream;
    dispatch_queue_t _queue;
    CVPixelBufferRef _displayedPixelBuffer;
}

- (instancetype)initWithSourceId:(long long)sourceId isWindow:(BOOL)isWindow boxPixelSize:(CGSize)boxPixelSize fps:(double)fps showsCursor:(BOOL)showsCursor {
    self = [super initWithFrame:NSZeroRect];
    if (self != nil) {
        _sourceId = sourceId;
        _isWindow = isWindow;
        _boxPixelSize = boxPixelSize;
        _fps = fps;
        _showsCursor = showsCursor;
        _queue = dispatch_queue_create("org.telegram.tgcalls.ScreenCapturePreview", DISPATCH_QUEUE_SERIAL);
        self.wantsLayer = YES;
        self.layer.contentsGravity = showsCursor ? kCAGravityResizeAspect : kCAGravityResizeAspectFill;
        self.layer.masksToBounds = YES;
    }
    return self;
}

- (void)dealloc {
    [_timer invalidate];
    [_stream stopCaptureWithCompletionHandler:nil];
    if (_displayedPixelBuffer) {
        CVPixelBufferRelease(_displayedPixelBuffer);
    }
}

- (SCContentFilter *)makeFilter:(SCShareableContent *)content {
    if (_isWindow) {
        for (SCWindow *window in content.windows) {
            if ((long long)window.windowID == _sourceId) {
                return [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
            }
        }
        return nil;
    }
    NSMutableArray<SCRunningApplication *> *excluded = [[NSMutableArray alloc] init];
    pid_t ownPid = [NSProcessInfo processInfo].processIdentifier;
    for (SCRunningApplication *application in content.applications) {
        if (application.processID == ownPid) {
            [excluded addObject:application];
        }
    }
    for (SCDisplay *display in content.displays) {
        if ((long long)display.displayID == _sourceId) {
            return [[SCContentFilter alloc] initWithDisplay:display excludingApplications:excluded exceptingWindows:@[]];
        }
    }
    return nil;
}

- (CGSize)sourcePixelSizeOfFilter:(SCContentFilter *)filter {
    CGFloat scale = filter.pointPixelScale > 0.0 ? filter.pointPixelScale : 1.0;
    return CGSizeMake(filter.contentRect.size.width * scale, filter.contentRect.size.height * scale);
}

- (void)start {
    if (_running) {
        return;
    }
    _running = YES;
    _generation += 1;
    if (TGScreenCapturePreviewUsesScreenshots(_fps)) {
        [self captureScreenshot];
        __weak TGScreenCapturePreviewView *weakSelf = self;
        _timer = [NSTimer scheduledTimerWithTimeInterval:TGScreenCaptureScreenshotInterval(_fps) repeats:YES block:^(NSTimer *timer) {
            [weakSelf captureScreenshot];
        }];
        _timer.tolerance = 0.25;
    } else {
        [self startStream];
    }
}

- (void)stop {
    if (!_running) {
        return;
    }
    _running = NO;
    _generation += 1;
    [_timer invalidate];
    _timer = nil;
    SCStream *stream = _stream;
    _stream = nil;
    [stream stopCaptureWithCompletionHandler:nil];
}

- (SCShareableContent *)contentOrRetry:(void (^)(TGScreenCapturePreviewView *view))retry {
    SCShareableContent *content = [[TGScreenCaptureContentCache shared] contentWaitingAtMost:0.5];
    if (content != nil) {
        return content;
    }
    NSInteger generation = _generation;
    __weak TGScreenCapturePreviewView *weakSelf = self;
    [[TGScreenCaptureContentCache shared] refreshWithCompletion:^{
        dispatch_async(dispatch_get_main_queue(), ^{
            TGScreenCapturePreviewView *strongSelf = weakSelf;
            if (strongSelf != nil && strongSelf->_running && strongSelf->_generation == generation && strongSelf->_stream == nil && [[TGScreenCaptureContentCache shared] contentWaitingAtMost:0.0] != nil) {
                retry(strongSelf);
            }
        });
    }];
    return nil;
}

- (void)captureScreenshot {
    SCShareableContent *content = [self contentOrRetry:^(TGScreenCapturePreviewView *view) {
        [view captureScreenshot];
    }];
    if (content == nil) {
        return;
    }
    SCContentFilter *filter = [self makeFilter:content];
    if (filter == nil) {
        return;
    }
    SCStreamConfiguration *configuration = TGScreenCaptureThumbnailConfiguration([self sourcePixelSizeOfFilter:filter], _boxPixelSize, _showsCursor);
    NSInteger generation = _generation;
    __weak TGScreenCapturePreviewView *weakSelf = self;
    [SCScreenshotManager captureImageWithFilter:filter configuration:configuration completionHandler:^(CGImageRef image, NSError *error) {
        if (image == NULL) {
            return;
        }
        CGImageRetain(image);
        dispatch_async(dispatch_get_main_queue(), ^{
            TGScreenCapturePreviewView *strongSelf = weakSelf;
            if (strongSelf != nil && strongSelf->_running && strongSelf->_generation == generation) {
                strongSelf.layer.contents = (__bridge id)image;
            }
            CGImageRelease(image);
        });
    }];
}

- (void)startStream {
    SCShareableContent *content = [self contentOrRetry:^(TGScreenCapturePreviewView *view) {
        [view startStream];
    }];
    if (content == nil) {
        return;
    }
    SCContentFilter *filter = [self makeFilter:content];
    if (filter == nil) {
        return;
    }
    SCStreamConfiguration *configuration = TGScreenCapturePreviewConfiguration([self sourcePixelSizeOfFilter:filter], _boxPixelSize, _fps, _showsCursor);
    SCStream *stream = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:self];
    if (![stream addStreamOutput:self type:SCStreamOutputTypeScreen sampleHandlerQueue:_queue error:nil]) {
        return;
    }
    _stream = stream;
    [stream startCaptureWithCompletionHandler:nil];
}

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
    if (type != SCStreamOutputTypeScreen || !TGScreenCaptureSampleBufferIsComplete(sampleBuffer)) {
        return;
    }
    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (pixelBuffer == NULL || CVPixelBufferGetIOSurface(pixelBuffer) == NULL) {
        return;
    }
    CVPixelBufferRetain(pixelBuffer);
    __weak TGScreenCapturePreviewView *weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        TGScreenCapturePreviewView *strongSelf = weakSelf;
        if (strongSelf == nil || strongSelf->_stream != stream) {
            CVPixelBufferRelease(pixelBuffer);
            return;
        }
        strongSelf.layer.contents = (__bridge id)CVPixelBufferGetIOSurface(pixelBuffer);
        if (strongSelf->_displayedPixelBuffer) {
            CVPixelBufferRelease(strongSelf->_displayedPixelBuffer);
        }
        strongSelf->_displayedPixelBuffer = pixelBuffer;
    });
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self->_stream == stream) {
            self->_stream = nil;
            self->_running = NO;
        }
    });
}

@end
