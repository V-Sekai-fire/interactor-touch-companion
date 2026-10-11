// SPDX-License-Identifier: Apache-2.0 OR MIT
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace companion {

struct Key {
    std::string code;
    std::string label;
    int row = 0;
    int col = 0;
    int span = 1;
    std::uint16_t vk = 0;
    std::uint16_t scan = 0;
    bool extended = false;
};

struct KeyMap {
    std::string name;
    std::vector<Key> keys;
};

struct Button {
    const Key* key = nullptr;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// A game's map names keys by DOM KeyboardEvent.code; the table gives each code's virtual key and scancode.
bool loadKeyMap(const std::string& mapJson, const std::string& tableJson, KeyMap& out, std::string* error);
bool readFile(const std::string& path, std::string& out);
// The same layout the browser keypad uses: column and row steps of size + gap, spans joining their gaps.
std::vector<Button> layoutButtons(const KeyMap& map, float size, float gap);

}
