#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <Vision/Vision.h>
#import <QuartzCore/QuartzCore.h>   // CACurrentMediaTime()
#import <ImageIO/ImageIO.h>         // kCGImagePropertyOrientationRight

#include "HandTracker.h"
#include <optional>
#include <mutex>
#include <cmath>

static inline float clamp01(float v){ return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

class HTBridgeOwner; // forward

/// C++ side store for latest sample (ARC-friendly).
class HTBridgeOwner {
public:
    mutable std::mutex mtx;               // mutable so we can lock in const method
    std::optional<HandSample> last;

    void push(const HandSample& s){
        std::lock_guard<std::mutex> lk(mtx);
        last = s;
    }
};

// Forward-declare the Objective-C helper so we can reference it in the C++ Impl.
@class HTImpl;

class HandTracker::Impl {
public:
    Impl();
    ~Impl();

    bool start();
    void stop();
    std::optional<HandSample> latest() const;

    HTBridgeOwner bridge;
    HTImpl* objc = nil; // ARC manages lifetime on ObjC side
};

// ---------------- Objective-C helper ----------------

@interface HTImpl : NSObject<AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, strong) AVCaptureSession *session;
@property (nonatomic, strong) VNDetectHumanHandPoseRequest *handReq;
@property (nonatomic) dispatch_queue_t visionQ;
@property (nonatomic, assign) HTBridgeOwner *owner; // raw C++ pointer (no retain)
@end

@implementation HTImpl

- (instancetype)init {
    if ((self = [super init])) {
        _visionQ = dispatch_queue_create("vision.hand.queue", DISPATCH_QUEUE_SERIAL);
        _handReq = [VNDetectHumanHandPoseRequest new];
        _handReq.maximumHandCount = 1;
    }
    return self;
}

- (BOOL)startSession {
    self.session = [AVCaptureSession new];
    self.session.sessionPreset = AVCaptureSessionPreset640x480;

    AVCaptureDevice *dev = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if(!dev) return NO;

    NSError *err = nil;
    AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:dev error:&err];
    if(!input || err) return NO;

    if([self.session canAddInput:input]) [self.session addInput:input];

    AVCaptureVideoDataOutput *out = [AVCaptureVideoDataOutput new];
    out.alwaysDiscardsLateVideoFrames = YES;
    [out setSampleBufferDelegate:self queue:self.visionQ];
    out.videoSettings = @{ (NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA) };
    if([self.session canAddOutput:out]) [self.session addOutput:out];

    [self.session startRunning];
    return YES;
}

- (void)stopSession {
    if(self.session){
        [self.session stopRunning];
        self.session = nil;
    }
}

- (void)captureOutput:(AVCaptureOutput *)output
 didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
        fromConnection:(AVCaptureConnection *)connection
{
    HTBridgeOwner *owner = self.owner; // grab once
    if(!owner) return;
    
    static double nextDeadline = 0.0;
    double nowT = CACurrentMediaTime();
    const double interval = 1.0 / 30.0; // target 30 fps for detection
    if (nowT < nextDeadline) return;
    nextDeadline = nowT + interval;

    CVPixelBufferRef px = CMSampleBufferGetImageBuffer(sampleBuffer);
    if(!px) return;

    VNImageRequestHandler *handler =
        [[VNImageRequestHandler alloc] initWithCVPixelBuffer:px
                                                orientation:kCGImagePropertyOrientationRight
                                                    options:@{}];

    NSError *err = nil;
    [handler performRequests:@[self.handReq] error:&err];
    if(err) return;

    NSArray<VNHumanHandPoseObservation*> *obs = self.handReq.results;
    HandSample hs;
    hs.timestamp = CACurrentMediaTime();

    if(obs.count < 1){
        hs.hasHand = false;
        owner->push(hs);
        return;
    }

    VNHumanHandPoseObservation *h = obs.firstObject;

    NSError *pErr = nil;
    // ✅ Correct group constants
    NSDictionary<NSString*, VNRecognizedPoint*> *idxGroup =
        [h recognizedPointsForGroupKey:VNHumanHandPoseObservationJointsGroupNameIndexFinger error:&pErr];
    NSDictionary<NSString*, VNRecognizedPoint*> *thmGroup =
        [h recognizedPointsForGroupKey:VNHumanHandPoseObservationJointsGroupNameThumb error:&pErr];

    if(pErr){
        hs.hasHand = false;
        owner->push(hs);
        return;
    }

    VNRecognizedPoint *idxTip = idxGroup[VNHumanHandPoseObservationJointNameIndexTip];
    VNRecognizedPoint *thmTip = thmGroup[VNHumanHandPoseObservationJointNameThumbTip];

    if(!idxTip || !thmTip || idxTip.confidence < 0.3 || thmTip.confidence < 0.3){
        hs.hasHand = false;
        owner->push(hs);
        return;
    }

    float ix = clamp01(idxTip.location.x);
    float iy = clamp01(idxTip.location.y);

    float dx = ix - clamp01(thmTip.location.x);
    float dy = iy - clamp01(thmTip.location.y);
    float dist = sqrtf(dx*dx + dy*dy);

    hs.hasHand = true;
    hs.pinching = (dist < 0.08f);   // pinch threshold
    hs.normX = ix;
    hs.normY = iy;

    owner->push(hs);
}

@end

// ---------------- C++ Impl methods ----------------

HandTracker::Impl::Impl() {
    objc = [HTImpl new];
    objc.owner = &bridge;
}

HandTracker::Impl::~Impl() {
    stop();
    objc.owner = nullptr;
    objc = nil; // ARC cleans up Objective-C objects
}

bool HandTracker::Impl::start() {
    return [objc startSession];
}

void HandTracker::Impl::stop() {
    [objc stopSession];
}

std::optional<HandSample> HandTracker::Impl::latest() const {
    std::lock_guard<std::mutex> lk(bridge.mtx);
    return bridge.last;
}

// ---------------- Public HandTracker ----------------

HandTracker::HandTracker(){ impl = new Impl(); }
HandTracker::~HandTracker(){ delete impl; impl = nullptr; }
bool HandTracker::start(){ return impl->start(); }
void HandTracker::stop(){ impl->stop(); }
std::optional<HandSample> HandTracker::latest() const { return impl->latest(); }
