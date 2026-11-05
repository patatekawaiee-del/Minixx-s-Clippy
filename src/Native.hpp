#pragma once

// Bridge between the C++ mod code and Native.mm (ReplayKit + VideoToolbox + AVFoundation).
// This header must stay free of Geode / cocos2d includes: it is also compiled as Objective-C++.
// Every callback is delivered on the main thread.

#include <functional>
#include <string>

namespace clippy::native {

    enum class CaptureState : int {
        Off = 0,
        Starting = 1,
        Running = 2,
        Denied = 3,
        Failed = 4,
    };

    struct Config {
        double bufferSeconds = 30.0;
        int bitrateKbps = 12000;
        bool hevc = false;
        int rotation = 0; // 0 auto, 1 none, 2 clockwise, 3 counter-clockwise, 4 upside down
    };

    struct Status {
        CaptureState state = CaptureState::Off;
        double bufferedSeconds = 0.0;
        double megabytes = 0.0;
        int width = 0;
        int height = 0;
        long long frames = 0;
        long long audioChunks = 0;
        int orientationKey = 0;
        std::string codec;
        std::string lastError;
    };

    struct ClipRequest {
        double startHost = 0.0;     // host-clock seconds (see hostNow) of the first wanted instant
        double endHost = 0.0;       // host-clock seconds of the last wanted instant
        double highlightHost = 0.0; // instant used for the thumbnail
        std::string directory;
        std::string baseName;       // file name without extension
    };

    struct ClipResult {
        bool ok = false;
        std::string error;
        std::string videoPath;
        std::string thumbPath;
        double duration = 0.0;
        long long bytes = 0;
    };

    using LogFn = void (*)(int level, char const* message); // 0 debug, 1 info, 2 warn, 3 error
    using StateFn = std::function<void(CaptureState, std::string const&)>;
    using ClipFn = std::function<void(ClipResult const&)>;

    void setLogger(LogFn fn);
    double hostNow();
    void configure(Config const& config);
    void start(StateFn onState);
    void stop();
    Status status();
    void requestClip(ClipRequest const& request, ClipFn onDone);
    void share(std::string const& path);
    void haptic(int kind); // 0 light, 1 success, 2 warning
    std::string systemLanguage();

}
