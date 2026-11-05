#pragma once

#include <Geode/Geode.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Native.hpp"

namespace clippy {

    // ---------------------------------------------------------------- settings (mod.json)
    namespace cfg {
        inline bool b(char const* key) { return geode::Mod::get()->getSettingValue<bool>(key); }
        inline int i(char const* key) { return static_cast<int>(geode::Mod::get()->getSettingValue<int64_t>(key)); }
        inline std::string s(char const* key) { return geode::Mod::get()->getSettingValue<std::string>(key); }
        inline void setB(char const* key, bool v) { geode::Mod::get()->setSettingValue<bool>(key, v); }
        inline void setI(char const* key, int v) { geode::Mod::get()->setSettingValue<int64_t>(key, v); }
        inline void setS(char const* key, std::string const& v) { geode::Mod::get()->setSettingValue<std::string>(key, v); }

        inline int bitrateKbps() {
            std::string q = s("quality");
            if (q == "Light") return 6000;
            if (q == "High") return 20000;
            if (q == "Max") return 32000;
            return 12000;
        }
        inline double estimatedMegabytes() { return bitrateKbps() * double(i("buffer-seconds")) / 8000.0; }
    }

    // ---------------------------------------------------------------- strings (Strings.cpp)
    std::string tr(char const* key);
    std::string tr(char const* key, std::initializer_list<std::string> args);
    bool french();

    // ---------------------------------------------------------------- clips
    enum class Reason { Manual, Death, Best, Complete };

    struct ClipInfo {
        std::string id;
        std::filesystem::path video;
        std::filesystem::path thumb;
        std::string level;
        int percent = -1; // -1: unknown (manual clip)
        int attempt = 0;
        double duration = 0.0;
        long long bytes = 0;
        long long created = 0; // unix seconds
        std::string reason;    // death | best | complete | manual
    };

    struct EngineEvent {
        enum class Kind { State, ClipStarted, ClipSaved, ClipFailed };
        Kind kind = Kind::State;
        ClipInfo clip;
        std::string text;
    };

    class Engine {
    public:
        static Engine& get();

        void init();                 // once, when the mod is loaded
        void menuShown();            // once the main menu exists: auto-start the buffer
        void applyConfig();          // push settings to the native recorder

        void startBuffer();
        void stopBuffer();
        native::Status status() const;
        bool bufferRunning() const;

        // gameplay events (fed by the PlayLayer hooks)
        void levelEntered(std::string const& name);
        void levelLeft();
        void runStarted();
        void death(int percent, bool practice);
        void levelComplete(bool practice);
        void paused();
        void resumed();
        bool inGameplay() const;
        bool completeHandled() const { return m_completeHandled; }

        // manual clip: the last N seconds (or up to the moment the game was paused)
        void clipLast(double seconds);
        void clipManual();

        // library
        std::vector<ClipInfo> const& clips();
        void reloadClips();
        void deleteClip(std::string const& id);
        void shareClip(ClipInfo const& clip) const;
        std::filesystem::path clipDir() const;

        void setListener(std::function<void(EngineEvent const&)> fn) { m_listener = std::move(fn); }

    private:
        void emit(EngineEvent::Kind kind, ClipInfo const& clip, std::string const& text);
        void requestClip(Reason reason, double startHost, double endHost, double highlightHost, int percent);
        void writeSidecar(ClipInfo const& clip) const;
        void prune();

        std::function<void(EngineEvent const&)> m_listener;
        std::vector<ClipInfo> m_clips;
        bool m_clipsLoaded = false;
        bool m_autoStarted = false;

        std::string m_level = "Level";
        bool m_inLevel = false;
        bool m_paused = false;
        bool m_deathHandled = false;
        bool m_completeHandled = false;
        double m_runStart = 0.0;
        double m_pauseHost = 0.0;
        double m_lastAutoClip = -100.0;
        int m_attempt = 0;
        int m_best = 0;
        int m_pending = 0;
    };

}
