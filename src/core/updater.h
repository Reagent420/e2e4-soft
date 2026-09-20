#pragma once

#include <functional>
#include <string>
#include <vector>

// Проверка обновлений по GitHub Releases. Модуль Qt-free: сеть и файловая
// система подключаются через инъектируемые швы, поэтому логика (сравнение
// версий, разбор ответа, проверка SHA-256) полностью тестируется без сети.
//
// Политика безопасности: обновление принимается ТОЛЬКО при совпадении
// SHA-256 с ожидаемым. Если сервер не отдал дайджест — установка
// отклоняется, а не выполняется «на доверии».
namespace gno {
namespace updater {

// Репозиторий продукта (см. также src/ui/theme.h).
extern const char* const kOwner;
extern const char* const kRepo;

// Ссылка на репозиторий для интерфейса: "https://github.com/owner/repo".
std::string repositoryUrl();

struct Asset {
    std::string name;
    std::string download_url;
    std::string sha256; // нормализованный hex, пусто если сервер не отдал
};

struct Release {
    std::string tag;      // напр. "v3.1.1"
    std::string name;
    std::string notes;
    std::string html_url;
    std::vector<Asset> assets;
};

struct CheckResult {
    bool ok = false;              // запрос и разбор прошли
    bool update_available = false;
    std::string current_version;
    std::string latest_version;
    std::string error;
    Release release;
};

// Сравнение версий: "3.1.0" < "3.1.1"; ведущая 'v' игнорируется; версия с
// суффиксом предрелиза ("3.2.0-beta1") младше той же версии без суффикса.
// Нечисловые части трактуются как 0, поэтому мусорный тег не «обновит» ничего.
bool isNewerVersion(const std::string& candidate, const std::string& current);

// Разбор тела ответа GET /repos/{owner}/{repo}/releases/latest.
CheckResult parseLatestRelease(const std::string& json_body,
                               const std::string& current_version);

// Подходящий ассет для Windows: приоритет — установщик .exe, затем любой .exe,
// затем архив .zip (релизы этого проекта выкладывают именно .zip).
// nullptr, если в релизе нет ни exe, ни zip.
const Asset* pickWindowsAsset(const Release& release);

// Ассет "<asset.name>.sha256" (sidecar с дайджестом), либо nullptr.
const Asset* findSidecarAsset(const Release& release, const Asset& asset);

// Разбор тела sidecar-файла: "<hex>" или "<hex>  <имя файла>".
bool parseSidecarDigest(const std::string& body, std::string& out_hex);

// Приводит "sha256:ABC..." или "ABC..." к hex в нижнем регистре.
// false, если строка не похожа на SHA-256-дайджест.
bool normalizeDigest(const std::string& raw, std::string& out_hex);
// Сравнение двух hex-дайджестов без учёта регистра.
bool digestEquals(const std::string& a, const std::string& b);

using HttpGet = std::function<bool(const std::string& url, std::string& body,
                                   std::string& error)>;
using HttpDownload = std::function<bool(const std::string& url,
                                        const std::string& dest_path,
                                        std::string& error)>;
using FileHasher = std::function<bool(const std::string& path, std::string& hex,
                                      std::string& error)>;

class Updater {
public:
    void setHttpGet(HttpGet fn) { http_get_ = std::move(fn); }
    void setHttpDownload(HttpDownload fn) { http_download_ = std::move(fn); }
    void setFileHasher(FileHasher fn) { file_hasher_ = std::move(fn); }

    bool hasHttpGet() const { return static_cast<bool>(http_get_); }
    bool hasHttpDownload() const { return static_cast<bool>(http_download_); }
    bool hasFileHasher() const { return static_cast<bool>(file_hasher_); }

    // Запрашивает последний релиз и сравнивает с current_version.
    CheckResult check(const std::string& current_version) const;

    // Скачивает ассет и сверяет SHA-256 с ожидаемым из релиза.
    // При несовпадении файл удаляется и возвращается false.
    bool downloadAndVerify(const Asset& asset, const std::string& dest_path,
                           std::string& out_hex, std::string& error) const;

    static std::string releasesApiUrl();

private:
    HttpGet http_get_;
    HttpDownload http_download_;
    FileHasher file_hasher_;
};

} // namespace updater
} // namespace gno
