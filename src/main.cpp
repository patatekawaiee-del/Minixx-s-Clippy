// Minixx's Clippy - hooks.
//
// Only functions that have an iOS address in the 2.2081 bindings are hooked here:
//   MenuLayer::init, PauseLayer::customSetup,
//   PlayLayer::onEnterTransitionDidFinish / onExit / resetLevel / destroyPlayer / postUpdate / pauseGame.
// Level completion is detected by polling PlayLayer::m_hasCompletedLevel in postUpdate
// (PlayLayer::levelComplete has no iOS address).

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Engine.hpp"
#include "GDAccess.hpp"
#include "UI.hpp"

using namespace geode::prelude;
using clippy::Engine;

$on_mod(Loaded) {
    Engine::get().init();
}

class $modify(ClippyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        clippy::ui::installOverlay();
        Engine::get().menuShown();

        auto button = CCMenuItemSpriteExtra::create(
            clippy::ui::clippyButtonFace(34.f), nullptr, this, menu_selector(ClippyMenuLayer::onClippy));

        if (auto menu = typeinfo_cast<CCMenu*>(this->getChildByID("bottom-menu"))) {
            menu->addChild(button);
            menu->updateLayout();
        } else {
            // Node IDs not available: fall back to our own little menu in the bottom-left corner.
            auto fallback = CCMenu::create();
            fallback->setPosition({30.f, 30.f});
            fallback->addChild(button);
            this->addChild(fallback, 10);
        }
        return true;
    }

    void onClippy(CCObject*) {
        clippy::ui::openStudio();
    }
};

class $modify(ClippyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});

        auto studio = CCMenuItemSpriteExtra::create(
            clippy::ui::clippyButtonFace(34.f), nullptr, this, menu_selector(ClippyPauseLayer::onStudio));
        studio->setPosition({28.f, 28.f});
        menu->addChild(studio);

        auto clipNow = CCMenuItemSpriteExtra::create(
            clippy::ui::clipNowButtonFace(34.f), nullptr, this, menu_selector(ClippyPauseLayer::onClipNow));
        clipNow->setPosition({28.f, 68.f});
        menu->addChild(clipNow);

        this->addChild(menu, 20);
    }

    void onStudio(CCObject*) {
        clippy::ui::openStudio();
    }

    void onClipNow(CCObject*) {
        auto& engine = Engine::get();
        if (engine.bufferRunning()) engine.clipManual();
        else engine.startBuffer();
    }
};

class $modify(ClippyPlayLayer, PlayLayer) {
    void onEnterTransitionDidFinish() {
        PlayLayer::onEnterTransitionDidFinish();
        Engine::get().levelEntered(clippy::gd::levelName(m_level));
    }

    void onExit() {
        Engine::get().levelLeft();
        PlayLayer::onExit();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        Engine::get().runStarted();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool alreadyDead = m_playerDied;
        PlayLayer::destroyPlayer(player, object);
        // same guards the game uses; also skips "deaths" that another mod (noclip...) cancelled
        if (alreadyDead || object == m_anticheatSpike || !m_playerDied) return;
        Engine::get().death(clippy::gd::percentOf(this), m_isPracticeMode);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto& engine = Engine::get();
        if (!m_isPaused) engine.resumed();
        if (m_hasCompletedLevel && !engine.completeHandled()) engine.levelComplete(m_isPracticeMode);
    }

    void pauseGame(bool unfocused) {
        Engine::get().paused();
        PlayLayer::pauseGame(unfocused);
    }
};
