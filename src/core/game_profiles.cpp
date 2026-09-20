#include "game_profiles.h"
#include "json_persistence.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <mutex>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <shlobj.h>
#endif

namespace gno {

// Единый процесс-глобальный замок для всех экземпляров GameProfiles. Разные
// потоки (GUI и воркер MonitoringService) создают свои экземпляры, но работают
// с одним файлом profiles.json — без сериализации чтение могло попасть на
// частично записанный файл, а два сохранения перемешивали бы содержимое.
namespace {
std::mutex& profilesFileMutex() {
    static std::mutex mutex;
    return mutex;
}
} // namespace

std::string GameProfiles::getAppDataPath() {
#ifdef PLATFORM_WINDOWS
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::string(path);
    }
#endif
    return ".";
}

GameProfiles::GameProfiles() {
    load();
}

GameProfiles::~GameProfiles() {
    // Сохраняем только то, что реально изменили. Экземпляры «только чтение»
    // (воркер MonitoringService, discoverRunningGameProcess) раньше писали файл
    // обратно своим снимком и могли затереть чужие сохранённые изменения.
    if (dirty_) save();
}

std::string GameProfiles::getSavePath() const {
    return getAppDataPath() + "\\GNO\\profiles.json";
}

void GameProfiles::load() {
    std::lock_guard<std::mutex> lock(profilesFileMutex());

    profiles_.clear();

    std::ifstream file(getSavePath());
    if (!file.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (content.empty()) return;

    // Simple JSON parsing
    std::string trimmed = content;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n["));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n]") + 1);
    
    size_t pos = 0;
    while (pos < trimmed.size()) {
        // Find next object
        size_t objStart = trimmed.find('{', pos);
        if (objStart == std::string::npos) break;
        size_t objEnd = trimmed.find('}', objStart);
        if (objEnd == std::string::npos) break;
        
        std::string obj = trimmed.substr(objStart, objEnd - objStart + 1);
        GameProfile p = parseProfile(obj);
        if (!p.game_name.empty()) {
            profiles_.push_back(p);
        }
        pos = objEnd + 1;
    }
}

GameProfile GameProfiles::parseProfile(const std::string& json) {
    GameProfile p;
    auto extractString = [&](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";
        pos = json.find('"', pos);
        if (pos == std::string::npos) return "";
        size_t end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    };
    
    auto extractBool = [&](const std::string& key) -> bool {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return false;
        pos = json.find(':', pos);
        if (pos == std::string::npos) return false;
        std::string val = json.substr(pos + 1, 5);
        return val.find("true") != std::string::npos;
    };
    
    auto extractInt = [&](const std::string& key) -> int {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return 0;
        pos = json.find(':', pos);
        if (pos == std::string::npos) return 0;
        size_t end = json.find_first_of(",}", pos);
        if (end == std::string::npos) return 0;
        std::string val = json.substr(pos + 1, end - pos - 1);
        try { return std::stoi(val); } catch (...) { return 0; }
    };
    
    p.game_name = extractString("game_name");
    p.process_name = extractString("process_name");
    p.multipath_enabled = extractBool("multipath_enabled");
    p.fps_boost_enabled = extractBool("fps_boost_enabled");
    p.network_optimization = extractBool("network_optimization");
    p.max_routes = extractInt("max_routes");
    p.auto_apply = extractBool("auto_apply");

    // granular flags (v1.4); default to the coarse flags for old files
    p.game_dvr_opt = extractBool("game_dvr_opt");
    p.power_plan_opt = extractBool("power_plan_opt");
    p.high_priority_opt = extractBool("high_priority_opt");
    p.tcp_opt = extractBool("tcp_opt");
    p.mtu_opt = extractBool("mtu_opt");
    p.custom_dns = extractBool("custom_dns");
    p.dns_server = extractString("dns_server");
    p.pro_config_opt = extractBool("pro_config_opt");
    p.overlay_enabled = extractBool("overlay_enabled");

    // migration: old profiles only had coarse flags
    bool hasGranular = json.find("\"game_dvr_opt\"") != std::string::npos;
    if (!hasGranular) {
        p.game_dvr_opt = p.fps_boost_enabled;
        p.power_plan_opt = p.fps_boost_enabled;
        p.high_priority_opt = p.fps_boost_enabled;
        p.tcp_opt = p.network_optimization;
        p.mtu_opt = p.network_optimization;
    }
    return p;
}

// Сериализация profiles_ в текст. Не трогает файл и не берёт замок —
// вызывающий код сам определяет, нужна ли ему блокировка (save() берёт,
// а importFromFile уже держит замок на всё время read→parse→write).
std::string GameProfiles::serializeProfiles(const std::vector<GameProfile>& profiles) {
    std::string content;
    content.reserve(profiles.size() * 512 + 16);
    content += "[\n";
    for (size_t i = 0; i < profiles.size(); ++i) {
        const auto& p = profiles[i];
        content += "  {\n";
        content += "    \"game_name\": \"" + GameProfiles::escapeJson(p.game_name) + "\",\n";
        content += "    \"process_name\": \"" + GameProfiles::escapeJson(p.process_name) + "\",\n";
        content += "    \"multipath_enabled\": " + std::string(p.multipath_enabled ? "true" : "false") + ",\n";
        content += "    \"fps_boost_enabled\": " + std::string(p.fps_boost_enabled ? "true" : "false") + ",\n";
        content += "    \"network_optimization\": " + std::string(p.network_optimization ? "true" : "false") + ",\n";
        content += "    \"max_routes\": " + std::to_string(p.max_routes) + ",\n";
        content += "    \"auto_apply\": " + std::string(p.auto_apply ? "true" : "false") + ",\n";
        content += "    \"game_dvr_opt\": " + std::string(p.game_dvr_opt ? "true" : "false") + ",\n";
        content += "    \"power_plan_opt\": " + std::string(p.power_plan_opt ? "true" : "false") + ",\n";
        content += "    \"high_priority_opt\": " + std::string(p.high_priority_opt ? "true" : "false") + ",\n";
        content += "    \"tcp_opt\": " + std::string(p.tcp_opt ? "true" : "false") + ",\n";
        content += "    \"mtu_opt\": " + std::string(p.mtu_opt ? "true" : "false") + ",\n";
        content += "    \"custom_dns\": " + std::string(p.custom_dns ? "true" : "false") + ",\n";
        content += "    \"dns_server\": \"" + GameProfiles::escapeJson(p.dns_server) + "\",\n";
        content += "    \"pro_config_opt\": " + std::string(p.pro_config_opt ? "true" : "false") + ",\n";
        content += "    \"overlay_enabled\": " + std::string(p.overlay_enabled ? "true" : "false") + "\n";
        content += "  }";
        if (i < profiles.size() - 1) content += ",";
        content += "\n";
    }
    content += "]\n";
    return content;
}

void GameProfiles::save() {
    // Атомарная запись через временный файл + rename: другой поток (или
    // процесс) не увидит наполовину записанный profiles.json. Замок
    // сериализует экземпляры внутри процесса.
    const std::string content = serializeProfiles(profiles_);
    std::lock_guard<std::mutex> lock(profilesFileMutex());

    std::string path = getSavePath();
    std::string dir = path.substr(0, path.find_last_of("\\/"));
#ifdef PLATFORM_WINDOWS
    CreateDirectoryA(dir.c_str(), nullptr);
#else
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
#endif
    persistence::atomicWriteText(std::filesystem::path(path), content);
}

std::string GameProfiles::escapeJson(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

bool GameProfiles::exportToFile(const std::string& path) const {
    std::string dir = path.substr(0, path.find_last_of("\\/"));
#ifdef PLATFORM_WINDOWS
    CreateDirectoryA(dir.c_str(), nullptr);
#endif

    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "{\n";
    file << "  \"version\": 1,\n";
    file << "  \"profiles\": [\n";
    for (size_t i = 0; i < profiles_.size(); ++i) {
        const auto& p = profiles_[i];
        file << "    {\n";
        file << "      \"game_name\": \"" << GameProfiles::escapeJson(p.game_name) << "\",\n";
        file << "      \"process_name\": \"" << GameProfiles::escapeJson(p.process_name) << "\",\n";
        file << "      \"multipath_enabled\": " << (p.multipath_enabled ? "true" : "false") << ",\n";
        file << "      \"fps_boost_enabled\": " << (p.fps_boost_enabled ? "true" : "false") << ",\n";
        file << "      \"network_optimization\": " << (p.network_optimization ? "true" : "false") << ",\n";
        file << "      \"max_routes\": " << p.max_routes << ",\n";
        file << "      \"auto_apply\": " << (p.auto_apply ? "true" : "false") << ",\n";
        file << "      \"game_dvr_opt\": " << (p.game_dvr_opt ? "true" : "false") << ",\n";
        file << "      \"power_plan_opt\": " << (p.power_plan_opt ? "true" : "false") << ",\n";
        file << "      \"high_priority_opt\": " << (p.high_priority_opt ? "true" : "false") << ",\n";
        file << "      \"tcp_opt\": " << (p.tcp_opt ? "true" : "false") << ",\n";
        file << "      \"mtu_opt\": " << (p.mtu_opt ? "true" : "false") << ",\n";
        file << "      \"custom_dns\": " << (p.custom_dns ? "true" : "false") << ",\n";
        file << "      \"dns_server\": \"" << GameProfiles::escapeJson(p.dns_server) << "\",\n";
        file << "      \"pro_config_opt\": " << (p.pro_config_opt ? "true" : "false") << ",\n";
        file << "      \"overlay_enabled\": " << (p.overlay_enabled ? "true" : "false") << "\n";
        file << "    }";
        if (i < profiles_.size() - 1) file << ",";
        file << "\n";
    }
    file << "  ]\n";
    file << "}\n";
    return true;
}

bool GameProfiles::importFromFile(const std::string& path) {
    // Чтение, разбор и запись держим под одним замком целиком: иначе между
    // чтением файла и сохранением другой поток успеет записать свои изменения,
    // и этот вызов их перетрёт своим снимком.
    std::lock_guard<std::mutex> lock(profilesFileMutex());

    std::ifstream file(path);
    if (!file.is_open()) return false;
    
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (content.empty()) return false;
    
    // Новый формат — объект с массивом "profiles", старый — просто массив.
    // Вырезаем именно массив [ ... ], а не «все скобки по краям»: прежняя
    // обрезка съедала закрывающую скобку ПОСЛЕДНЕГО объекта, и импорт любого
    // корректного файла (включая вывод exportToFile) не находил ни одного
    // профиля — экспорт/импорт был полностью нерабочим.
    std::string body = content;
    if (body.find("\"profiles\"") != std::string::npos) {
        const size_t arrStart = body.find('[');
        const size_t arrEnd = body.rfind(']');
        if (arrStart == std::string::npos || arrEnd == std::string::npos || arrEnd <= arrStart)
            return false;
        body = body.substr(arrStart, arrEnd - arrStart + 1);
    }

    // Парсим во временный список, а не прямо в profiles_: пустой результат
    // (битый файл, пустой массив profiles) не должен затирать уже сохранённые
    // профили. Раньше profiles_ очищался до разбора и сохранялся пустым —
    // импорт мусорного файла молча удалял все настройки игр.
    std::vector<GameProfile> imported;

    size_t pos = 0;
    while (pos < body.size()) {
        size_t objStart = body.find('{', pos);
        if (objStart == std::string::npos) break;
        size_t objEnd = body.find('}', objStart);
        if (objEnd == std::string::npos) break;

        std::string obj = body.substr(objStart, objEnd - objStart + 1);
        GameProfile p = parseProfile(obj);
        if (!p.game_name.empty()) {
            imported.push_back(p);
        }
        pos = objEnd + 1;
    }

    if (imported.empty()) return false;

    profiles_ = std::move(imported);
    dirty_ = true;

    // Замок уже удерживается этим методом — пишем напрямую, а не через save().
    std::string save_path = getSavePath();
    std::string dir = save_path.substr(0, save_path.find_last_of("\\/"));
#ifdef PLATFORM_WINDOWS
    CreateDirectoryA(dir.c_str(), nullptr);
#else
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
#endif
    persistence::atomicWriteText(std::filesystem::path(save_path), serializeProfiles(profiles_));
    return true;
}

std::vector<GameProfile> GameProfiles::getAll() const { return profiles_; }

GameProfile GameProfiles::get(const std::string& game_name) const {
    for (const auto& p : profiles_) {
        if (p.game_name == game_name) return p;
    }
    return GameProfile{};
}

bool GameProfiles::has(const std::string& game_name) const {
    for (const auto& p : profiles_) {
        if (p.game_name == game_name) return true;
    }
    return false;
}

void GameProfiles::set(const GameProfile& profile) {
    for (auto& p : profiles_) {
        if (p.game_name == profile.game_name) {
            p = profile;
            dirty_ = true;
            save();
            return;
        }
    }
    profiles_.push_back(profile);
    dirty_ = true;
    save();
}

void GameProfiles::remove(const std::string& game_name) {
    profiles_.erase(
        std::remove_if(profiles_.begin(), profiles_.end(),
                       [&game_name](const GameProfile& p) { return p.game_name == game_name; }),
        profiles_.end());
    dirty_ = true;
    save();
}

GameProfile GameProfiles::detectForProcess(const std::string& process_name) const {
    for (const auto& p : profiles_) {
        if (p.process_name == process_name) return p;
    }
    return GameProfile{};
}

} // namespace gno
