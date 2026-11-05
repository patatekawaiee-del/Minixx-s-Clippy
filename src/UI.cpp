#include "UI.hpp"

#include <Geode/ui/OverlayManager.hpp>
#include <Geode/utils/cocos.hpp>

#include <cstdio>
#include <ctime>

#include "Engine.hpp"
#include "Widgets.hpp"

using namespace geode::prelude;
using namespace clippy::ui::kit;

namespace clippy::ui {

    // ====================================================================== helpers

    namespace {

        std::string fmtDuration(double seconds) {
            int s = static_cast<int>(seconds + 0.5);
            char buf[16];
            std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
            return buf;
        }

        std::string fmtSize(long long bytes) {
            char buf[24];
            std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
            return buf;
        }

        std::string fmtAge(long long created) {
            long long d = static_cast<long long>(std::time(nullptr)) - created;
            if (d < 0) d = 0;
            if (d < 60) return tr("age.now");
            if (d < 3600) return tr("age.min", {std::to_string(d / 60)});
            if (d < 86400) return tr("age.hour", {std::to_string(d / 3600)});
            return tr("age.day", {std::to_string(d / 86400)});
        }

        int reasonColor(std::string const& reason) {
            if (reason == "best") return kAmber;
            if (reason == "complete") return kMint;
            if (reason == "manual") return kSilver;
            return kRec;
        }

        std::string reasonText(std::string const& reason) {
            return tr(("reason." + reason).c_str());
        }

        std::string clip(std::string s, size_t n) {
            if (s.size() > n) s = s.substr(0, n - 1) + "...";
            return s;
        }

        std::string stateText(native::Status const& st) {
            switch (st.state) {
                case native::CaptureState::Running: return tr("state.running", {std::to_string(static_cast<int>(st.bufferedSeconds + 0.5)), std::to_string(cfg::i("buffer-seconds"))});
                case native::CaptureState::Starting: return tr("state.starting");
                case native::CaptureState::Denied: return tr("state.denied");
                case native::CaptureState::Failed: return tr("state.failed");
                default: return tr("state.off");
            }
        }

        int stateColor(native::CaptureState s) {
            switch (s) {
                case native::CaptureState::Running: return kMint;
                case native::CaptureState::Starting: return kAmber;
                case native::CaptureState::Denied:
                case native::CaptureState::Failed: return kRec;
                default: return kDim;
            }
        }

        // ---------------------------------------------------------------- settings model

        struct Row {
            enum class Kind { Switch, Stepper, Cycle, Action };
            Kind kind;
            char const* key;
            char const* label; // translation key
            int minV = 0;
            int maxV = 0;
            int step = 1;
            char const* unit = "";
            std::vector<std::string> options;
        };

        std::vector<std::vector<Row>> const& settingsTabs() {
            static std::vector<std::vector<Row>> tabs = {
                {
                    {Row::Kind::Stepper, "buffer-seconds", "row.buffer-seconds", 10, 120, 5, " s", {}},
                    {Row::Kind::Cycle, "quality", "row.quality", 0, 0, 0, "", {"Light", "Balanced", "High", "Max"}},
                    {Row::Kind::Cycle, "codec", "row.codec", 0, 0, 0, "", {"H.264", "HEVC"}},
                    {Row::Kind::Switch, "auto-start", "row.auto-start", 0, 0, 0, "", {}},
                    {Row::Kind::Cycle, "rotation", "row.rotation", 0, 0, 0, "", {"Auto", "None", "Clockwise", "Counter-clockwise", "Upside down"}},
                },
                {
                    {Row::Kind::Switch, "on-death", "row.on-death", 0, 0, 0, "", {}},
                    {Row::Kind::Stepper, "death-percent", "row.death-percent", 1, 99, 5, "%", {}},
                    {Row::Kind::Switch, "on-best", "row.on-best", 0, 0, 0, "", {}},
                    {Row::Kind::Stepper, "best-min-percent", "row.best-min-percent", 1, 99, 5, "%", {}},
                    {Row::Kind::Switch, "on-complete", "row.on-complete", 0, 0, 0, "", {}},
                    {Row::Kind::Switch, "practice", "row.practice", 0, 0, 0, "", {}},
                },
                {
                    {Row::Kind::Stepper, "preroll", "row.preroll", 0, 15, 1, " s", {}},
                    {Row::Kind::Stepper, "postroll", "row.postroll", 0, 8, 1, " s", {}},
                    {Row::Kind::Stepper, "manual-seconds", "row.manual-seconds", 5, 60, 5, " s", {}},
                    {Row::Kind::Stepper, "max-clip-seconds", "row.max-clip-seconds", 10, 120, 10, " s", {}},
                    {Row::Kind::Stepper, "max-clips", "row.max-clips", 5, 300, 5, "", {}},
                },
                {
                    {Row::Kind::Switch, "mascot", "row.mascot", 0, 0, 0, "", {}},
                    {Row::Kind::Switch, "ingame-toast", "row.ingame-toast", 0, 0, 0, "", {}},
                    {Row::Kind::Switch, "haptics", "row.haptics", 0, 0, 0, "", {}},
                    {Row::Kind::Cycle, "language", "row.language", 0, 0, 0, "", {"Auto", "English", "French"}},
                    {Row::Kind::Action, "diagnostics", "row.diagnostics", 0, 0, 0, "", {}},
                },
            };
            return tabs;
        }

        std::string rowValueText(Row const& r) {
            if (r.kind == Row::Kind::Stepper) {
                int v = cfg::i(r.key);
                std::string text = std::to_string(v) + r.unit;
                if (std::string(r.key) == "buffer-seconds") {
                    char buf[24];
                    std::snprintf(buf, sizeof buf, "  ~%d MB", static_cast<int>(cfg::estimatedMegabytes() + 0.5));
                    text += buf;
                }
                return text;
            }
            if (r.kind == Row::Kind::Cycle) {
                std::string v = cfg::s(r.key);
                if (std::string(r.key) == "quality" || std::string(r.key) == "language" || std::string(r.key) == "rotation") {
                    std::string t = tr(("opt." + v).c_str());
                    return t;
                }
                return v;
            }
            return std::string();
        }

    }

    // ====================================================================== button faces for the menus

    CCNode* clippyButtonFace(float d) {
        auto icon = CCDrawNode::create();
        drawPaperclip(icon, d * 0.62f, 0.f, 0.f, true);
        return circleFace(d, kCard, icon);
    }

    CCNode* clipNowButtonFace(float d) {
        auto icon = CCDrawNode::create();
        disc(icon, ccp(0, 0), d * 0.3f, col4(kRec), 28);
        disc(icon, ccp(0, 0), d * 0.22f, col4(kCard), 28);
        disc(icon, ccp(0, 0), d * 0.13f, col4(kRec), 24);
        return circleFace(d, kCard, icon);
    }

    // ====================================================================== toast (Clippy + speech bubble)

    namespace {

        class Toast : public CCNode {
        public:
            static Toast* create() {
                auto r = new Toast();
                if (r->CCNode::init()) {
                    r->autorelease();
                    r->setVisible(false);
                    return r;
                }
                delete r;
                return nullptr;
            }

            void present(std::string const& title, std::string const& sub, Mascot::Mood mood, std::filesystem::path const& thumb, float seconds) {
                auto win = CCDirector::sharedDirector()->getWinSize();
                this->stopAllActions();
                this->removeAllChildrenWithCleanup(true);
                this->setVisible(true);

                auto mascot = Mascot::create(48.f);
                float mw = mascot->getContentSize().width;
                mascot->setPosition({mw / 2, 0.f});
                mascot->setMood(mood);
                mascot->lookTo(1.f, -0.2f);
                this->addChild(mascot, 2);

                auto t = label(title, "chatFont.fnt", 0.62f, kBlack, {0.f, 0.5f});
                auto s = label(sub, "chatFont.fnt", 0.46f, 0x56586A, {0.f, 0.5f});
                float textW = std::min(std::max(labelWidth(t), labelWidth(s)), 190.f);
                fit(t, textW, 0.62f);
                fit(s, textW, 0.46f);

                CCSprite* thumbSprite = nullptr;
                float thumbW = 0.f, thumbH = 0.f;
                if (!thumb.empty()) {
                    thumbSprite = CCSprite::create(thumb.string().c_str());
                    if (thumbSprite && thumbSprite->getContentSize().width > 1.f) {
                        thumbW = 62.f;
                        thumbH = thumbW * thumbSprite->getContentSize().height / thumbSprite->getContentSize().width;
                        thumbH = std::min(thumbH, 40.f);
                    } else {
                        thumbSprite = nullptr;
                    }
                }

                float bw = std::max(120.f, textW + 24.f + (thumbSprite ? thumbW + 8.f : 0.f));
                float bh = 44.f;
                auto bub = bubble(bw, bh);
                t->setPosition({12.f, bh * 0.66f});
                s->setPosition({12.f, bh * 0.30f});
                bub->addChild(t, 5);
                bub->addChild(s, 5);
                if (thumbSprite) {
                    auto cs = thumbSprite->getContentSize();
                    float scale = thumbW / cs.width;
                    if (cs.height * scale > thumbH) {
                        // crop vertically so the thumbnail keeps a tidy box
                        float keep = thumbH / scale;
                        thumbSprite->setTextureRect(CCRect(0.f, (cs.height - keep) / 2, cs.width, keep));
                    }
                    thumbSprite->setScale(scale);
                    thumbSprite->setPosition({bw - 6.f - thumbW / 2, bh / 2});
                    auto frame = box(thumbW + 3.f, thumbH + 3.f, kBlack);
                    frame->setPosition(thumbSprite->getPosition());
                    bub->addChild(frame, 4);
                    bub->addChild(thumbSprite, 5);
                }

                auto menu = CCMenu::create();
                menu->setPosition({0.f, 0.f});
                auto item = CCMenuItemSpriteExtra::create(bub, nullptr, this, menu_selector(Toast::onTap));
                item->setPosition({mw + 12.f + bw / 2, 4.f});
                menu->addChild(item);
                this->addChild(menu, 3);

                float restX = 18.f;
                float restY = win.height - 42.f;
                this->setPosition({restX, win.height + 80.f});
                this->runAction(CCSequence::create(
                    CCEaseBackOut::create(CCMoveTo::create(0.42f, ccp(restX, restY))),
                    CCDelayTime::create(seconds),
                    CCEaseIn::create(CCMoveTo::create(0.3f, ccp(restX, win.height + 80.f)), 2.f),
                    CCCallFunc::create(this, callfunc_selector(Toast::hide)),
                    nullptr));
            }

        private:
            void hide() { this->setVisible(false); }

            void onTap(CCObject*) {
                this->stopAllActions();
                this->setVisible(false);
                openStudio();
            }
        };

        Toast* s_toast = nullptr;

        char const* kQuips[6] = {"quip.0", "quip.1", "quip.2", "quip.3", "quip.4", "quip.5"};

        bool wantToast() {
            return cfg::b("mascot") && (!Engine::get().inGameplay() || cfg::b("ingame-toast"));
        }

    }

    // ====================================================================== Studio

    namespace {

        constexpr float kFooterH = 46.f;
        constexpr float kHeaderH = 52.f;

        class StudioLayer : public FLAlertLayer {
        public:
            static StudioLayer* s_open;

            static void open() {
                if (s_open) return;
                auto layer = new StudioLayer();
                if (layer->setup()) {
                    layer->autorelease();
                    s_open = layer;
                    layer->show();
                    layer->animateIn();
                } else {
                    delete layer;
                }
            }

            static void refreshIfOpen() {
                if (s_open) s_open->refresh();
            }

            ~StudioLayer() {
                if (s_open == this) s_open = nullptr;
            }

            void keyBackClicked() override { this->closeStudio(); }

        private:
            enum class Mode { Clips, Settings };

            struct RowUi {
                CCLabelBMFont* value = nullptr;
                CCDrawNode* knob = nullptr;
                CCScale9Sprite* track = nullptr;
            };

            float m_w = 0.f;
            float m_h = 0.f;
            CCNode* m_root = nullptr;
            CCNode* m_body = nullptr;
            CCMenu* m_headerMenu = nullptr;
            CCNode* m_chip = nullptr;
            CCLabelBMFont* m_info = nullptr;
            Mascot* m_mascot = nullptr;
            float m_titleRight = 0.f;
            Mode m_mode = Mode::Clips;
            int m_page = 0;
            int m_slide = 0;
            int m_tab = 0;
            int m_pendingDelete = -1;
            bool m_closing = false;
            std::vector<RowUi> m_rowUi;

            // ------------------------------------------------------------ setup

            bool setup() {
                if (!FLAlertLayer::init(0)) return false;
                m_noElasticity = true;
                if (!m_mainLayer) {
                    m_mainLayer = CCLayer::create();
                    this->addChild(m_mainLayer);
                }
                this->setKeypadEnabled(true);
                buildFrame();
                rebuildChip();
                showMode(Mode::Clips);
                this->schedule(schedule_selector(StudioLayer::tick), 0.5f);
                return true;
            }

            void buildFrame() {
                auto win = CCDirector::sharedDirector()->getWinSize();
                m_w = std::min(win.width - 40.f, 470.f);
                m_h = std::min(win.height - 24.f, 262.f);

                m_root = CCNode::create();
                m_root->setContentSize({m_w, m_h});
                m_root->setAnchorPoint({0.5f, 0.5f});
                m_root->setPosition({win.width / 2, win.height / 2});
                m_mainLayer->addChild(m_root);

                auto shadow = box(m_w + 18.f, m_h + 18.f, 0x000000, 120);
                shadow->setPosition({m_w / 2, m_h / 2 - 5.f});
                m_root->addChild(shadow, 0);
                auto edge = box(m_w + 2.5f, m_h + 2.5f, kEdge);
                edge->setPosition({m_w / 2, m_h / 2});
                m_root->addChild(edge, 1);
                auto panel = box(m_w, m_h, kSurface, 252);
                panel->setPosition({m_w / 2, m_h / 2});
                m_root->addChild(panel, 2);

                auto head = box(m_w - 16.f, 38.f, kCard);
                head->setPosition({m_w / 2, m_h - 27.f});
                m_root->addChild(head, 3);

                m_mascot = Mascot::create(34.f);
                m_mascot->setPosition({30.f, m_h - 27.f});
                m_root->addChild(m_mascot, 6);

                auto title = label("Clippy", "bigFont.fnt", 0.46f, kText, {0.f, 0.5f});
                title->setPosition({52.f, m_h - 27.f});
                m_root->addChild(title, 6);
                m_titleRight = 52.f + labelWidth(title);

                m_body = CCNode::create();
                m_body->setContentSize({m_w, m_h});
                m_body->setAnchorPoint({0.f, 0.f});
                m_body->setPosition({0.f, 0.f});
                m_root->addChild(m_body, 4);
            }

            void buildHeaderButtons() {
                if (m_headerMenu) m_headerMenu->removeFromParentAndCleanup(true);
                m_headerMenu = CCMenu::create();
                m_headerMenu->setPosition({0.f, 0.f});
                m_root->addChild(m_headerMenu, 7);

                auto close = CCMenuItemSpriteExtra::create(circleFace(26.f, kCardDeep, iconClose(26.f, kText)), nullptr, this, menu_selector(StudioLayer::onClose));
                close->setPosition({m_w - 27.f, m_h - 27.f});
                m_headerMenu->addChild(close);

                CCNode* icon = m_mode == Mode::Clips ? static_cast<CCNode*>(iconTune(26.f, kText)) : static_cast<CCNode*>(iconChevron(26.f, kText, true));
                auto mode = CCMenuItemSpriteExtra::create(circleFace(26.f, kCardDeep, icon), nullptr, this, menu_selector(StudioLayer::onMode));
                mode->setPosition({m_w - 27.f - 32.f, m_h - 27.f});
                m_headerMenu->addChild(mode);

                geode::cocos::handleTouchPriority(this);
            }

            void rebuildChip() {
                if (m_chip) m_chip->removeFromParentAndCleanup(true);
                auto st = Engine::get().status();
                auto text = stateText(st);
                auto l = label(text, "chatFont.fnt", 0.5f, kText, {0.f, 0.5f});
                fit(l, 150.f, 0.5f);
                float w = labelWidth(l) + 28.f;
                m_chip = CCNode::create();
                m_chip->setContentSize({w, 18.f});
                auto e = box(w + 2.f, 20.f, kEdge);
                e->setPosition({w / 2, 9.f});
                m_chip->addChild(e);
                auto b = box(w, 18.f, kCardDeep);
                b->setPosition({w / 2, 9.f});
                m_chip->addChild(b, 1);
                auto dot = CCDrawNode::create();
                disc(dot, ccp(10.f, 9.f), 3.6f, col4(stateColor(st.state)), 16);
                m_chip->addChild(dot, 2);
                l->setPosition({19.f, 9.f});
                m_chip->addChild(l, 2);
                m_chip->setPosition({m_titleRight + 12.f, m_h - 27.f - 9.f});
                m_root->addChild(m_chip, 6);

                if (m_mascot) {
                    using M = Mascot::Mood;
                    m_mascot->setMood(st.state == native::CaptureState::Denied || st.state == native::CaptureState::Failed ? M::Worried : M::Happy);
                }
            }

            // ------------------------------------------------------------ animations

            void animateIn() {
                auto win = CCDirector::sharedDirector()->getWinSize();
                this->setOpacity(0);
                this->runAction(CCFadeTo::create(0.18f, 170));
                m_root->setScale(0.88f);
                m_root->setPosition({win.width / 2, win.height / 2 - 14.f});
                m_root->runAction(CCEaseBackOut::create(CCScaleTo::create(0.3f, 1.f)));
                m_root->runAction(CCEaseExponentialOut::create(CCMoveTo::create(0.3f, ccp(win.width / 2, win.height / 2))));
            }

            void closeStudio() {
                if (m_closing) return;
                m_closing = true;
                this->setKeypadEnabled(false);
                this->runAction(CCFadeTo::create(0.14f, 0));
                m_root->runAction(CCEaseIn::create(CCScaleTo::create(0.14f, 0.92f), 2.f));
                this->runAction(CCSequence::create(
                    CCDelayTime::create(0.16f),
                    CCCallFunc::create(this, callfunc_selector(StudioLayer::finishClose)),
                    nullptr));
            }

            void finishClose() { this->removeFromParentAndCleanup(true); }

            // ------------------------------------------------------------ refresh

            void refresh() {
                if (m_closing) return;
                rebuildChip();
                if (m_mode == Mode::Clips) buildClips();
            }

            void tick(float) {
                if (m_closing) return;
                auto st = Engine::get().status();
                // update the chip only when its text changed, to avoid rebuilding every tick
                static std::string last;
                std::string text = stateText(st);
                if (text != last) {
                    last = text;
                    rebuildChip();
                }
                if (m_info) {
                    char buf[96];
                    std::snprintf(buf, sizeof buf, "%dx%d  %s  %.1f MB", st.width, st.height, st.codec.empty() ? "-" : st.codec.c_str(), st.megabytes);
                    m_info->setString(st.state == native::CaptureState::Running ? buf : "");
                }
            }

            // ------------------------------------------------------------ modes

            void showMode(Mode mode) {
                m_mode = mode;
                m_pendingDelete = -1;
                buildHeaderButtons();
                if (mode == Mode::Clips) buildClips();
                else buildSettings();
            }

            void onMode(CCObject*) { showMode(m_mode == Mode::Clips ? Mode::Settings : Mode::Clips); }
            void onClose(CCObject*) { closeStudio(); }

            // ------------------------------------------------------------ clips page

            CCNode* thumbNode(ClipInfo const& c, float w, float h) {
                auto holder = CCNode::create();
                holder->setContentSize({w, h});
                holder->setAnchorPoint({0.f, 0.f});
                auto frame = box(w + 2.f, h + 2.f, kEdge);
                frame->setPosition({w / 2, h / 2});
                holder->addChild(frame, 0);
                auto back = box(w, h, kInk);
                back->setPosition({w / 2, h / 2});
                holder->addChild(back, 1);
                if (!c.thumb.empty()) {
                    if (auto spr = CCSprite::create(c.thumb.string().c_str())) {
                        auto sz = spr->getContentSize();
                        if (sz.width > 1.f && sz.height > 1.f) {
                            float target = w / h;
                            float aspect = sz.width / sz.height;
                            CCRect r(0.f, 0.f, sz.width, sz.height);
                            if (aspect > target) {
                                float nw = sz.height * target;
                                r = CCRect((sz.width - nw) / 2, 0.f, nw, sz.height);
                            } else {
                                float nh = sz.width / target;
                                r = CCRect(0.f, (sz.height - nh) / 2, sz.width, nh);
                            }
                            spr->setTextureRect(r);
                            spr->setScale(w / r.size.width);
                            spr->setPosition({w / 2, h / 2});
                            holder->addChild(spr, 2);
                        }
                    }
                }
                return holder;
            }

            CCNode* badge(std::string const& text, int fill, int textHex, float scale = 0.42f) {
                auto l = label(text, "chatFont.fnt", scale, textHex);
                float w = labelWidth(l) + 12.f;
                auto n = CCNode::create();
                n->setContentSize({w, 14.f});
                n->setAnchorPoint({0.f, 0.f});
                auto b = box(w, 14.f, fill);
                b->setPosition({w / 2, 7.f});
                n->addChild(b);
                l->setPosition({w / 2, 7.5f});
                n->addChild(l, 1);
                return n;
            }

            void buildClips() {
                m_body->removeAllChildrenWithCleanup(true);
                m_info = nullptr;

                auto const& clips = Engine::get().clips();
                int n = static_cast<int>(clips.size());
                int pages = std::max(1, (n + 2) / 3);
                m_page = std::clamp(m_page, 0, pages - 1);

                auto menu = CCMenu::create();
                menu->setPosition({0.f, 0.f});
                m_body->addChild(menu, 30);

                if (n == 0) buildEmpty();
                else buildCards(clips, pages, menu);
                buildFooter(menu);

                m_slide = 0;
                geode::cocos::handleTouchPriority(this);
            }

            void buildEmpty() {
                float cy = kFooterH + (m_h - kFooterH - kHeaderH) / 2;
                auto big = Mascot::create(86.f);
                big->setPosition({m_w * 0.26f, cy});
                big->setMood(Mascot::Mood::Neutral);
                m_body->addChild(big, 5);

                auto title = tr("empty.title");
                auto body = tr("empty.body", {std::to_string(cfg::i("death-percent"))});
                auto t = label(title, "chatFont.fnt", 0.66f, kBlack, {0.f, 0.5f});
                auto b = label(body, "chatFont.fnt", 0.48f, 0x4A4C5E, {0.f, 1.f});
                float tw = std::max(labelWidth(t), labelWidth(b));
                float bw = std::min(tw + 26.f, m_w * 0.5f);
                fit(t, bw - 26.f, 0.66f);
                fit(b, bw - 26.f, 0.48f);
                float bh = 70.f;
                auto bub = bubble(bw, bh);
                bub->setPosition({m_w * 0.26f + 44.f + 12.f + bw / 2, cy + 4.f});
                t->setPosition({13.f, bh - 16.f});
                b->setPosition({13.f, bh - 28.f});
                bub->addChild(t, 5);
                bub->addChild(b, 5);
                m_body->addChild(bub, 5);
            }

            void buildCards(std::vector<ClipInfo> const& clips, int pages, CCMenu* navMenu) {
                int n = static_cast<int>(clips.size());
                float gap = 9.f;
                float side = 30.f;
                float cw = std::floor((m_w - side * 2 - gap * 2) / 3.f);
                float ch = 138.f;
                float left = (m_w - (cw * 3 + gap * 2)) / 2;
                float cardY = kFooterH + 20.f;

                for (int slot = 0; slot < 3; ++slot) {
                    int idx = m_page * 3 + slot;
                    if (idx >= n) break;
                    auto const& c = clips[static_cast<size_t>(idx)];

                    auto card = CCNode::create();
                    card->setContentSize({cw, ch});
                    card->setAnchorPoint({0.f, 0.f});
                    float x = left + slot * (cw + gap);
                    float drift = m_slide == 0 ? 0.f : (m_slide > 0 ? 40.f : -40.f);
                    card->setPosition({x + drift, cardY - (m_slide == 0 ? 14.f : 0.f)});
                    m_body->addChild(card, 5);

                    auto edge = box(cw + 2.f, ch + 2.f, kEdge);
                    edge->setPosition({cw / 2, ch / 2});
                    card->addChild(edge, 0);
                    auto bg = box(cw, ch, kCard);
                    bg->setPosition({cw / 2, ch / 2});
                    card->addChild(bg, 1);

                    float tw = cw - 12.f;
                    float th = std::floor(tw * 0.5f);
                    auto thumb = thumbNode(c, tw, th);
                    thumb->setPosition({6.f, ch - 6.f - th});
                    card->addChild(thumb, 3);

                    if (c.percent >= 0) {
                        auto pb = badge(std::to_string(c.percent) + "%", reasonColor(c.reason), kBlack, 0.46f);
                        pb->setPosition({10.f, ch - 6.f - th + 5.f});
                        card->addChild(pb, 6);
                    }
                    auto rb = badge(reasonText(c.reason), kInk, reasonColor(c.reason), 0.4f);
                    rb->setPosition({6.f + tw - rb->getContentSize().width - 4.f, ch - 6.f - 18.f});
                    card->addChild(rb, 6);
                    auto db = badge(fmtDuration(c.duration), kInk, kText, 0.4f);
                    db->setPosition({6.f + tw - db->getContentSize().width - 4.f, ch - 6.f - th + 5.f});
                    card->addChild(db, 6);

                    auto title = label(clip(c.level, 26), "chatFont.fnt", 0.56f, kText, {0.f, 0.5f});
                    fit(title, cw - 16.f, 0.56f);
                    title->setPosition({8.f, ch - 6.f - th - 13.f});
                    card->addChild(title, 4);
                    auto sub = label(fmtAge(c.created) + "  -  " + fmtSize(c.bytes), "chatFont.fnt", 0.42f, kDim, {0.f, 0.5f});
                    fit(sub, cw - 16.f, 0.42f);
                    sub->setPosition({8.f, ch - 6.f - th - 25.f});
                    card->addChild(sub, 4);

                    auto cardMenu = CCMenu::create();
                    cardMenu->setPosition({0.f, 0.f});
                    card->addChild(cardMenu, 8);

                    float bw1 = std::floor((cw - 16.f - 5.f) * 0.58f);
                    float bw2 = cw - 16.f - 5.f - bw1;
                    auto share = CCMenuItemSpriteExtra::create(pillFace(tr("btn.share"), bw1, 22.f, kYellow, kBlack, false, false, 0.52f), nullptr, this, menu_selector(StudioLayer::onShare));
                    share->setTag(idx);
                    share->setPosition({8.f + bw1 / 2, 6.f + 11.f});
                    cardMenu->addChild(share);

                    bool confirm = (m_pendingDelete == idx);
                    auto del = CCMenuItemSpriteExtra::create(
                        pillFace(confirm ? tr("btn.confirm") : tr("btn.delete"), bw2, 22.f, confirm ? kRec : kCardDeep, confirm ? kBlack : kRec, false, !confirm, 0.5f),
                        nullptr, this, menu_selector(StudioLayer::onDelete));
                    del->setTag(idx);
                    del->setPosition({8.f + bw1 + 5.f + bw2 / 2, 6.f + 11.f});
                    cardMenu->addChild(del);

                    card->runAction(CCSequence::create(
                        CCDelayTime::create(0.04f * static_cast<float>(slot)),
                        CCEaseExponentialOut::create(CCMoveTo::create(0.38f, ccp(x, cardY))),
                        nullptr));
                }

                if (pages > 1) {
                    float cy = cardY + ch / 2;
                    auto prev = CCMenuItemSpriteExtra::create(circleFace(24.f, kCardDeep, iconChevron(24.f, kText, true)), nullptr, this, menu_selector(StudioLayer::onPrev));
                    prev->setPosition({15.f, cy});
                    navMenu->addChild(prev);
                    auto next = CCMenuItemSpriteExtra::create(circleFace(24.f, kCardDeep, iconChevron(24.f, kText, false)), nullptr, this, menu_selector(StudioLayer::onNext));
                    next->setPosition({m_w - 15.f, cy});
                    navMenu->addChild(next);

                    auto dots = CCDrawNode::create();
                    float total = static_cast<float>(pages - 1) * 11.f;
                    for (int i = 0; i < pages; ++i) {
                        bool active = i == m_page;
                        disc(dots, ccp(m_w / 2 - total / 2 + static_cast<float>(i) * 11.f, kFooterH + 9.f), active ? 3.4f : 2.5f, col4(active ? kYellow : kEdge), 14);
                    }
                    m_body->addChild(dots, 6);
                }
            }

            void buildFooter(CCMenu* menu) {
                auto line = box(m_w - 24.f, 1.5f, kEdge, 170);
                line->setPosition({m_w / 2, kFooterH});
                m_body->addChild(line, 3);

                auto st = Engine::get().status();
                bool running = st.state == native::CaptureState::Running;
                bool starting = st.state == native::CaptureState::Starting;

                CCNode* face;
                if (running) face = pillFace(tr("btn.clip", {std::to_string(cfg::i("manual-seconds"))}), 190.f, 28.f, kYellow, kBlack, true, false, 0.6f);
                else if (starting) face = pillFace(tr("state.starting"), 190.f, 28.f, kCard, kDim, false, true, 0.6f);
                else face = pillFace(tr("btn.start"), 190.f, 28.f, kYellow, kBlack, false, false, 0.6f);
                auto primary = CCMenuItemSpriteExtra::create(face, nullptr, this, menu_selector(StudioLayer::onPrimary));
                primary->setPosition({16.f + 95.f, kFooterH / 2 - 1.f});
                menu->addChild(primary);

                float right = m_w - 16.f;
                if (running || starting) {
                    auto stop = CCMenuItemSpriteExtra::create(pillFace(tr("btn.stop"), 64.f, 24.f, kCardDeep, kText, false, true, 0.54f), nullptr, this, menu_selector(StudioLayer::onStop));
                    stop->setPosition({right - 32.f, kFooterH / 2 - 1.f});
                    menu->addChild(stop);
                    right -= 74.f;
                }
                m_info = label("", "chatFont.fnt", 0.42f, kDim, {1.f, 0.5f});
                m_info->setPosition({right, kFooterH / 2 - 1.f});
                m_body->addChild(m_info, 4);
                tick(0.f);

                if (st.state == native::CaptureState::Denied) {
                    auto hint = label(tr("banner.denied"), "chatFont.fnt", 0.46f, kRec, {0.5f, 0.5f});
                    fit(hint, m_w - 40.f, 0.46f);
                    hint->setPosition({m_w / 2, kFooterH + 8.f});
                    if (Engine::get().clips().empty()) m_body->addChild(hint, 6);
                }
            }

            void onPrev(CCObject*) {
                m_pendingDelete = -1;
                m_slide = -1;
                int pages = std::max(1, (static_cast<int>(Engine::get().clips().size()) + 2) / 3);
                m_page = (m_page + pages - 1) % pages;
                buildClips();
            }

            void onNext(CCObject*) {
                m_pendingDelete = -1;
                m_slide = 1;
                int pages = std::max(1, (static_cast<int>(Engine::get().clips().size()) + 2) / 3);
                m_page = (m_page + 1) % pages;
                buildClips();
            }

            void onShare(CCObject* sender) {
                auto const& clips = Engine::get().clips();
                int idx = static_cast<CCNode*>(sender)->getTag();
                if (idx >= 0 && idx < static_cast<int>(clips.size())) Engine::get().shareClip(clips[static_cast<size_t>(idx)]);
            }

            void onDelete(CCObject* sender) {
                auto const& clips = Engine::get().clips();
                int idx = static_cast<CCNode*>(sender)->getTag();
                if (idx < 0 || idx >= static_cast<int>(clips.size())) return;
                if (m_pendingDelete != idx) {
                    m_pendingDelete = idx;
                    buildClips();
                    return;
                }
                std::string id = clips[static_cast<size_t>(idx)].id;
                m_pendingDelete = -1;
                Engine::get().deleteClip(id);
                buildClips();
            }

            void onPrimary(CCObject*) {
                auto st = Engine::get().status();
                if (st.state == native::CaptureState::Running) {
                    Engine::get().clipManual();
                } else if (st.state != native::CaptureState::Starting) {
                    Engine::get().startBuffer();
                }
            }

            void onStop(CCObject*) {
                Engine::get().stopBuffer();
            }

            // ------------------------------------------------------------ settings page

            void buildSettings() {
                m_body->removeAllChildrenWithCleanup(true);
                m_info = nullptr;
                m_rowUi.clear();
                m_pendingDelete = -1;

                auto menu = CCMenu::create();
                menu->setPosition({0.f, 0.f});
                m_body->addChild(menu, 30);

                // tabs
                char const* tabKeys[4] = {"tab.buffer", "tab.triggers", "tab.timing", "tab.clippy"};
                for (int i = 0; i < 4; ++i) {
                    bool active = (i == m_tab);
                    auto face = pillFace(tr(tabKeys[i]), 90.f, 26.f, active ? kYellow : kCard, active ? kBlack : kText, false, !active, 0.56f);
                    auto item = CCMenuItemSpriteExtra::create(face, nullptr, this, menu_selector(StudioLayer::onTab));
                    item->setTag(i);
                    item->setPosition({16.f + 45.f, m_h - kHeaderH - 22.f - static_cast<float>(i) * 34.f});
                    menu->addChild(item);
                }
                auto divider = box(1.5f, m_h - kHeaderH - 24.f, kEdge, 170);
                divider->setPosition({116.f, (m_h - kHeaderH) / 2 + 2.f});
                m_body->addChild(divider, 3);

                auto const& rows = settingsTabs()[static_cast<size_t>(m_tab)];
                float xL = 132.f;
                float xR = m_w - 18.f;
                float y = m_h - kHeaderH - 24.f;
                m_rowUi.resize(rows.size());

                for (size_t i = 0; i < rows.size(); ++i) {
                    auto const& r = rows[i];
                    auto name = label(tr(r.label), "chatFont.fnt", 0.6f, kText, {0.f, 0.5f});
                    fit(name, (xR - xL) * 0.5f, 0.6f);
                    name->setPosition({xL, y});
                    m_body->addChild(name, 4);

                    if (i + 1 < rows.size()) {
                        auto sep = box(xR - xL, 1.f, kEdge, 90);
                        sep->setPosition({(xL + xR) / 2, y - 16.f});
                        m_body->addChild(sep, 2);
                    }

                    switch (r.kind) {
                        case Row::Kind::Switch: buildSwitch(menu, r, i, xR - 22.f, y); break;
                        case Row::Kind::Stepper: buildStepper(menu, r, i, xR, y); break;
                        case Row::Kind::Cycle: buildCycle(menu, r, i, xR - 66.f, y); break;
                        case Row::Kind::Action: {
                            auto item = CCMenuItemSpriteExtra::create(pillFace(tr("btn.open"), 90.f, 24.f, kCard, kText, false, true, 0.54f), nullptr, this, menu_selector(StudioLayer::onDiagnostics));
                            item->setPosition({xR - 45.f, y});
                            menu->addChild(item);
                            break;
                        }
                    }
                    y -= 32.f;
                }
                geode::cocos::handleTouchPriority(this);
            }

            void buildSwitch(CCMenu* menu, Row const& r, size_t i, float cx, float y) {
                auto face = CCNode::create();
                face->setContentSize({44.f, 24.f});
                face->setAnchorPoint({0.5f, 0.5f});
                bool on = cfg::b(r.key);
                auto track = box(44.f, 22.f, on ? kMint : 0x3A4160);
                track->setPosition({22.f, 12.f});
                face->addChild(track, 0);
                auto knob = CCDrawNode::create();
                disc(knob, ccp(0, 0), 8.6f, col4(0xFFFFFF), 20);
                knob->setPosition({on ? 33.f : 11.f, 12.f});
                face->addChild(knob, 1);
                m_rowUi[i].knob = knob;
                m_rowUi[i].track = track;

                auto item = CCMenuItemSpriteExtra::create(face, nullptr, this, menu_selector(StudioLayer::onSwitch));
                item->setTag(static_cast<int>(i));
                item->setPosition({cx, y});
                menu->addChild(item);
            }

            void buildStepper(CCMenu* menu, Row const& r, size_t i, float xR, float y) {
                float plusX = xR - 12.f;
                float minusX = xR - 12.f - 24.f - 96.f;
                auto minus = CCMenuItemSpriteExtra::create(circleFace(24.f, kCard, iconPlusMinus(24.f, kText, false)), nullptr, this, menu_selector(StudioLayer::onStep));
                minus->setTag(static_cast<int>(i) * 2);
                minus->setPosition({minusX, y});
                menu->addChild(minus);
                auto plus = CCMenuItemSpriteExtra::create(circleFace(24.f, kCard, iconPlusMinus(24.f, kText, true)), nullptr, this, menu_selector(StudioLayer::onStep));
                plus->setTag(static_cast<int>(i) * 2 + 1);
                plus->setPosition({plusX, y});
                menu->addChild(plus);

                auto value = label(rowValueText(r), "chatFont.fnt", 0.56f, kYellow);
                fit(value, 92.f, 0.56f);
                value->setPosition({(minusX + plusX) / 2, y});
                m_body->addChild(value, 4);
                m_rowUi[i].value = value;
            }

            void buildCycle(CCMenu* menu, Row const& r, size_t i, float cx, float y) {
                auto face = pillFace(rowValueText(r), 132.f, 24.f, kCard, kYellow, false, true, 0.56f);
                auto item = CCMenuItemSpriteExtra::create(face, nullptr, this, menu_selector(StudioLayer::onCycle));
                item->setTag(static_cast<int>(i));
                item->setPosition({cx, y});
                menu->addChild(item);
            }

            Row const& rowAt(int idx) { return settingsTabs()[static_cast<size_t>(m_tab)][static_cast<size_t>(idx)]; }

            void onTab(CCObject* sender) {
                m_tab = static_cast<CCNode*>(sender)->getTag();
                buildSettings();
            }

            void onSwitch(CCObject* sender) {
                int idx = static_cast<CCNode*>(sender)->getTag();
                auto const& r = rowAt(idx);
                bool on = !cfg::b(r.key);
                cfg::setB(r.key, on);
                Engine::get().applyConfig();
                auto& ui = m_rowUi[static_cast<size_t>(idx)];
                if (ui.knob) {
                    ui.knob->stopAllActions();
                    ui.knob->runAction(CCEaseOut::create(CCMoveTo::create(0.14f, ccp(on ? 33.f : 11.f, 12.f)), 2.f));
                }
                if (ui.track) ui.track->setColor(col(on ? kMint : 0x3A4160));
            }

            void onStep(CCObject* sender) {
                int tag = static_cast<CCNode*>(sender)->getTag();
                int idx = tag / 2;
                bool plus = (tag % 2) == 1;
                auto const& r = rowAt(idx);
                int v = cfg::i(r.key) + (plus ? r.step : -r.step);
                v = std::clamp(v, r.minV, r.maxV);
                cfg::setI(r.key, v);
                Engine::get().applyConfig();
                auto& ui = m_rowUi[static_cast<size_t>(idx)];
                if (ui.value) {
                    ui.value->setString(rowValueText(r).c_str());
                    fit(ui.value, 92.f, 0.56f);
                }
            }

            void onCycle(CCObject* sender) {
                int idx = static_cast<CCNode*>(sender)->getTag();
                auto const& r = rowAt(idx);
                std::string cur = cfg::s(r.key);
                size_t at = 0;
                for (size_t k = 0; k < r.options.size(); ++k) {
                    if (r.options[k] == cur) at = k;
                }
                cfg::setS(r.key, r.options[(at + 1) % r.options.size()]);
                Engine::get().applyConfig();
                buildSettings(); // labels (and language) change, so rebuild the page
            }

            void onDiagnostics(CCObject*) {
                auto st = Engine::get().status();
                char const* names[5] = {"off", "starting", "running", "denied", "failed"};
                char buf[512];
                std::snprintf(buf, sizeof buf,
                    "State: %s\nFrames: %lld   Audio chunks: %lld\nFrame size: %dx%d   Orientation key: %d\nCodec: %s\nBuffered: %.1f s   %.1f MB\nLast error: %s",
                    names[static_cast<int>(st.state)], st.frames, st.audioChunks, st.width, st.height, st.orientationKey,
                    st.codec.empty() ? "-" : st.codec.c_str(), st.bufferedSeconds, st.megabytes,
                    st.lastError.empty() ? "-" : st.lastError.c_str());
                FLAlertLayer::create("Clippy diagnostics", std::string(buf), "OK")->show();
            }
        };

        StudioLayer* StudioLayer::s_open = nullptr;

    }

    // ====================================================================== public API

    void openStudio() {
        StudioLayer::open();
    }

    void installOverlay() {
        if (s_toast) return;
        s_toast = Toast::create();
        if (!s_toast) return;
        OverlayManager::get()->addChild(s_toast, 100);

        Engine::get().setListener([](EngineEvent const& e) {
            switch (e.kind) {
                case EngineEvent::Kind::State:
                    StudioLayer::refreshIfOpen();
                    break;
                case EngineEvent::Kind::ClipStarted:
                    if (e.clip.reason == "manual" && wantToast()) {
                        s_toast->present(tr("toast.saving"), clip(e.clip.level, 28), Mascot::Mood::Neutral, {}, 1.2f);
                    }
                    break;
                case EngineEvent::Kind::ClipSaved: {
                    if (cfg::b("haptics")) native::haptic(1);
                    if (wantToast()) {
                        static int quip = 0;
                        std::string title = tr("toast.saved");
                        if (e.clip.percent >= 0) title += "  -  " + std::to_string(e.clip.percent) + "%";
                        s_toast->present(title, tr(kQuips[quip++ % 6]), Mascot::Mood::Happy, e.clip.thumb, 4.2f);
                    }
                    StudioLayer::refreshIfOpen();
                    break;
                }
                case EngineEvent::Kind::ClipFailed:
                    if (cfg::b("haptics")) native::haptic(2);
                    if (wantToast()) {
                        s_toast->present(tr("toast.failed"), clip(e.text, 44), Mascot::Mood::Worried, {}, 4.5f);
                    }
                    break;
            }
        });
    }

}
