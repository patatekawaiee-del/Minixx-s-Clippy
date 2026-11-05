// Native.mm - the recorder behind Minixx's Clippy.
//
// Pipeline:
//   ReplayKit (in-app capture, screen + game audio)
//     -> video frames go straight into a VideoToolbox hardware encoder (H.264 / HEVC)
//     -> encoded frames are kept in a ring buffer made of GOPs (one keyframe per second)
//     -> audio is kept as small PCM chunks next to it
//   On request, the matching slice is written to an .mp4 with AVAssetWriter
//   (video is copied as-is, no re-encode; audio is encoded to AAC), then a thumbnail is made.
//
// No Geode / cocos2d header is included here on purpose (name clashes with UIKit).

#include "Native.hpp"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ReplayKit/ReplayKit.h>
#import <UIKit/UIKit.h>
#import <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <vector>

#include <unistd.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace clippy::native {
namespace {

    // ------------------------------------------------------------------ data

    struct VSample {
        CMSampleBufferRef buf;
        double pts;
        size_t bytes;
    };

    struct Gop {
        std::vector<VSample> frames;
        double startPts = 0.0;
        size_t bytes = 0;
    };

    struct ASample {
        CMSampleBufferRef buf;
        double pts;
        size_t bytes;
    };

    constexpr size_t kMaxBytes = 450ull * 1024ull * 1024ull;

    struct Core {
        std::mutex mtx;
        Config cfg;
        bool cfgDirty = false;
        CaptureState state = CaptureState::Off;
        std::string lastError;
        StateFn onState;
        uint64_t generation = 0;

        VTCompressionSessionRef session = nullptr;
        int sessionW = 0;
        int sessionH = 0;
        std::string codecName;

        CMFormatDescriptionRef videoFmt = nullptr;
        CMFormatDescriptionRef audioFmt = nullptr;

        std::deque<Gop> gops;
        std::deque<ASample> audio;
        size_t videoBytes = 0;
        size_t audioBytes = 0;
        double newestPts = 0.0;

        bool haveClock = false;
        double clockPts = 0.0;  // pts of the latest video frame...
        double clockHost = 0.0; // ...and the host time at which it arrived

        long long frames = 0;
        long long audioChunks = 0;
        int orientationKey = 0;
        int frameW = 0;
        int frameH = 0;

        bool observersInstalled = false;
        dispatch_queue_t exportQueue = nil;
    };

    Core& core() {
        static Core* c = [] {
            auto* k = new Core();
            k->exportQueue = dispatch_queue_create("minixx.clippy.export", DISPATCH_QUEUE_SERIAL);
            return k;
        }();
        return *c;
    }

    std::atomic<LogFn> g_logger{nullptr};

    void nlog(int level, char const* fmt, ...) __attribute__((format(printf, 2, 3)));
    void nlog(int level, char const* fmt, ...) {
        char buf[600];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        LogFn fn = g_logger.load();
        if (fn) {
            fn(level, buf);
        } else {
            NSLog(@"[Clippy] %s", buf);
        }
    }

    inline double ptsOf(CMSampleBufferRef b) {
        return CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(b));
    }

    // ------------------------------------------------------------------ ring buffer (call with the lock held)

    void releaseGop(Gop& g) {
        for (auto& s : g.frames) {
            if (s.buf) CFRelease(s.buf);
        }
        g.frames.clear();
    }

    void dropFrontGop(Core& c) {
        c.videoBytes -= std::min(c.videoBytes, c.gops.front().bytes);
        releaseGop(c.gops.front());
        c.gops.pop_front();
    }

    void popFrontAudio(Core& c) {
        c.audioBytes -= std::min(c.audioBytes, c.audio.front().bytes);
        CFRelease(c.audio.front().buf);
        c.audio.pop_front();
    }

    void trimLocked(Core& c) {
        double window = std::max(5.0, c.cfg.bufferSeconds);
        while (c.gops.size() > 1 && (c.newestPts - c.gops[1].startPts) >= window) dropFrontGop(c);
        while (c.gops.size() > 1 && (c.videoBytes + c.audioBytes) > kMaxBytes) dropFrontGop(c);

        double keepFrom = (c.gops.empty() ? c.newestPts : c.gops.front().startPts) - 0.5;
        while (!c.audio.empty() && c.audio.front().pts < keepFrom) popFrontAudio(c);
        if (!c.audio.empty()) {
            double audioNewest = c.audio.back().pts;
            while (!c.audio.empty() && c.audio.front().pts < audioNewest - (window + 5.0)) popFrontAudio(c);
        }
    }

    void clearRingLocked(Core& c) {
        for (auto& g : c.gops) releaseGop(g);
        c.gops.clear();
        while (!c.audio.empty()) popFrontAudio(c);
        c.videoBytes = 0;
        c.audioBytes = 0;
        c.newestPts = 0.0;
        if (c.videoFmt) {
            CFRelease(c.videoFmt);
            c.videoFmt = nullptr;
        }
        if (c.audioFmt) {
            CFRelease(c.audioFmt);
            c.audioFmt = nullptr;
        }
    }

    // ------------------------------------------------------------------ state

    void setState(CaptureState st, std::string const& msg) {
        StateFn cb;
        {
            std::lock_guard<std::mutex> l(core().mtx);
            core().state = st;
            core().lastError = (st == CaptureState::Failed || st == CaptureState::Denied) ? msg : std::string();
            cb = core().onState;
        }
        std::string m = msg;
        if (cb) {
            dispatch_async(dispatch_get_main_queue(), ^{
                cb(st, m);
            });
        }
    }

    // ------------------------------------------------------------------ encoder

    void setInt(VTCompressionSessionRef s, CFStringRef key, int64_t v) {
        CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &v);
        VTSessionSetProperty(s, key, n);
        CFRelease(n);
    }

    void setDouble(VTCompressionSessionRef s, CFStringRef key, double v) {
        CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &v);
        VTSessionSetProperty(s, key, n);
        CFRelease(n);
    }

    void vtOutput(void* refcon, void*, OSStatus status, VTEncodeInfoFlags flags, CMSampleBufferRef sb) {
        if (status != noErr || !sb || (flags & kVTEncodeInfo_FrameDropped)) return;
        if (!CMSampleBufferDataIsReady(sb)) return;
        uint64_t gen = (uint64_t)(uintptr_t)refcon;

        bool key = true;
        CFArrayRef atts = CMSampleBufferGetSampleAttachmentsArray(sb, false);
        if (atts && CFArrayGetCount(atts) > 0) {
            CFDictionaryRef d = (CFDictionaryRef)CFArrayGetValueAtIndex(atts, 0);
            CFTypeRef notSync = CFDictionaryGetValue(d, kCMSampleAttachmentKey_NotSync);
            if (notSync == kCFBooleanTrue) key = false;
        }
        double pts = ptsOf(sb);
        size_t bytes = CMSampleBufferGetTotalSampleSize(sb);
        CFRetain(sb);

        std::lock_guard<std::mutex> l(core().mtx);
        Core& c = core();
        if (gen != c.generation || c.state == CaptureState::Off) {
            CFRelease(sb);
            return;
        }
        if (!c.videoFmt) {
            c.videoFmt = CMSampleBufferGetFormatDescription(sb);
            if (c.videoFmt) CFRetain(c.videoFmt);
        }
        if (key) {
            Gop g;
            g.startPts = pts;
            c.gops.push_back(std::move(g));
        }
        if (c.gops.empty()) { // still waiting for the first keyframe
            CFRelease(sb);
            return;
        }
        Gop& g = c.gops.back();
        g.frames.push_back({sb, pts, bytes});
        g.bytes += bytes;
        c.videoBytes += bytes;
        c.newestPts = pts;
        trimLocked(c);
    }

    void teardownSession(VTCompressionSessionRef s) {
        if (!s) return;
        VTCompressionSessionCompleteFrames(s, kCMTimeInvalid);
        VTCompressionSessionInvalidate(s);
        CFRelease(s);
    }

    bool rebuildSession(int w, int h) {
        VTCompressionSessionRef old = nullptr;
        Config cfg;
        uint64_t gen = 0;
        {
            std::lock_guard<std::mutex> l(core().mtx);
            Core& c = core();
            old = c.session;
            c.session = nullptr;
            cfg = c.cfg;
            c.cfgDirty = false;
            gen = ++c.generation;
            clearRingLocked(c);
        }
        teardownSession(old); // late callbacks from the old session carry an old generation and are dropped

        bool hevc = cfg.hevc;
        VTCompressionSessionRef ns = nullptr;
        OSStatus st = VTCompressionSessionCreate(kCFAllocatorDefault, w, h,
            hevc ? kCMVideoCodecType_HEVC : kCMVideoCodecType_H264,
            NULL, NULL, NULL, vtOutput, (void*)(uintptr_t)gen, &ns);
        if ((st != noErr || !ns) && hevc) {
            nlog(2, "HEVC encoder unavailable (%d), falling back to H.264", (int)st);
            hevc = false;
            ns = nullptr;
            st = VTCompressionSessionCreate(kCFAllocatorDefault, w, h, kCMVideoCodecType_H264,
                NULL, NULL, NULL, vtOutput, (void*)(uintptr_t)gen, &ns);
        }
        if (st != noErr || !ns) {
            nlog(3, "Could not create the video encoder (%d) for %dx%d", (int)st, w, h);
            setState(CaptureState::Failed, "Could not create the video encoder.");
            return false;
        }

        int64_t bitrate = (int64_t)std::max(1000, cfg.bitrateKbps) * 1000;
        VTSessionSetProperty(ns, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
        VTSessionSetProperty(ns, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
        VTSessionSetProperty(ns, kVTCompressionPropertyKey_ProfileLevel,
            hevc ? kVTProfileLevel_HEVC_Main_AutoLevel : kVTProfileLevel_H264_High_AutoLevel);
        setInt(ns, kVTCompressionPropertyKey_AverageBitRate, bitrate);
        setInt(ns, kVTCompressionPropertyKey_ExpectedFrameRate, 60);
        setInt(ns, kVTCompressionPropertyKey_MaxKeyFrameInterval, 60);
        setDouble(ns, kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, 1.0);

        int64_t bytesPerSec = bitrate / 8 * 3 / 2;
        int64_t oneSec = 1;
        CFNumberRef limits[2] = {
            CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &bytesPerSec),
            CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &oneSec),
        };
        CFArrayRef arr = CFArrayCreate(kCFAllocatorDefault, (const void**)limits, 2, &kCFTypeArrayCallBacks);
        VTSessionSetProperty(ns, kVTCompressionPropertyKey_DataRateLimits, arr);
        CFRelease(arr);
        CFRelease(limits[0]);
        CFRelease(limits[1]);

        VTCompressionSessionPrepareToEncodeFrames(ns);

        {
            std::lock_guard<std::mutex> l(core().mtx);
            Core& c = core();
            c.session = ns;
            c.sessionW = w;
            c.sessionH = h;
            c.codecName = hevc ? "HEVC" : "H.264";
        }
        nlog(1, "Encoder ready: %dx%d %s @ %d kbps", w, h, hevc ? "HEVC" : "H.264", cfg.bitrateKbps);
        return true;
    }

    // ------------------------------------------------------------------ capture handlers

    void handleVideo(CMSampleBufferRef sb) {
        if (!sb || !CMSampleBufferIsValid(sb) || !CMSampleBufferDataIsReady(sb)) return;
        CVImageBufferRef pix = CMSampleBufferGetImageBuffer(sb);
        if (!pix) return;
        CMTime pts = CMSampleBufferGetPresentationTimeStamp(sb);
        if (!CMTIME_IS_VALID(pts)) return;
        int w = (int)CVPixelBufferGetWidth(pix);
        int h = (int)CVPixelBufferGetHeight(pix);
        double host = hostNow();

        int orient = 0;
        CFTypeRef o = CMGetAttachment(sb, (__bridge CFStringRef)RPVideoSampleOrientationKey, NULL);
        if (o && CFGetTypeID(o) == CFNumberGetTypeID()) {
            int v = 0;
            if (CFNumberGetValue((CFNumberRef)o, kCFNumberIntType, &v)) orient = v;
        }

        bool needSession = false;
        {
            std::lock_guard<std::mutex> l(core().mtx);
            Core& c = core();
            if (c.state == CaptureState::Off || c.state == CaptureState::Failed || c.state == CaptureState::Denied) return;
            c.frames++;
            c.frameW = w;
            c.frameH = h;
            c.orientationKey = orient;
            c.clockPts = CMTimeGetSeconds(pts);
            c.clockHost = host;
            c.haveClock = true;
            needSession = !c.session || c.sessionW != w || c.sessionH != h || c.cfgDirty;
        }
        if (needSession && !rebuildSession(w, h)) return;

        VTCompressionSessionRef s = nullptr;
        {
            std::lock_guard<std::mutex> l(core().mtx);
            s = core().session;
            if (s) CFRetain(s);
        }
        if (!s) return;
        OSStatus st = VTCompressionSessionEncodeFrame(s, pix, pts, kCMTimeInvalid, NULL, NULL, NULL);
        CFRelease(s);
        if (st != noErr) {
            static std::atomic<int> errors{0};
            if (errors++ < 5) nlog(2, "VTCompressionSessionEncodeFrame failed (%d)", (int)st);
        }
    }

    // Deep copy so we never depend on ReplayKit keeping its buffers alive.
    CMSampleBufferRef copyAudio(CMSampleBufferRef src) {
        CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(src);
        CMBlockBufferRef srcBlock = CMSampleBufferGetDataBuffer(src);
        if (!fmt || !srcBlock) return NULL;
        AudioStreamBasicDescription const* asbd = CMAudioFormatDescriptionGetStreamBasicDescription(fmt);
        if (!asbd || asbd->mFormatID != kAudioFormatLinearPCM) return NULL;
        size_t len = CMBlockBufferGetDataLength(srcBlock);
        if (len == 0) return NULL;

        void* mem = malloc(len);
        if (!mem) return NULL;
        if (CMBlockBufferCopyDataBytes(srcBlock, 0, len, mem) != kCMBlockBufferNoErr) {
            free(mem);
            return NULL;
        }
        CMBlockBufferRef blk = NULL;
        OSStatus st = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, mem, len, kCFAllocatorMalloc, NULL, 0, len, 0, &blk);
        if (st != kCMBlockBufferNoErr || !blk) {
            free(mem);
            return NULL;
        }
        CMSampleBufferRef out = NULL;
        st = CMAudioSampleBufferCreateReadyWithPacketDescriptions(kCFAllocatorDefault, blk, fmt,
            CMSampleBufferGetNumSamples(src), CMSampleBufferGetPresentationTimeStamp(src), NULL, &out);
        CFRelease(blk);
        return st == noErr ? out : NULL;
    }

    void handleAudio(CMSampleBufferRef sb) {
        if (!sb || !CMSampleBufferIsValid(sb) || !CMSampleBufferDataIsReady(sb)) return;
        {
            std::lock_guard<std::mutex> l(core().mtx);
            if (core().state == CaptureState::Off) return;
        }
        CMSampleBufferRef copy = copyAudio(sb);
        if (!copy) return;
        double pts = ptsOf(copy);
        size_t bytes = CMSampleBufferGetTotalSampleSize(copy);

        std::lock_guard<std::mutex> l(core().mtx);
        Core& c = core();
        if (c.state == CaptureState::Off) {
            CFRelease(copy);
            return;
        }
        if (!c.audioFmt) {
            c.audioFmt = CMSampleBufferGetFormatDescription(copy);
            if (c.audioFmt) CFRetain(c.audioFmt);
        }
        c.audio.push_back({copy, pts, bytes});
        c.audioBytes += bytes;
        c.audioChunks++;
        trimLocked(c);
    }

    // ------------------------------------------------------------------ export

    struct Held {
        std::vector<CMSampleBufferRef> v;
        std::vector<CMSampleBufferRef> a;
        CMFormatDescriptionRef vf = nullptr;
        CMFormatDescriptionRef af = nullptr;
        Held() = default;
        Held(Held const&) = delete;
        Held& operator=(Held const&) = delete;
        ~Held() {
            for (auto b : v) CFRelease(b);
            for (auto b : a) CFRelease(b);
            if (vf) CFRelease(vf);
            if (af) CFRelease(af);
        }
    };

    // The encoded frames keep the buffer's native (portrait) layout; the movie's track transform rotates them.
    CGAffineTransform transformFor(int key, int mode, int w, int h) {
        CGAffineTransform cw = CGAffineTransformMake(0, 1, -1, 0, h, 0);
        CGAffineTransform ccw = CGAffineTransformMake(0, -1, 1, 0, 0, w);
        CGAffineTransform flip = CGAffineTransformMake(-1, 0, 0, -1, w, h);
        switch (mode) {
            case 1: return CGAffineTransformIdentity;
            case 2: return cw;
            case 3: return ccw;
            case 4: return flip;
            default: break;
        }
        int k = key;
        if (k == 0 && h > w) k = 6; // no hint but a portrait buffer: the game runs in landscape
        if (k == 6) return cw;
        if (k == 8) return ccw;
        if (k == 3) return flip;
        return CGAffineTransformIdentity;
    }

    ClipResult doExport(ClipRequest const& req) {
        ClipResult res;
        Core& c = core();

        // Wait (max ~5 s) until the post-roll has actually been captured.
        for (int i = 0; i < 120; ++i) {
            bool ready = false;
            {
                std::lock_guard<std::mutex> l(c.mtx);
                if (c.state != CaptureState::Running && c.state != CaptureState::Starting) {
                    res.error = "The buffer is not running.";
                    return res;
                }
                if (!c.haveClock) {
                    res.error = "No frame has been captured yet.";
                    return res;
                }
                double target = c.clockPts + (req.endHost - c.clockHost);
                ready = target <= c.newestPts + 0.03;
            }
            if (ready) break;
            usleep(40000);
        }

        // Snapshot the slice we need (retained, so the ring can keep moving).
        Held held;
        double hlPts = 0.0;
        int orient = 0, fw = 0, fh = 0;
        Config cfg;
        {
            std::lock_guard<std::mutex> l(c.mtx);
            if (c.gops.empty() || !c.videoFmt) {
                res.error = "Nothing has been recorded yet.";
                return res;
            }
            auto toPts = [&](double host) { return c.clockPts + (host - c.clockHost); };
            double t0 = toPts(req.startHost);
            double t1 = toPts(req.endHost);
            double oldest = c.gops.front().startPts;
            if (t0 < oldest) t0 = oldest;
            if (t1 > c.newestPts) t1 = c.newestPts;
            if (t1 - t0 < 0.3) {
                res.error = "Not enough footage buffered yet.";
                return res;
            }
            hlPts = toPts(req.highlightHost > 0.0 ? req.highlightHost : req.endHost);

            size_t first = 0;
            for (size_t i = 0; i < c.gops.size(); ++i) {
                if (c.gops[i].startPts <= t0) first = i;
                else break;
            }
            bool done = false;
            for (size_t i = first; i < c.gops.size() && !done; ++i) {
                for (auto& s : c.gops[i].frames) {
                    if (s.pts > t1 + 0.0005) {
                        done = true;
                        break;
                    }
                    CFRetain(s.buf);
                    held.v.push_back(s.buf);
                }
            }
            if (held.v.empty()) {
                res.error = "Nothing to export.";
                return res;
            }
            double firstPts = ptsOf(held.v.front());
            double lastPts = ptsOf(held.v.back());
            for (auto& a : c.audio) {
                if (a.pts >= firstPts - 0.05 && a.pts <= lastPts + 0.1) {
                    CFRetain(a.buf);
                    held.a.push_back(a.buf);
                }
            }
            held.vf = c.videoFmt;
            CFRetain(held.vf);
            if (c.audioFmt) {
                held.af = c.audioFmt;
                CFRetain(held.af);
            }
            orient = c.orientationKey;
            fw = c.frameW;
            fh = c.frameH;
            cfg = c.cfg;
        }

        CMTime startT = CMSampleBufferGetPresentationTimeStamp(held.v.front());
        CMTime lastT = CMSampleBufferGetPresentationTimeStamp(held.v.back());
        double firstPts = CMTimeGetSeconds(startT);
        double lastPts = CMTimeGetSeconds(lastT);

        NSString* dir = [NSString stringWithUTF8String:req.directory.c_str()];
        NSString* base = [NSString stringWithUTF8String:req.baseName.c_str()];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
        NSString* videoPath = [dir stringByAppendingPathComponent:[base stringByAppendingString:@".mp4"]];
        NSString* thumbPath = [dir stringByAppendingPathComponent:[base stringByAppendingString:@".png"]];
        [[NSFileManager defaultManager] removeItemAtPath:videoPath error:nil];

        NSError* err = nil;
        AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL:[NSURL fileURLWithPath:videoPath] fileType:AVFileTypeMPEG4 error:&err];
        if (!writer) {
            res.error = std::string("Could not create the movie file: ") + (err.localizedDescription.UTF8String ?: "unknown error");
            return res;
        }

        AVAssetWriterInput* vin = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:nil sourceFormatHint:held.vf];
        vin.expectsMediaDataInRealTime = NO;
        vin.transform = transformFor(orient, cfg.rotation, fw, fh);
        if (![writer canAddInput:vin]) {
            res.error = "The movie writer refused the video track.";
            return res;
        }
        [writer addInput:vin];

        AVAssetWriterInput* ain = nil;
        if (!held.a.empty() && held.af) {
            AudioStreamBasicDescription const* asbd = CMAudioFormatDescriptionGetStreamBasicDescription((CMAudioFormatDescriptionRef)held.af);
            if (asbd && asbd->mFormatID == kAudioFormatLinearPCM) {
                double rate = asbd->mSampleRate > 0.0 ? asbd->mSampleRate : 44100.0;
                int channels = asbd->mChannelsPerFrame > 0 ? (int)std::min<UInt32>(2, asbd->mChannelsPerFrame) : 2;
                NSDictionary* settings = @{
                    AVFormatIDKey: @(kAudioFormatMPEG4AAC),
                    AVSampleRateKey: @(rate),
                    AVNumberOfChannelsKey: @(channels),
                    AVEncoderBitRateKey: @(128000),
                };
                ain = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio outputSettings:settings sourceFormatHint:held.af];
                ain.expectsMediaDataInRealTime = NO;
                if ([writer canAddInput:ain]) {
                    [writer addInput:ain];
                } else {
                    ain = nil;
                }
            }
        }

        if (![writer startWriting]) {
            res.error = std::string("Could not start writing: ") + (writer.error.localizedDescription.UTF8String ?: "unknown error");
            return res;
        }
        [writer startSessionAtSourceTime:startT];

        // Interleave video and audio by timestamp.
        size_t vi = 0, ai = 0;
        NSTimeInterval deadline = [NSDate timeIntervalSinceReferenceDate] + 60.0;
        bool failed = false;
        while ((vi < held.v.size() || (ain && ai < held.a.size())) && !failed) {
            bool pickVideo;
            if (vi >= held.v.size()) pickVideo = false;
            else if (!ain || ai >= held.a.size()) pickVideo = true;
            else pickVideo = ptsOf(held.v[vi]) <= ptsOf(held.a[ai]);

            AVAssetWriterInput* in = pickVideo ? vin : ain;
            while (!in.isReadyForMoreMediaData) {
                if (writer.status != AVAssetWriterStatusWriting || [NSDate timeIntervalSinceReferenceDate] > deadline) {
                    failed = true;
                    break;
                }
                [NSThread sleepForTimeInterval:0.002];
            }
            if (failed) break;

            CMSampleBufferRef sb = pickVideo ? held.v[vi++] : held.a[ai++];
            if (!pickVideo && CMTimeCompare(CMSampleBufferGetPresentationTimeStamp(sb), startT) < 0) continue;
            if (![in appendSampleBuffer:sb]) failed = true;
        }

        [vin markAsFinished];
        if (ain) [ain markAsFinished];
        if (failed) {
            [writer cancelWriting];
            [[NSFileManager defaultManager] removeItemAtPath:videoPath error:nil];
            res.error = std::string("Writing the clip failed: ") + (writer.error.localizedDescription.UTF8String ?: "timeout");
            return res;
        }

        [writer endSessionAtSourceTime:CMTimeAdd(lastT, CMTimeMake(1, 30))];
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [writer finishWritingWithCompletionHandler:^{
            dispatch_semaphore_signal(sem);
        }];
        dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(30 * NSEC_PER_SEC)));
        if (writer.status != AVAssetWriterStatusCompleted) {
            [[NSFileManager defaultManager] removeItemAtPath:videoPath error:nil];
            res.error = std::string("Finishing the clip failed: ") + (writer.error.localizedDescription.UTF8String ?: "unknown error");
            return res;
        }

        res.ok = true;
        res.videoPath = std::string(videoPath.UTF8String);
        res.duration = std::max(0.0, lastPts - firstPts);
        NSDictionary* attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:videoPath error:nil];
        res.bytes = (long long)[attrs fileSize];

        // Thumbnail around the highlight moment (the death, the completion...).
        @autoreleasepool {
            AVURLAsset* asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:videoPath] options:nil];
            AVAssetImageGenerator* gen = [AVAssetImageGenerator assetImageGeneratorWithAsset:asset];
            gen.appliesPreferredTrackTransform = YES;
            gen.maximumSize = CGSizeMake(480, 480);
            gen.requestedTimeToleranceBefore = kCMTimePositiveInfinity;
            gen.requestedTimeToleranceAfter = kCMTimePositiveInfinity;
            double off = std::min(std::max(hlPts - firstPts + 0.1, 0.0), std::max(res.duration - 0.2, 0.0));
            NSError* ierr = nil;
            CGImageRef img = [gen copyCGImageAtTime:CMTimeMakeWithSeconds(off, 600) actualTime:NULL error:&ierr];
            if (img) {
                UIImage* ui = [UIImage imageWithCGImage:img];
                NSData* png = UIImagePNGRepresentation(ui);
                if (png && [png writeToFile:thumbPath atomically:YES]) res.thumbPath = std::string(thumbPath.UTF8String);
                CGImageRelease(img);
            } else {
                nlog(2, "Thumbnail failed: %s", ierr.localizedDescription.UTF8String ?: "unknown error");
            }
        }

        nlog(1, "Clip saved: %s (%.1fs, %lld bytes)", res.videoPath.c_str(), res.duration, res.bytes);
        return res;
    }

    // ------------------------------------------------------------------ misc

    UIViewController* topViewController() {
        UIWindow* win = nil;
        for (UIScene* scene in [UIApplication sharedApplication].connectedScenes) {
            if (scene.activationState == UISceneActivationStateForegroundActive && [scene isKindOfClass:[UIWindowScene class]]) {
                UIWindowScene* ws = (UIWindowScene*)scene;
                for (UIWindow* w in ws.windows) {
                    if (w.isKeyWindow) {
                        win = w;
                        break;
                    }
                }
                if (!win) win = ws.windows.firstObject;
                if (win) break;
            }
        }
        if (!win) win = [UIApplication sharedApplication].windows.firstObject;
        UIViewController* vc = win.rootViewController;
        while (vc.presentedViewController) vc = vc.presentedViewController;
        return vc;
    }

    void installObservers() {
        Core& c = core();
        {
            std::lock_guard<std::mutex> l(c.mtx);
            if (c.observersInstalled) return;
            c.observersInstalled = true;
        }
        [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidReceiveMemoryWarningNotification
                                                          object:nil
                                                           queue:nil
                                                      usingBlock:^(NSNotification*) {
            Core& k = core();
            std::lock_guard<std::mutex> l(k.mtx);
            while (k.gops.size() > 1 && (k.newestPts - k.gops[1].startPts) >= 8.0) dropFrontGop(k);
            nlog(2, "Memory warning: buffer trimmed to ~8 seconds");
        }];
    }

} // namespace

// ---------------------------------------------------------------------- public API

void setLogger(LogFn fn) {
    g_logger.store(fn);
}

double hostNow() {
    return CMTimeGetSeconds(CMClockGetTime(CMClockGetHostTimeClock()));
}

void configure(Config const& config) {
    Core& c = core();
    std::lock_guard<std::mutex> l(c.mtx);
    bool encoderChanged = c.session && (config.hevc != c.cfg.hevc || config.bitrateKbps != c.cfg.bitrateKbps);
    c.cfg = config;
    if (encoderChanged) c.cfgDirty = true;
    trimLocked(c);
}

void start(StateFn onState) {
    dispatch_async(dispatch_get_main_queue(), ^{
        Core& c = core();
        {
            std::lock_guard<std::mutex> l(c.mtx);
            c.onState = onState;
            if (c.state == CaptureState::Starting || c.state == CaptureState::Running) return;
        }
        installObservers();

        RPScreenRecorder* rec = [RPScreenRecorder sharedRecorder];
        if (!rec.isAvailable) {
            setState(CaptureState::Failed, "Screen capture is not available right now.");
            return;
        }
        {
            std::lock_guard<std::mutex> l(c.mtx);
            clearRingLocked(c);
            c.generation++;
            c.frames = 0;
            c.audioChunks = 0;
            c.haveClock = false;
        }
        setState(CaptureState::Starting, "");
        rec.microphoneEnabled = NO;
        nlog(1, "Starting ReplayKit capture");
        [rec startCaptureWithHandler:^(CMSampleBufferRef sampleBuffer, RPSampleBufferType type, NSError* error) {
            if (error) return;
            if (type == RPSampleBufferTypeVideo) handleVideo(sampleBuffer);
            else if (type == RPSampleBufferTypeAudioApp) handleAudio(sampleBuffer);
        }
        completionHandler:^(NSError* error) {
            if (error) {
                bool denied = (error.code == RPRecordingErrorUserDeclined);
                std::string msg = error.localizedDescription.UTF8String ?: "";
                nlog(denied ? 1 : 3, "ReplayKit start failed (%ld): %s", (long)error.code, msg.c_str());
                setState(denied ? CaptureState::Denied : CaptureState::Failed, msg);
            } else {
                nlog(1, "ReplayKit capture running");
                setState(CaptureState::Running, "");
            }
        }];
    });
}

void stop() {
    dispatch_async(dispatch_get_main_queue(), ^{
        Core& c = core();
        VTCompressionSessionRef old = nullptr;
        {
            std::lock_guard<std::mutex> l(c.mtx);
            c.state = CaptureState::Off;
            old = c.session;
            c.session = nullptr;
            c.generation++;
            clearRingLocked(c);
        }
        [[RPScreenRecorder sharedRecorder] stopCaptureWithHandler:nil];
        teardownSession(old);
        setState(CaptureState::Off, "");
    });
}

Status status() {
    Core& c = core();
    std::lock_guard<std::mutex> l(c.mtx);
    Status s;
    s.state = c.state;
    if (!c.gops.empty()) s.bufferedSeconds = std::max(0.0, c.newestPts - c.gops.front().startPts);
    s.megabytes = (double)(c.videoBytes + c.audioBytes) / (1024.0 * 1024.0);
    s.width = c.frameW;
    s.height = c.frameH;
    s.frames = c.frames;
    s.audioChunks = c.audioChunks;
    s.orientationKey = c.orientationKey;
    s.codec = c.codecName;
    s.lastError = c.lastError;
    return s;
}

void requestClip(ClipRequest const& request, ClipFn onDone) {
    ClipRequest r = request;
    dispatch_async(core().exportQueue, ^{
        ClipResult res;
        @autoreleasepool {
            res = doExport(r);
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            if (onDone) onDone(res);
        });
    });
}

void share(std::string const& path) {
    std::string p = path;
    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController* top = topViewController();
        if (!top) return;
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:p.c_str()]];
        UIActivityViewController* vc = [[UIActivityViewController alloc] initWithActivityItems:@[url] applicationActivities:nil];
        UIPopoverPresentationController* pop = vc.popoverPresentationController;
        if (pop) { // iPad
            pop.sourceView = top.view;
            pop.sourceRect = CGRectMake(CGRectGetMidX(top.view.bounds), CGRectGetMidY(top.view.bounds), 1, 1);
            pop.permittedArrowDirections = (UIPopoverArrowDirection)0;
        }
        [top presentViewController:vc animated:YES completion:nil];
    });
}

void haptic(int kind) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (kind == 0) {
            UIImpactFeedbackGenerator* g = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
            [g impactOccurred];
        } else {
            UINotificationFeedbackGenerator* g = [[UINotificationFeedbackGenerator alloc] init];
            [g notificationOccurred:(kind == 1 ? UINotificationFeedbackTypeSuccess : UINotificationFeedbackTypeWarning)];
        }
    });
}

std::string systemLanguage() {
    NSString* l = [[NSLocale preferredLanguages] firstObject];
    if (!l) return "en";
    NSString* head = [[l componentsSeparatedByString:@"-"] firstObject];
    return head ? std::string(head.UTF8String) : std::string("en");
}

} // namespace clippy::native
