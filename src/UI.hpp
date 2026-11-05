#pragma once

#include <Geode/Geode.hpp>

namespace clippy::ui {

    // Creates Clippy's persistent overlay (toasts + haptics) and connects it to the engine. Idempotent.
    void installOverlay();

    // Opens the Studio (clips gallery + settings) on top of the current scene.
    void openStudio();

    // Small round button faces used in the main menu and the pause menu.
    cocos2d::CCNode* clippyButtonFace(float diameter);
    cocos2d::CCNode* clipNowButtonFace(float diameter);

}
