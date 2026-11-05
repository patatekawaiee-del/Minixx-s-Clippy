#pragma once

// UI kit for Minixx's Clippy. Everything is built from assets that exist in every Geometry Dash
// install ("square02_001.png", the BMFonts) plus CCDrawNode polygons - no custom textures.

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace clippy::ui::kit {

    // ------------------------------------------------------------------ palette
    constexpr int kInk = 0x0E1018;
    constexpr int kSurface = 0x161927;
    constexpr int kCard = 0x20243A;
    constexpr int kCardDeep = 0x171A2B;
    constexpr int kEdge = 0x343B5C;
    constexpr int kSilver = 0xCBD3E6;
    constexpr int kPaper = 0xFFFFE1; // the Office Assistant balloon yellow
    constexpr int kRec = 0xFF4358;
    constexpr int kMint = 0x4DE8B0;
    constexpr int kAmber = 0xFFC24D;
    constexpr int kYellow = 0xFFD84D;
    constexpr int kText = 0xEEF1FA;
    constexpr int kDim = 0x8E97B8;
    constexpr int kBlack = 0x14151C;

    inline ccColor3B col(int hex) {
        return ccc3(static_cast<GLubyte>((hex >> 16) & 0xFF), static_cast<GLubyte>((hex >> 8) & 0xFF), static_cast<GLubyte>(hex & 0xFF));
    }

    inline ccColor4F col4(int hex, float a = 1.f) {
        return ccc4f(((hex >> 16) & 0xFF) / 255.f, ((hex >> 8) & 0xFF) / 255.f, (hex & 0xFF) / 255.f, a);
    }

    // ------------------------------------------------------------------ basic nodes
    inline CCScale9Sprite* box(float w, float h, int hex, int opacity = 255) {
        auto s = CCScale9Sprite::create("square02_001.png", {0, 0, 80, 80});
        s->setContentSize({w, h});
        s->setColor(col(hex));
        s->setOpacity(static_cast<GLubyte>(opacity));
        return s;
    }

    inline CCLabelBMFont* label(std::string const& text, char const* font, float scale, int hex, CCPoint anchor = {0.5f, 0.5f}) {
        auto l = CCLabelBMFont::create(text.c_str(), font);
        l->setScale(scale);
        l->setColor(col(hex));
        l->setAnchorPoint(anchor);
        return l;
    }

    inline void fit(CCLabelBMFont* l, float maxWidth, float baseScale) {
        float w = l->getContentSize().width;
        float s = (w > 0.f && w * baseScale > maxWidth) ? maxWidth / w : baseScale;
        l->setScale(s);
    }

    inline float labelWidth(CCLabelBMFont* l) {
        return l->getContentSize().width * l->getScale();
    }

    // ------------------------------------------------------------------ vector drawing (convex polygons only)
    inline void poly(CCDrawNode* n, std::vector<CCPoint>& pts, ccColor4F fill) {
        n->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), fill, 0.f, ccc4f(0.f, 0.f, 0.f, 0.f));
    }

    inline void disc(CCDrawNode* n, CCPoint c, float r, ccColor4F color, int segments = 24) {
        std::vector<CCPoint> p;
        p.reserve(static_cast<size_t>(segments));
        for (int i = 0; i < segments; ++i) {
            float a = static_cast<float>(i) / static_cast<float>(segments) * 6.2831853f;
            p.emplace_back(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
        }
        poly(n, p, color);
    }

    inline void bar(CCDrawNode* n, CCPoint a, CCPoint b, float w, ccColor4F color) {
        float dx = b.x - a.x, dy = b.y - a.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.0001f) return;
        float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
        std::vector<CCPoint> p;
        p.emplace_back(a.x + nx, a.y + ny);
        p.emplace_back(b.x + nx, b.y + ny);
        p.emplace_back(b.x - nx, b.y - ny);
        p.emplace_back(a.x - nx, a.y - ny);
        poly(n, p, color);
    }

    // a line with round caps
    inline void seg(CCDrawNode* n, CCPoint a, CCPoint b, float w, ccColor4F color) {
        bar(n, a, b, w, color);
        disc(n, a, w * 0.5f, color, 10);
        disc(n, b, w * 0.5f, color, 10);
    }

    // ------------------------------------------------------------------ icons (drawn around the origin)
    inline CCDrawNode* iconClose(float s, int hex) {
        auto n = CCDrawNode::create();
        float a = s * 0.2f;
        seg(n, ccp(-a, -a), ccp(a, a), s * 0.1f, col4(hex));
        seg(n, ccp(-a, a), ccp(a, -a), s * 0.1f, col4(hex));
        return n;
    }

    inline CCDrawNode* iconChevron(float s, int hex, bool left) {
        auto n = CCDrawNode::create();
        float d = left ? 1.f : -1.f; // left: tip on the left ("<"), right: tip on the right (">")
        seg(n, ccp(0.1f * s * d, -0.2f * s), ccp(-0.1f * s * d, 0.f), s * 0.1f, col4(hex));
        seg(n, ccp(-0.1f * s * d, 0.f), ccp(0.1f * s * d, 0.2f * s), s * 0.1f, col4(hex));
        return n;
    }

    inline CCDrawNode* iconPlusMinus(float s, int hex, bool plus) {
        auto n = CCDrawNode::create();
        float a = s * 0.2f;
        seg(n, ccp(-a, 0.f), ccp(a, 0.f), s * 0.1f, col4(hex));
        if (plus) seg(n, ccp(0.f, -a), ccp(0.f, a), s * 0.1f, col4(hex));
        return n;
    }

    inline CCDrawNode* iconTune(float s, int hex) {
        auto n = CCDrawNode::create();
        float ys[3] = {-0.2f * s, 0.f, 0.2f * s};
        float ks[3] = {-0.08f * s, 0.12f * s, -0.14f * s};
        for (int i = 0; i < 3; ++i) {
            bar(n, ccp(-0.28f * s, ys[i]), ccp(0.28f * s, ys[i]), s * 0.07f, col4(hex));
            disc(n, ccp(ks[i], ys[i]), s * 0.075f, col4(hex), 14);
        }
        return n;
    }

    // ------------------------------------------------------------------ paperclip
    inline std::vector<CCPoint> clipPolyline() {
        std::vector<CCPoint> p;
        constexpr float PI = 3.14159265f;
        auto add = [&](float x, float y) { p.emplace_back(x, 160.f - y); };
        auto line = [&](float x0, float y0, float x1, float y1) {
            int n = 6;
            for (int i = 0; i <= n; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(n);
                add(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
            }
        };
        auto arc = [&](float cx, float cy, float r, float a0, float a1) {
            int n = 14;
            for (int i = 0; i <= n; ++i) {
                float a = a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(n);
                add(cx + r * std::cos(a), cy + r * std::sin(a));
            }
        };
        line(18, 118, 18, 40);
        arc(50, 40, 32, PI, 2 * PI);
        line(82, 40, 82, 122);
        arc(60, 122, 22, 0, PI);
        line(38, 122, 38, 62);
        arc(53, 62, 15, PI, 2 * PI);
        line(68, 62, 68, 100);
        return p;
    }

    inline void wire(CCDrawNode* n, std::vector<CCPoint> const& pts, float w, ccColor4F color, float ox, float oy, float k) {
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            bar(n, ccp(pts[i].x * k + ox, pts[i].y * k + oy), ccp(pts[i + 1].x * k + ox, pts[i + 1].y * k + oy), w * k, color);
        }
        for (auto const& q : pts) disc(n, ccp(q.x * k + ox, q.y * k + oy), w * k * 0.5f, color, 8);
    }

    // Draws the clip centred on (cx, cy), `height` points tall.
    inline void drawPaperclip(CCDrawNode* n, float height, float cx, float cy, bool shiny = true) {
        float k = height / 160.f;
        float ox = cx - 50.f * k, oy = cy - 80.f * k;
        auto pts = clipPolyline();
        wire(n, pts, 11.f, col4(0x555E7E), ox, oy, k);
        wire(n, pts, 7.4f, col4(kSilver), ox, oy, k);
        if (shiny) wire(n, pts, 2.2f, col4(0xF6F9FF), ox - 1.4f * k, oy + 1.4f * k, k);
    }

    // ------------------------------------------------------------------ buttons
    inline CCNode* circleFace(float d, int fill, CCNode* icon, bool ring = true) {
        auto n = CCNode::create();
        n->setContentSize({d, d});
        n->setAnchorPoint({0.5f, 0.5f});
        auto bg = CCDrawNode::create();
        if (ring) disc(bg, ccp(d / 2, d / 2), d / 2, col4(kEdge), 28);
        disc(bg, ccp(d / 2, d / 2), d / 2 - (ring ? 1.3f : 0.f), col4(fill), 28);
        n->addChild(bg);
        if (icon) {
            icon->setPosition({d / 2, d / 2});
            n->addChild(icon, 2);
        }
        return n;
    }

    inline CCNode* pillFace(std::string const& text, float w, float h, int fill, int textHex, bool dot = false, bool outline = true, float textScale = 0.56f) {
        auto n = CCNode::create();
        n->setContentSize({w, h});
        n->setAnchorPoint({0.5f, 0.5f});
        if (outline) {
            auto e = box(w + 2.f, h + 2.f, kEdge);
            e->setPosition({w / 2, h / 2});
            n->addChild(e, 0);
        }
        auto b = box(w, h, fill);
        b->setPosition({w / 2, h / 2});
        n->addChild(b, 1);

        auto l = label(text, "chatFont.fnt", textScale, textHex);
        fit(l, w - (dot ? 34.f : 16.f), textScale);
        float cx = w / 2 + (dot ? 7.f : 0.f);
        l->setPosition({cx, h / 2 + 0.5f});
        n->addChild(l, 3);
        if (dot) {
            auto d = CCDrawNode::create();
            float dx = cx - labelWidth(l) / 2 - 9.f;
            disc(d, ccp(dx, h / 2), 4.2f, col4(kRec), 16);
            n->addChild(d, 3);
        }
        return n;
    }

    // ------------------------------------------------------------------ speech bubble (Office Assistant style)
    inline CCNode* bubble(float w, float h) {
        auto n = CCNode::create();
        n->setContentSize({w, h});
        n->setAnchorPoint({0.5f, 0.5f});

        auto tail = CCDrawNode::create();
        auto diamond = [&](float d, int hex) {
            std::vector<CCPoint> p;
            p.emplace_back(-d, h / 2);
            p.emplace_back(0.f, h / 2 + d);
            p.emplace_back(d, h / 2);
            p.emplace_back(0.f, h / 2 - d);
            poly(tail, p, col4(hex));
        };
        diamond(8.f, kBlack);
        n->addChild(tail, 0);

        auto border = box(w + 3.f, h + 3.f, kBlack);
        border->setPosition({w / 2, h / 2});
        n->addChild(border, 1);
        auto fill = box(w, h, kPaper);
        fill->setPosition({w / 2, h / 2});
        n->addChild(fill, 2);

        auto tailFill = CCDrawNode::create();
        std::vector<CCPoint> p;
        float d = 6.4f;
        p.emplace_back(-d, h / 2);
        p.emplace_back(0.f, h / 2 + d);
        p.emplace_back(d, h / 2);
        p.emplace_back(0.f, h / 2 - d);
        poly(tailFill, p, col4(kPaper));
        n->addChild(tailFill, 3);
        return n;
    }

    // ------------------------------------------------------------------ Clippy
    class Mascot : public CCNode {
    public:
        enum class Mood { Neutral, Happy, Worried, Annoyed };

        static Mascot* create(float height) {
            auto r = new Mascot();
            if (r->setup(height)) {
                r->autorelease();
                return r;
            }
            delete r;
            return nullptr;
        }

        void setMood(Mood mood) {
            float left = 0.f, right = 0.f, lift = 0.f;
            switch (mood) {
                case Mood::Happy: lift = 3.f; break;
                case Mood::Worried: left = -16.f; right = 16.f; lift = 1.f; break;
                case Mood::Annoyed: left = 16.f; right = -16.f; break;
                default: break;
            }
            float angles[2] = {left, right};
            float xs[2] = {38.f, 62.f};
            for (int i = 0; i < 2; ++i) {
                m_brow[i]->stopAllActions();
                m_brow[i]->runAction(CCSpawn::create(
                    CCEaseBackOut::create(CCRotateTo::create(0.22f, angles[i])),
                    CCMoveTo::create(0.22f, ccp(xs[i], 146.f + lift)),
                    nullptr));
            }
        }

        void lookTo(float dx, float dy) {
            for (int i = 0; i < 2; ++i) {
                m_pupil[i]->stopAllActions();
                m_pupil[i]->runAction(CCEaseOut::create(CCMoveTo::create(0.18f, ccp(dx * 3.2f, dy * 3.2f - 0.5f)), 2.f));
            }
        }

        void blink() {
            for (int i = 0; i < 2; ++i) {
                m_eye[i]->stopAllActions();
                m_eye[i]->runAction(CCSequence::create(
                    CCScaleTo::create(0.06f, 1.f, 0.08f),
                    CCScaleTo::create(0.09f, 1.f, 1.f),
                    nullptr));
            }
        }

    private:
        bool setup(float height) {
            if (!CCNode::init()) return false;
            constexpr float W = 100.f, H = 160.f;
            float s = height / H;
            this->setContentSize({W * s, height});
            this->setAnchorPoint({0.5f, 0.5f});

            m_bob = CCNode::create();
            m_bob->setScale(s);
            this->addChild(m_bob);

            auto body = CCDrawNode::create();
            drawPaperclip(body, H, W / 2, H / 2, true);
            m_bob->addChild(body, 1);

            float ex[2] = {38.f, 62.f};
            for (int i = 0; i < 2; ++i) {
                auto eye = CCNode::create();
                eye->setPosition({ex[i], 130.f});
                m_bob->addChild(eye, 2);

                auto sclera = CCDrawNode::create();
                disc(sclera, ccp(0, 0), 11.6f, col4(0x20222E), 22);
                disc(sclera, ccp(0, 0), 10.2f, col4(0xFFFFFF), 22);
                eye->addChild(sclera);

                auto pupil = CCDrawNode::create();
                disc(pupil, ccp(0, 0), 4.6f, col4(0x15161C), 16);
                disc(pupil, ccp(1.6f, 1.8f), 1.5f, col4(0xFFFFFF), 10);
                pupil->setPosition({0.f, -0.5f});
                eye->addChild(pupil, 1);

                m_eye[i] = eye;
                m_pupil[i] = pupil;

                auto brow = CCNode::create();
                brow->setPosition({ex[i], 146.f});
                auto bd = CCDrawNode::create();
                seg(bd, ccp(-8.f, 0.f), ccp(8.f, 0.f), 3.2f, col4(0x15161C));
                brow->addChild(bd);
                m_bob->addChild(brow, 3);
                m_brow[i] = brow;
            }

            m_bob->runAction(CCRepeatForever::create(CCSequence::create(
                CCEaseInOut::create(CCMoveBy::create(1.1f, ccp(0.f, 3.f)), 2.f),
                CCEaseInOut::create(CCMoveBy::create(1.1f, ccp(0.f, -3.f)), 2.f),
                nullptr)));
            this->schedule(schedule_selector(Mascot::blinkTick), 2.8f);
            return true;
        }

        void blinkTick(float) { blink(); }

        CCNode* m_bob = nullptr;
        CCNode* m_eye[2] = {nullptr, nullptr};
        CCDrawNode* m_pupil[2] = {nullptr, nullptr};
        CCNode* m_brow[2] = {nullptr, nullptr};
    };

}
