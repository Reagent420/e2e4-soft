#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// SHA-256 без внешних зависимостей: нужен для проверки целостности
// скачанного обновления (core/updater.h). Qt здесь не используется
// намеренно — модуль должен собираться и тестироваться без GUI.
namespace gno {
namespace sha256 {

// Шестнадцатеричная строка в нижнем регистре, 64 символа.
std::string hashBytes(const void* data, std::size_t len);
std::string hashString(const std::string& data);

// Потоковое хеширование файла: обновление может весить десятки мегабайт,
// целиком в память его тянуть не нужно. Возвращает false, если файл не
// открылся или прочитался не полностью.
bool hashFile(const std::string& path, std::string& out_hex, std::string& out_error);

// Инкрементальный интерфейс — для случаев, когда данные приходят кусками
// (например, из сетевого ответа).
class Hasher {
public:
    Hasher();
    void update(const void* data, std::size_t len);
    // Возвращает hex-строку. Повторный вызов после final() не допускается.
    std::string final();

private:
    void processBlock(const std::uint8_t* block);
    // Внутренняя подача данных без проверки finalized_ — нужна для паддинга
    // в final(), где флаг уже взведён. Длину не считает: паддинг в неё не входит.
    void feed(const std::uint8_t* data, std::size_t len);
    std::uint32_t state_[8];
    std::uint64_t total_len_;
    std::uint8_t buffer_[64];
    std::size_t buffer_len_;
    bool finalized_;
};

} // namespace sha256
} // namespace gno
