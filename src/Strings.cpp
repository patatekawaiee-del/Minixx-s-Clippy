#include <initializer_list>
#include <string>
#include <unordered_map>

#include "Engine.hpp"

namespace clippy {

    namespace {

        struct Pair {
            char const* en;
            char const* fr;
        };

        std::unordered_map<std::string, Pair> const& table() {
            static const std::unordered_map<std::string, Pair> t = {
                // buffer state
                {"state.off", {"Buffer off", "Tampon inactif"}},
                {"state.starting", {"Starting...", "Démarrage..."}},
                {"state.running", {"REC {} / {} s", "REC {} / {} s"}},
                {"state.denied", {"Permission refused", "Permission refusée"}},
                {"state.failed", {"Capture error", "Erreur de capture"}},
                {"banner.denied", {"Screen recording was refused. Tap Start buffer to try again.", "L'enregistrement d'écran a été refusé. Appuie sur Lancer le tampon pour réessayer."}},
                {"err.notrunning", {"The buffer is not running.", "Le tampon n'est pas lancé."}},

                // buttons
                {"btn.clip", {"Clip last {}s", "Clipper les {} dernières s"}},
                {"btn.start", {"Start buffer", "Lancer le tampon"}},
                {"btn.stop", {"Stop", "Arrêter"}},
                {"btn.share", {"Share", "Partager"}},
                {"btn.delete", {"Delete", "Suppr."}},
                {"btn.confirm", {"Sure?", "Sûr ?"}},
                {"btn.open", {"Open", "Ouvrir"}},

                // empty state
                {"empty.title", {"No clips yet", "Aucun clip pour l'instant"}},
                {"empty.body", {"Die past {}% and I'll save the moment.\nIt looks like you're about to beat this level!", "Meurs après {}% et je sauvegarde le moment.\nOn dirait que tu vas battre ce niveau !"}},

                // clip cards
                {"reason.death", {"Death", "Mort"}},
                {"reason.best", {"New best", "Record"}},
                {"reason.complete", {"Complete", "Victoire"}},
                {"reason.manual", {"Manual", "Manuel"}},
                {"age.now", {"just now", "à l'instant"}},
                {"age.min", {"{} min ago", "il y a {} min"}},
                {"age.hour", {"{} h ago", "il y a {} h"}},
                {"age.day", {"{} d ago", "il y a {} j"}},

                // toasts
                {"toast.saving", {"Saving clip...", "Sauvegarde du clip..."}},
                {"toast.saved", {"Clip saved", "Clip sauvegardé"}},
                {"toast.failed", {"Clip failed", "Échec du clip"}},
                {"quip.0", {"It looks like you almost had it!", "On dirait que tu y étais presque !"}},
                {"quip.1", {"That was close. Want a tissue?", "C'était limite. Un mouchoir ?"}},
                {"quip.2", {"Saved. Future you says thanks.", "Sauvegardé. Ton futur toi te remercie."}},
                {"quip.3", {"That spike came out of nowhere.", "Ce spike est sorti de nulle part."}},
                {"quip.4", {"Keeping this one for the highlights.", "Je garde ça pour les meilleurs moments."}},
                {"quip.5", {"Pro tip: try not dying next time.", "Astuce : essaie de ne pas mourir."}},

                // settings tabs and rows
                {"tab.buffer", {"Buffer", "Tampon"}},
                {"tab.triggers", {"Triggers", "Déclencheurs"}},
                {"tab.timing", {"Timing", "Durées"}},
                {"tab.clippy", {"Clippy", "Clippy"}},
                {"row.buffer-seconds", {"Buffer length", "Durée du tampon"}},
                {"row.quality", {"Quality", "Qualité"}},
                {"row.codec", {"Codec", "Codec"}},
                {"row.auto-start", {"Start with the game", "Lancer avec le jeu"}},
                {"row.rotation", {"Rotation fix", "Correction rotation"}},
                {"row.on-death", {"Clip deaths", "Clipper les morts"}},
                {"row.death-percent", {"Minimum percent", "Pourcentage min."}},
                {"row.on-best", {"Clip new bests", "Clipper les records"}},
                {"row.best-min-percent", {"New bests from", "Records dès"}},
                {"row.on-complete", {"Clip completions", "Clipper les victoires"}},
                {"row.practice", {"Practice runs", "Runs en practice"}},
                {"row.preroll", {"Pre-roll", "Avant la run"}},
                {"row.postroll", {"Post-roll", "Après la mort"}},
                {"row.manual-seconds", {"Manual clip length", "Durée du clip manuel"}},
                {"row.max-clip-seconds", {"Max clip length", "Durée max d'un clip"}},
                {"row.max-clips", {"Clips to keep", "Clips conservés"}},
                {"row.mascot", {"Clippy reactions", "Réactions de Clippy"}},
                {"row.ingame-toast", {"Reactions while playing", "Réactions en jeu"}},
                {"row.haptics", {"Haptics", "Vibrations"}},
                {"row.language", {"Language", "Langue"}},
                {"row.diagnostics", {"Diagnostics", "Diagnostic"}},

                // option labels
                {"opt.Auto", {"Auto", "Auto"}},
                {"opt.Light", {"Light", "Légère"}},
                {"opt.Balanced", {"Balanced", "Équilibrée"}},
                {"opt.High", {"High", "Haute"}},
                {"opt.Max", {"Max", "Max"}},
                {"opt.English", {"English", "Anglais"}},
                {"opt.French", {"French", "Français"}},
                {"opt.None", {"None", "Aucune"}},
                {"opt.Clockwise", {"Clockwise", "Sens horaire"}},
                {"opt.Counter-clockwise", {"Counter-clockwise", "Sens anti-horaire"}},
                {"opt.Upside down", {"Upside down", "À l'envers"}},
            };
            return t;
        }

        std::string fillArgs(std::string text, std::initializer_list<std::string> args) {
            size_t pos = 0;
            for (auto const& a : args) {
                pos = text.find("{}", pos);
                if (pos == std::string::npos) break;
                text.replace(pos, 2, a);
                pos += a.size();
            }
            return text;
        }

    }

    bool french() {
        std::string l = cfg::s("language");
        if (l == "French") return true;
        if (l == "English") return false;
        static std::string system = native::systemLanguage();
        return system.rfind("fr", 0) == 0;
    }

    std::string tr(char const* key) {
        auto const& t = table();
        auto it = t.find(key);
        if (it == t.end()) return key;
        return french() ? it->second.fr : it->second.en;
    }

    std::string tr(char const* key, std::initializer_list<std::string> args) {
        return fillArgs(tr(key), args);
    }

}
