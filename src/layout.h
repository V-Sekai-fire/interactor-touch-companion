// SPDX-License-Identifier: Apache-2.0 OR MIT
#pragma once
#include <string>
#include <vector>

#include "keymap.h"

namespace companion {

struct Box {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool inside(const Box& o) const { return x >= o.x - 0.5f && y >= o.y - 0.5f && x + w <= o.x + o.w + 0.5f && y + h <= o.y + o.h + 0.5f; }
    bool overlaps(const Box& o) const { return x < o.x + o.w - 0.5f && o.x < x + w - 0.5f && y < o.y + o.h - 0.5f && o.y < y + h - 0.5f; }
};

// The companion window split into the game's region (top) and the key strip (bottom), in window pixels.
struct CompanionLayout {
    Box window;
    Box game;
    Box keys;
    Box picture;
    std::vector<Button> buttons;
    float buttonScale = 1.0f;
};

struct LayoutOptions {
    float size = 96.0f;
    float gap = 16.0f;
    // A fraction of the largest picture that fits the game region, so the picture never spills out of it.
    float viewScale = 1.0f;
    float margin = 12.0f;
    // The strip never takes more than this share of the window; the buttons shrink to fit instead.
    float maxKeysShare = 0.4f;
};

// The buttons shrink, never grow, to fit the strip, and the picture keeps the game's aspect inside its region.
CompanionLayout fitCompanion(const KeyMap& map, float windowW, float windowH, float pictureW, float pictureH,
                             const LayoutOptions& opt);

// What a layout gets wrong: anything outside its region, overlaps, or buttons too small to hit.
std::vector<std::string> layoutViolations(const CompanionLayout& layout, float minButton = 32.0f);

}
