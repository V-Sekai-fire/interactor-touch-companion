// SPDX-License-Identifier: Apache-2.0 OR MIT
#include "layout.h"

#include <algorithm>

namespace companion {

CompanionLayout fitCompanion(const KeyMap& map, float windowW, float windowH, float pictureW, float pictureH,
                             const LayoutOptions& opt) {
    CompanionLayout out;
    out.window = {0.0f, 0.0f, windowW, windowH};
    std::vector<Button> natural = layoutButtons(map, opt.size, opt.gap);
    float w = 0.0f, h = 0.0f;
    for (const Button& b : natural) {
        w = std::max(w, b.x + b.w);
        h = std::max(h, b.y + b.h);
    }
    const float m = opt.margin;
    const float roomW = std::max(1.0f, windowW - 2 * m);
    const float roomH = std::max(1.0f, windowH * opt.maxKeysShare - 2 * m);
    out.buttonScale = (w > 0.0f && h > 0.0f) ? std::min({1.0f, roomW / w, roomH / h}) : 1.0f;
    const float stripH = h * out.buttonScale + 2 * m;
    out.keys = {0.0f, windowH - stripH, windowW, stripH};
    out.game = {0.0f, 0.0f, windowW, windowH - stripH};
    const float x0 = (windowW - w * out.buttonScale) / 2, y0 = out.keys.y + m;
    for (const Button& b : natural)
        out.buttons.push_back(Button{b.key, x0 + b.x * out.buttonScale, y0 + b.y * out.buttonScale, b.w * out.buttonScale, b.h * out.buttonScale});
    if (pictureW > 0.0f && pictureH > 0.0f && out.game.w > 0.0f && out.game.h > 0.0f) {
        const float fit = std::min(out.game.w / pictureW, out.game.h / pictureH) * std::clamp(opt.viewScale, 0.05f, 1.0f);
        out.picture = {(out.game.w - pictureW * fit) / 2, (out.game.h - pictureH * fit) / 2, pictureW * fit, pictureH * fit};
    }
    return out;
}

std::vector<std::string> layoutViolations(const CompanionLayout& l, float minButton) {
    std::vector<std::string> bad;
    if (!l.picture.inside(l.game)) bad.push_back("the picture spills out of the game region");
    if (l.picture.overlaps(l.keys)) bad.push_back("the picture overlaps the key strip");
    if (!l.keys.inside(l.window) || !l.game.inside(l.window)) bad.push_back("a region lies outside the window");
    for (size_t i = 0; i < l.buttons.size(); ++i) {
        const Button& b = l.buttons[i];
        const Box box{b.x, b.y, b.w, b.h};
        const std::string name = b.key ? b.key->code : std::to_string(i);
        if (!box.inside(l.keys)) bad.push_back("button " + name + " lies outside the key strip");
        if (std::min(b.w, b.h) < minButton) bad.push_back("button " + name + " is under " + std::to_string(int(minButton)) + " px");
        for (size_t j = i + 1; j < l.buttons.size(); ++j) {
            const Button& o = l.buttons[j];
            if (box.overlaps(Box{o.x, o.y, o.w, o.h})) bad.push_back("button " + name + " overlaps another");
        }
    }
    return bad;
}

}
