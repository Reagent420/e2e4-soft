#pragma once

#include <string>
#include <vector>

namespace gno {

struct ProPresetLine {
    std::string key;
    std::string value;
    std::string comment;
};

struct ProPreset {
    std::string game_name;      // matches GameDetector names
    std::string display_name;
    std::string config_path;    // resolved at apply time
    std::string config_kind;    // "cfg" (append lines) or "ini" (patch keys)
    std::string description;
    std::vector<ProPresetLine> lines;
};

// Ready-made pro player settings applied to the game config files.
// Config files are backed up before modification.
class ProPresets {
public:
    static std::vector<ProPreset> allPresets();

    static bool applyPreset(const ProPreset& preset, std::string& outMessage);

private:
    static std::string resolveCS2Path();
    static std::string resolveValorantPath();
    static std::string resolvePUBGPath();
    static std::string trim(const std::string& s);
    static std::string toLower(const std::string& s);
};

} // namespace gno