// SPDX-License-Identifier: Apache-2.0 OR MIT
#include <fstream>
#include <sstream>

#include "keymap.h"

#include <nlohmann/json.hpp>

namespace companion {

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream s;
    s << in.rdbuf();
    out = s.str();
    return true;
}

bool loadKeyMap(const std::string& mapJson, const std::string& tableJson, KeyMap& out, std::string* error) {
    nlohmann::json map = nlohmann::json::parse(mapJson, nullptr, false);
    nlohmann::json table = nlohmann::json::parse(tableJson, nullptr, false);
    if (map.is_discarded() || table.is_discarded() || !map.contains("keys") || !map["keys"].is_array()) {
        if (error) *error = "the key map or key table is not valid JSON with a keys array";
        return false;
    }
    out = KeyMap{map.value("name", std::string()), {}};
    for (const nlohmann::json& k : map["keys"]) {
        Key key;
        key.code = k.value("code", std::string());
        if (!table.contains(key.code)) {
            if (error) *error = "the key table has no code " + key.code;
            return false;
        }
        const nlohmann::json& t = table[key.code];
        key.label = k.value("label", key.code);
        key.row = k.value("row", 0);
        key.col = k.value("col", 0);
        key.span = k.value("span", 1);
        key.vk = t.value("vk", 0);
        key.scan = t.value("scan", 0);
        key.extended = t.value("extended", false);
        out.keys.push_back(key);
    }
    return true;
}

std::vector<Button> layoutButtons(const KeyMap& map, float size, float gap) {
    std::vector<Button> out;
    for (const Key& k : map.keys)
        out.push_back(Button{&k, k.col * (size + gap), k.row * (size + gap), k.span * size + (k.span - 1) * gap, size});
    return out;
}

}
