#include "updater.h"

#include "sha256.h"
#include "simple_json.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace gno {
namespace updater {

const char* const kOwner = "Reagent420";
const char* const kRepo = "e2e4-soft";

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// Удаление без исключений: вызывается на пути отказа, где бросать нельзя.
void removeQuietly(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// Разобранная версия. Части, которых нет, остаются нулями.
struct Version {
    long parts[3] = {0, 0, 0};
    bool has_prerelease = false;
};

Version parseVersion(const std::string& raw) {
    Version v;
    // Отбрасываем ведущий мусор: "v3.1.1", "release-3.1.1" → "3.1.1".
    std::size_t i = 0;
    while (i < raw.size() && !std::isdigit(static_cast<unsigned char>(raw[i])))
        ++i;

    int index = 0;
    while (i < raw.size() && index < 3) {
        if (!std::isdigit(static_cast<unsigned char>(raw[i]))) break;
        long value = 0;
        while (i < raw.size() && std::isdigit(static_cast<unsigned char>(raw[i]))) {
            // Ограничиваем, чтобы длинная числовая строка не переполнила long.
            if (value < 1000000000L)
                value = value * 10 + (raw[i] - '0');
            ++i;
        }
        v.parts[index++] = value;
        if (i < raw.size() && raw[i] == '.') {
            ++i;
            continue;
        }
        break;
    }

    // Всё, что идёт после чисел: "-beta1", "+build". Дефис означает
    // предрелиз, плюс — только метаданные сборки (предрелизом не считается).
    if (i < raw.size() && raw[i] == '-')
        v.has_prerelease = true;
    return v;
}

} // namespace

bool isNewerVersion(const std::string& candidate, const std::string& current) {
    const Version a = parseVersion(candidate);
    const Version b = parseVersion(current);

    for (int i = 0; i < 3; ++i) {
        if (a.parts[i] != b.parts[i])
            return a.parts[i] > b.parts[i];
    }
    // Числовые части равны: 3.2.0-beta1 младше 3.2.0, но новее 3.1.9
    // (последнее уже отсечено выше).
    if (a.has_prerelease != b.has_prerelease)
        return b.has_prerelease;
    return false;
}

bool normalizeDigest(const std::string& raw, std::string& out_hex) {
    std::string value = raw;
    const std::string prefix = "sha256:";
    if (value.size() > prefix.size() &&
        toLower(value.substr(0, prefix.size())) == prefix) {
        value = value.substr(prefix.size());
    }
    // Иногда дайджест приходит в виде "sha256-<base64>" (формат SRI) —
    // такой формат не поддерживаем, честно сообщаем об этом.
    if (value.find('-') != std::string::npos) return false;
    if (value.size() != 64) return false;
    for (char c : value) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    out_hex = toLower(value);
    return true;
}

bool digestEquals(const std::string& a, const std::string& b) {
    return !a.empty() && !b.empty() && toLower(a) == toLower(b);
}

CheckResult parseLatestRelease(const std::string& json_body,
                               const std::string& current_version) {
    CheckResult result;
    result.current_version = current_version;

    json::Value root;
    std::string parse_error;
    if (!json::parse(json_body, root, parse_error)) {
        result.error = "не удалось разобрать ответ сервера: " + parse_error;
        return result;
    }
    if (!root.isObject()) {
        result.error = "ответ сервера не является объектом";
        return result;
    }

    result.release.tag = root["tag_name"].asString();
    if (result.release.tag.empty()) {
        result.error = "в ответе нет tag_name";
        return result;
    }
    result.release.name = root["name"].asString();
    result.release.notes = root["body"].asString();
    result.release.html_url = root["html_url"].asString();

    const json::Value& assets = root["assets"];
    if (assets.isArray()) {
        for (std::size_t i = 0; i < assets.size(); ++i) {
            const json::Value& item = assets.at(i);
            if (!item.isObject()) continue;
            Asset asset;
            asset.name = item["name"].asString();
            asset.download_url = item["browser_download_url"].asString();
            if (asset.name.empty() || asset.download_url.empty()) continue;

            // GitHub с 2024 отдаёт digest вида "sha256:<hex>". Если его нет —
            // пробуем sidecar-файл "<asset>.sha256", добавленный в релиз.
            std::string hex;
            if (normalizeDigest(item["digest"].asString(), hex))
                asset.sha256 = hex;
            result.release.assets.push_back(std::move(asset));
        }
    }

    // Sidecar-файл "<name>.sha256" обрабатывается отдельно (нужна ещё одна
    // загрузка), см. findSidecarAsset/parseSidecarDigest.
    result.latest_version = result.release.tag;
    result.update_available =
        isNewerVersion(result.release.tag, current_version);
    result.ok = true;
    return result;
}

const Asset* findSidecarAsset(const Release& release, const Asset& asset) {
    const std::string wanted = asset.name + ".sha256";
    for (const Asset& candidate : release.assets) {
        if (candidate.name == wanted) return &candidate;
    }
    return nullptr;
}

bool parseSidecarDigest(const std::string& body, std::string& out_hex) {
    // Формат как у sha256sum: "<hex>" или "<hex>  <имя файла>".
    std::size_t end = body.find_first_of(" \t\r\n");
    const std::string token = body.substr(0, end == std::string::npos ? body.size() : end);
    return normalizeDigest(token, out_hex);
}

const Asset* pickWindowsAsset(const Release& release) {
    // Приоритет: установщик .exe → любой .exe → архив .zip. Порядок важен:
    // реальные релизы этого проекта выкладывают .zip, и без запасного
    // варианта обновление было бы недоступно вообще.
    const Asset* plain_exe = nullptr;
    const Asset* windows_zip = nullptr;
    const Asset* plain_zip = nullptr;

    for (const Asset& asset : release.assets) {
        const std::string lower = toLower(asset.name);
        const bool is_exe = lower.size() > 4 && lower.substr(lower.size() - 4) == ".exe";
        const bool is_zip = lower.size() > 4 && lower.substr(lower.size() - 4) == ".zip";
        if (!is_exe && !is_zip) continue;

        const bool windows_marked =
            lower.find("windows") != std::string::npos ||
            lower.find("win64") != std::string::npos ||
            lower.find("win-x64") != std::string::npos ||
            lower.find("setup") != std::string::npos ||
            lower.find("installer") != std::string::npos;

        if (is_exe) {
            if (windows_marked) return &asset;
            if (plain_exe == nullptr) plain_exe = &asset;
        } else {
            if (windows_marked && windows_zip == nullptr) windows_zip = &asset;
            if (plain_zip == nullptr) plain_zip = &asset;
        }
    }

    if (plain_exe != nullptr) return plain_exe;
    if (windows_zip != nullptr) return windows_zip;
    return plain_zip;
}

std::string Updater::releasesApiUrl() {
    return std::string("https://api.github.com/repos/") + kOwner + "/" + kRepo +
           "/releases/latest";
}

std::string repositoryUrl() {
    return std::string("https://github.com/") + kOwner + "/" + kRepo;
}CheckResult Updater::check(const std::string& current_version) const {
    CheckResult result;
    result.current_version = current_version;

    if (!http_get_) {
        result.error = "не задан способ загрузки (http_get)";
        return result;
    }

    std::string body;
    std::string http_error;
    if (!http_get_(releasesApiUrl(), body, http_error)) {
        result.error = http_error.empty() ? "не удалось получить список релизов"
                                          : http_error;
        return result;
    }
    return parseLatestRelease(body, current_version);
}

bool Updater::downloadAndVerify(const Asset& asset, const std::string& dest_path,
                                std::string& out_hex, std::string& error) const {
    if (asset.sha256.empty()) {
        error = "в релизе нет SHA-256 для " + asset.name +
                " — установка непроверенного файла запрещена";
        return false;
    }
    if (!http_download_) {
        error = "не задан способ скачивания (http_download)";
        return false;
    }
    if (!file_hasher_) {
        error = "не задан способ вычисления хеша (file_hasher)";
        return false;
    }
    if (!http_download_(asset.download_url, dest_path, error))
        return false;

    std::string actual;
    std::string hash_error;
    if (!file_hasher_(dest_path, actual, hash_error)) {
        error = hash_error.empty() ? "не удалось вычислить SHA-256" : hash_error;
        removeQuietly(dest_path);
        return false;
    }

    if (!digestEquals(actual, asset.sha256)) {
        error = "SHA-256 не совпал: ожидался " + toLower(asset.sha256) +
                ", получен " + actual;
        removeQuietly(dest_path);
        return false;
    }

    out_hex = actual;
    error.clear();
    return true;
}

} // namespace updater
} // namespace gno
