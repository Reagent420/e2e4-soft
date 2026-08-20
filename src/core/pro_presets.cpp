#include "pro_presets.h"
#include "game_detector.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <filesystem>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <shlobj.h>
#endif

namespace gno {

namespace fs = std::filesystem;

std::string ProPresets::trim(const std::string& s) {
    std::string out = s;
    out.erase(0, out.find_first_not_of(" \t\r\n"));
    out.erase(out.find_last_not_of(" \t\r\n") + 1);
    return out;
}

std::string ProPresets::toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string ProPresets::resolveCS2Path() {
    GameDetector detector;
    detector.scanInstalledGames();
    auto installed = detector.getInstalledGames();
    for (const auto& game : installed) {
        if (toLower(game.process_name) == "cs2.exe" && !game.executable_path.empty()) {
            fs::path exe(game.executable_path);
            // ...\Counter-Strike Global Offensive\game\bin\win64\cs2.exe
            // or ...\game\csgo\bin\win64\cs2.exe
            fs::path gameDir = exe.parent_path();
            for (int i = 0; i < 3; ++i) {
                if (gameDir.filename() == "win64") {
                    gameDir = gameDir.parent_path();
                    break;
                }
                if (gameDir.has_parent_path())
                    gameDir = gameDir.parent_path();
            }
            // try csgo\cfg\autoexec.cfg
            for (const auto& candidate : { gameDir / "csgo" / "cfg" / "autoexec.cfg",
                                           gameDir / "game" / "csgo" / "cfg" / "autoexec.cfg" }) {
                if (fs::exists(candidate))
                    return candidate.string();
            }
        }
    }
    return {};
}

std::string ProPresets::resolveValorantPath() {
#ifdef PLATFORM_WINDOWS
    char buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf))) {
        fs::path p(buf);
        p /= "VALORANT/Saved/Config/Windows/GameUserSettings.ini";
        if (fs::exists(p)) return p.string();
    }
#endif
    return {};
}

std::string ProPresets::resolvePUBGPath() {
#ifdef PLATFORM_WINDOWS
    char buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf))) {
        fs::path p(buf);
        p /= "TslGame/Saved/Config/WindowsNoEditor/GameUserSettings.ini";
        if (fs::exists(p)) return p.string();
    }
#endif
    return {};
}

std::vector<ProPreset> ProPresets::allPresets() {
    std::vector<ProPreset> presets;

    {
        ProPreset cs2;
        cs2.game_name = "Counter-Strike 2";
        cs2.display_name = "Counter-Strike 2";
        cs2.config_path = resolveCS2Path();
        cs2.config_kind = "cfg";
        cs2.description =
            "Настройки киберспортсменов для CS2: прямой ввод мыши, "
            "высокий лимит FPS, максимальная скорость приёма, минимальный интерполяционный буфер. "
            "Записывается в autoexec.cfg (резервная копия создаётся автоматически).";
        cs2.lines = {
            {"m_rawinput", "1", "прямой ввод с мыши"},
            {"fps_max", "0", "лимит FPS отключён"},
            {"rate", "786432", "максимальная скорость обмена с сервером"},
            {"cl_interp_ratio", "1", "минимальная интерполяция — точнее попадания"},
            {"cl_updaterate", "128", "частота обновления сервера"},
            {"cl_cmdrate", "128", "частота отправки команд"},
        };
        presets.push_back(std::move(cs2));
    }

    {
        ProPreset val;
        val.game_name = "Valorant";
        val.display_name = "Valorant";
        val.config_path = resolveValorantPath();
        val.config_kind = "ini";
        val.description =
            "Настройки для Valorant: полноэкранный режим, отключение HDR-вывода — "
            "ниже задержка отображения. Патчится GameUserSettings.ini с резервной копией.";
        val.lines = {
            {"FullscreenMode", "1", "полноэкранный режим"},
            {"bUseHDRDisplayOutput", "0", "HDR выключен — ниже latency"},
        };
        presets.push_back(std::move(val));
    }

    {
        ProPreset pubg;
        pubg.game_name = "PLAYERUNKNOWN'S BATTLEGROUNDS";
        pubg.display_name = "PUBG: Battlegrounds";
        pubg.config_path = resolvePUBGPath();
        pubg.config_kind = "ini";
        pubg.description =
            "Настройки для PUBG: полноэкранный режим и отключение motion blur — "
            "чётче картинка при движении. Патчится GameUserSettings.ini с резервной копией.";
        pubg.lines = {
            {"FullscreenMode", "1", "полноэкранный режим"},
            {"MotionBlur", "0", "смазывание выключено"},
        };
        presets.push_back(std::move(pubg));
    }

    return presets;
}

bool ProPresets::applyPreset(const ProPreset& preset, std::string& outMessage) {
    if (preset.config_path.empty()) {
        outMessage = "конфигурация игры не найдена — установите и запустите игру хотя бы один раз";
        return false;
    }

    fs::path path(preset.config_path);
    if (!fs::exists(path)) {
        outMessage = "файл не найден: " + preset.config_path;
        return false;
    }

    // backup
    fs::path backup = path;
    backup += ".bak";
    try {
        fs::copy_file(path, backup, fs::copy_options::overwrite_existing);
    } catch (...) {
        outMessage = "не удалось создать резервную копию";
        return false;
    }

    std::ifstream in(preset.config_path, std::ios::binary);
    std::ostringstream content;
    content << in.rdbuf();
    in.close();
    std::string data = content.str();

    const std::string marker = "// E2E4 Soft";
    int applied = 0;

    if (preset.config_kind == "cfg") {
        if (data.find(marker) != std::string::npos) {
            outMessage = "профиль уже применён (autoexec.cfg)";
            return true;
        }
        data += "\n// E2E4 Soft Pro Preset — applied " + std::to_string(time(nullptr)) + "\n";
        for (const auto& line : preset.lines) {
            data += line.key + " " + line.value + "\n";
            ++applied;
        }
    } else {
        // INI patch: replace existing keys, append missing ones
        std::istringstream stream(data);
        std::string out;
        std::string line;
        bool anyAppend = false;
        while (std::getline(stream, line)) {
            bool matched = false;
            std::string trimmed = trim(line);
            for (const auto& item : preset.lines) {
                if (toLower(trimmed).compare(0, item.key.size() + 1, toLower(item.key) + "=") == 0) {
                    out += item.key + "=" + item.value + "\n";
                    matched = true;
                    ++applied;
                    break;
                }
            }
            if (!matched)
                out += line + "\n";
        }
        for (const auto& item : preset.lines) {
            // check if key already existed (would have been counted above)
            std::string needle = toLower(item.key) + "=";
            std::string lower = toLower(data);
            if (lower.find(needle) == std::string::npos) {
                out += item.key + "=" + item.value + "\n";
                ++applied;
            }
        }
        data = out;
    }

    std::ofstream outFile(preset.config_path, std::ios::binary | std::ios::trunc);
    if (!outFile.is_open()) {
        outMessage = "нет прав на запись: " + preset.config_path;
        return false;
    }
    outFile << data;
    outFile.close();

    outMessage = "применено настроек: " + std::to_string(applied) + " (файл: " + preset.config_path + ")";
    return true;
}

} // namespace gno