#include "Engine.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <unordered_map>

using namespace geode::prelude;

namespace clippy {

    namespace {
        std::string sanitize(std::string const& in) {
            std::string out;
            for (unsigned char ch : in) {
                if (std::isalnum(ch) || ch == '-' || ch == '_') out += static_cast<char>(ch);
                else if (ch == ' ') out += '_';
                if (out.size() >= 24) break;
            }
            return out.empty() ? std::string("clip") : out;
        }

        std::string oneLine(std::string s) {
            for (auto& ch : s) {
                if (ch == '\n' || ch == '\r' || ch == '=') ch = ' ';
            }
            return s;
        }

        char const* reasonName(Reason r) {
            switch (r) {
                case Reason::Death: return "death";
                case Reason::Best: return "best";
                case Reason::Complete: return "complete";
                default: return "manual";
            }
        }

        std::unordered_map<std::string, std::string> readKeyValues(std::filesystem::path const& file) {
            std::unordered_map<std::string, std::string> out;
            std::ifstream in(file);
            std::string line;
            while (std::getline(in, line)) {
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                out[line.substr(0, eq)] = line.substr(eq + 1);
            }
            return out;
        }

        int toInt(std::string const& s, int fallback) {
            if (s.empty()) return fallback;
            return std::atoi(s.c_str());
        }
    }

    Engine& Engine::get() {
        static Engine instance;
        return instance;
    }

    void Engine::init() {
        native::setLogger(+[](int level, char const* message) {
            switch (level) {
                case 0: log::debug("{}", message); break;
                case 2: log::warn("{}", message); break;
                case 3: log::error("{}", message); break;
                default: log::info("{}", message); break;
            }
        });
        applyConfig();
        log::info("Minixx's Clippy loaded");
    }

    void Engine::menuShown() {
        if (m_autoStarted) return;
        m_autoStarted = true;
        reloadClips();
        if (cfg::b("enabled") && cfg::b("auto-start")) startBuffer();
    }

    void Engine::applyConfig() {
        native::Config c;
        c.bufferSeconds = cfg::i("buffer-seconds");
        c.bitrateKbps = cfg::bitrateKbps();
        c.hevc = cfg::s("codec") == "HEVC";
        std::string rot = cfg::s("rotation");
        c.rotation = rot == "None" ? 1 : rot == "Clockwise" ? 2 : rot == "Counter-clockwise" ? 3 : rot == "Upside down" ? 4 : 0;
        native::configure(c);
    }

    void Engine::startBuffer() {
        applyConfig();
        native::start([this](native::CaptureState, std::string const& message) {
            emit(EngineEvent::Kind::State, ClipInfo{}, message);
        });
    }

    void Engine::stopBuffer() {
        native::stop();
    }

    native::Status Engine::status() const {
        return native::status();
    }

    bool Engine::bufferRunning() const {
        return native::status().state == native::CaptureState::Running;
    }

    void Engine::emit(EngineEvent::Kind kind, ClipInfo const& clip, std::string const& text) {
        if (!m_listener) return;
        EngineEvent e;
        e.kind = kind;
        e.clip = clip;
        e.text = text;
        m_listener(e);
    }

    // ------------------------------------------------------------------ gameplay events

    void Engine::levelEntered(std::string const& name) {
        m_level = name;
        m_inLevel = true;
        m_paused = false;
        m_attempt = 0;
        m_deathHandled = false;
        m_completeHandled = false;
        m_runStart = native::hostNow();
        m_best = Mod::get()->getSavedValue<int>("best:" + name, 0);
    }

    void Engine::levelLeft() {
        m_inLevel = false;
        m_paused = false;
    }

    void Engine::runStarted() {
        m_runStart = native::hostNow();
        m_attempt++;
        m_deathHandled = false;
        m_completeHandled = false;
        m_paused = false;
    }

    void Engine::paused() {
        m_paused = true;
        m_pauseHost = native::hostNow();
    }

    void Engine::resumed() {
        m_paused = false;
    }

    bool Engine::inGameplay() const {
        return m_inLevel && !m_paused;
    }

    void Engine::death(int percent, bool practice) {
        if (m_deathHandled) return;
        m_deathHandled = true;
        if (!cfg::b("enabled")) return;
        if (practice && !cfg::b("practice")) return;

        double now = native::hostNow();
        bool newBest = !practice && percent > m_best;
        if (newBest) {
            m_best = percent;
            Mod::get()->setSavedValue<int>("best:" + m_level, percent);
        }
        bool byDeath = cfg::b("on-death") && percent >= cfg::i("death-percent");
        bool byBest = newBest && cfg::b("on-best") && percent >= cfg::i("best-min-percent");
        if (!byDeath && !byBest) return;
        if (now - m_lastAutoClip < 3.0) return; // never spam clips
        m_lastAutoClip = now;

        requestClip(byBest ? Reason::Best : Reason::Death,
            m_runStart - cfg::i("preroll"), now + cfg::i("postroll"), now, percent);
    }

    void Engine::levelComplete(bool practice) {
        if (m_completeHandled) return;
        m_completeHandled = true;
        m_best = 100;
        if (!cfg::b("enabled") || !cfg::b("on-complete")) return;
        if (practice && !cfg::b("practice")) return;
        if (!practice) Mod::get()->setSavedValue<int>("best:" + m_level, 100);

        double now = native::hostNow();
        requestClip(Reason::Complete, m_runStart - cfg::i("preroll"), now + std::max(cfg::i("postroll"), 3), now, 100);
    }

    void Engine::clipLast(double seconds) {
        double end = (m_inLevel && m_paused) ? m_pauseHost : native::hostNow();
        requestClip(Reason::Manual, end - seconds, end, end - 0.2, -1);
    }

    void Engine::clipManual() {
        clipLast(cfg::i("manual-seconds"));
    }

    // ------------------------------------------------------------------ clip requests

    void Engine::requestClip(Reason reason, double startHost, double endHost, double highlightHost, int percent) {
        ClipInfo pending;
        pending.level = (m_inLevel || reason != Reason::Manual) ? m_level : std::string("Clippy");
        pending.percent = percent;
        pending.attempt = m_attempt;
        pending.reason = reasonName(reason);
        pending.created = static_cast<long long>(std::time(nullptr));

        if (!bufferRunning()) {
            emit(EngineEvent::Kind::ClipFailed, pending, tr("err.notrunning"));
            return;
        }

        double maxLen = cfg::i("max-clip-seconds");
        if (endHost - startHost > maxLen) startHost = endHost - maxLen;

        std::time_t t = std::time(nullptr);
        std::tm lt{};
        localtime_r(&t, &lt);
        char stampBuf[32];
        std::strftime(stampBuf, sizeof stampBuf, "%Y%m%d-%H%M%S", &lt);
        static std::string lastStamp;
        static int duplicates = 0;
        std::string stamp = stampBuf;
        if (stamp == lastStamp) stamp += "-" + std::to_string(++duplicates);
        else {
            lastStamp = stamp;
            duplicates = 0;
        }

        std::string base = sanitize(pending.level) + "_" + (percent >= 0 ? std::to_string(percent) + "pct_" : std::string()) + stamp;
        pending.id = base;

        std::error_code ec;
        std::filesystem::create_directories(clipDir(), ec);

        native::ClipRequest req;
        req.startHost = startHost;
        req.endHost = endHost;
        req.highlightHost = highlightHost;
        req.directory = clipDir().string();
        req.baseName = base;

        emit(EngineEvent::Kind::ClipStarted, pending, std::string());
        ++m_pending;
        native::requestClip(req, [this, pending](native::ClipResult const& r) mutable {
            --m_pending;
            if (!r.ok) {
                log::warn("Clip failed: {}", r.error);
                emit(EngineEvent::Kind::ClipFailed, pending, r.error);
                return;
            }
            pending.video = r.videoPath;
            pending.thumb = r.thumbPath;
            pending.duration = r.duration;
            pending.bytes = r.bytes;
            writeSidecar(pending);
            clips(); // make sure the library is loaded before we insert
            m_clips.insert(m_clips.begin(), pending);
            prune();
            emit(EngineEvent::Kind::ClipSaved, pending, std::string());
        });
    }

    // ------------------------------------------------------------------ library

    std::filesystem::path Engine::clipDir() const {
        return Mod::get()->getSaveDir() / "clips";
    }

    void Engine::writeSidecar(ClipInfo const& clip) const {
        std::ofstream out(clipDir() / (clip.id + ".clip"));
        if (!out) return;
        out << "level=" << oneLine(clip.level) << "\n"
            << "percent=" << clip.percent << "\n"
            << "attempt=" << clip.attempt << "\n"
            << "reason=" << clip.reason << "\n"
            << "created=" << clip.created << "\n"
            << "duration=" << clip.duration << "\n";
    }

    std::vector<ClipInfo> const& Engine::clips() {
        if (!m_clipsLoaded) reloadClips();
        return m_clips;
    }

    void Engine::reloadClips() {
        m_clipsLoaded = true;
        m_clips.clear();
        std::error_code ec;
        for (auto const& entry : std::filesystem::directory_iterator(clipDir(), ec)) {
            if (ec) break;
            auto const& path = entry.path();
            if (path.extension() != ".mp4") continue;

            ClipInfo info;
            info.id = path.stem().string();
            info.video = path;
            info.level = info.id;

            auto thumb = path;
            thumb.replace_extension(".png");
            if (std::filesystem::exists(thumb, ec)) info.thumb = thumb;

            auto side = path;
            side.replace_extension(".clip");
            auto kv = readKeyValues(side);
            if (kv.count("level")) info.level = kv["level"];
            info.percent = toInt(kv["percent"], -1);
            info.attempt = toInt(kv["attempt"], 0);
            info.reason = kv.count("reason") ? kv["reason"] : std::string("manual");
            info.duration = kv.count("duration") ? std::atof(kv["duration"].c_str()) : 0.0;
            info.created = kv.count("created") ? std::atoll(kv["created"].c_str()) : 0;

            struct stat st {};
            if (::stat(path.c_str(), &st) == 0) {
                info.bytes = static_cast<long long>(st.st_size);
                if (info.created == 0) info.created = static_cast<long long>(st.st_mtime);
            }
            m_clips.push_back(std::move(info));
        }
        std::sort(m_clips.begin(), m_clips.end(), [](ClipInfo const& a, ClipInfo const& b) {
            return a.created > b.created;
        });
    }

    void Engine::deleteClip(std::string const& id) {
        auto it = std::find_if(m_clips.begin(), m_clips.end(), [&](ClipInfo const& c) { return c.id == id; });
        if (it == m_clips.end()) return;
        std::error_code ec;
        std::filesystem::remove(it->video, ec);
        if (!it->thumb.empty()) std::filesystem::remove(it->thumb, ec);
        std::filesystem::remove(clipDir() / (id + ".clip"), ec);
        m_clips.erase(it);
    }

    void Engine::prune() {
        size_t keep = static_cast<size_t>(std::max(5, cfg::i("max-clips")));
        while (m_clips.size() > keep) {
            std::string id = m_clips.back().id;
            deleteClip(id);
        }
    }

    void Engine::shareClip(ClipInfo const& clip) const {
        native::share(clip.video.string());
    }

}
