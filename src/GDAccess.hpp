#pragma once

// Every read of a Geometry Dash field goes through this file.
// If a future GD / Geode update renames one of them, this is the only place to fix.

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cstdlib>
#include <string>

namespace clippy::gd {

    inline std::string levelName(GJGameLevel* level) {
        if (!level) return "Level";
        std::string name = level->m_levelName.c_str();
        return name.empty() ? std::string("Level") : name;
    }

    // Percent as shown on screen. PlayLayer::getCurrentPercent has no iOS address in the 2.2081
    // bindings, so we read the on-screen percentage label ("53%" or "53.21%") instead.
    inline int percentOf(PlayLayer* layer) {
        int percent = 0;
        if (layer && layer->m_percentageLabel) {
            if (char const* text = layer->m_percentageLabel->getString()) {
                percent = static_cast<int>(std::atof(text));
            }
        }
        return std::clamp(percent, 0, 100);
    }

}
