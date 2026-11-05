
#pragma once

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
        int rotation = 0;
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
        double startHost = 0.0;
        double endHost = 0.0;
        double highlightHost = 0.0;
        std::string directory;
        std::string baseName;
    };

    struct ClipResult {
        bool ok = false;
        std::string error;
        std::string videoPath;
        std::string thumbPath;
        double duration = 0.0;
        long long bytes = 0;
    };

    struct AttemptResult {
        bool ok = false;
        std::string error;
        std::string videoPath;
        double duration = 0.0;
        long long bytes = 0;
    };

    using LogFn = void (*)(int level, char const* message);
    using StateFn = std::function<void(CaptureState, std::string const&)>;
    using ClipFn = std::function<void(ClipResult const&)>;
    using AttemptFn = std::function<void(AttemptResult const&)>;

    void setLogger(LogFn fn);
    double hostNow();

    void configure(Config const& config);

    void start(StateFn onState);
    void stop();

    Status status();

    void requestClip(ClipRequest const& request, ClipFn onDone);

    void beginAttempt(std::string const& outputPath);
    void finishAttempt(AttemptFn onDone);
    void discardAttempt();

    void share(std::string const& path);
    void haptic(int kind);

    std::string systemLanguage();

}
