#include "sha256.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace gno {
namespace sha256 {
namespace {

// Таблица раундовых констант (дробная часть кубических корней первых 64 простых).
const std::uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline std::uint32_t rotr(std::uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

inline std::uint32_t bigEndian(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

inline void storeBigEndian(std::uint32_t v, std::uint8_t* p) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

} // namespace

void Hasher::processBlock(const std::uint8_t* block) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = bigEndian(block + i * 4);
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + ch + K[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

Hasher::Hasher() : total_len_(0), buffer_len_(0), finalized_(false) {
    state_[0] = 0x6a09e667u; state_[1] = 0xbb67ae85u;
    state_[2] = 0x3c6ef372u; state_[3] = 0xa54ff53au;
    state_[4] = 0x510e527fu; state_[5] = 0x9b05688cu;
    state_[6] = 0x1f83d9abu; state_[7] = 0x5be0cd19u;
    std::memset(buffer_, 0, sizeof(buffer_));
}

void Hasher::feed(const std::uint8_t* p, std::size_t len) {
    if (len == 0) return;

    // Добор до полного блока из уже накопленного.
    if (buffer_len_ > 0) {
        const std::size_t need = 64 - buffer_len_;
        const std::size_t take = len < need ? len : need;
        std::memcpy(buffer_ + buffer_len_, p, take);
        buffer_len_ += take;
        p += take;
        len -= take;
        if (buffer_len_ == 64) {
            processBlock(buffer_);
            buffer_len_ = 0;
        }
    }

    while (len >= 64) {
        processBlock(p);
        p += 64;
        len -= 64;
    }

    if (len > 0) {
        std::memcpy(buffer_, p, len);
        buffer_len_ = len;
    }
}

void Hasher::update(const void* data, std::size_t len) {
    if (finalized_ || len == 0) return;
    total_len_ += len;
    feed(static_cast<const std::uint8_t*>(data), len);
}

std::string Hasher::final() {
    if (finalized_)
        return std::string();
    finalized_ = true;

    const std::uint64_t bit_len = total_len_ * 8ull;

    // Паддинг: 0x80, нули, затем 64-битная длина в битах.
    // Нулей нужно 55 - buffer_len_ (а не 56 - ...), потому что сам байт 0x80
    // тоже занимает место: buffer_len_ + 1 + zeros должно дать ровно 56,
    // чтобы следом влезли 8 байт длины. Ошибка на единицу здесь оставляла
    // последний блок необработанным и портила хеш (ловилось тестами).
    std::uint8_t pad[128];
    std::memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    const std::size_t pad_len = (buffer_len_ < 56) ? (55 - buffer_len_)
                                                   : (119 - buffer_len_);
    feed(pad, pad_len + 1); // +1 — сам байт 0x80

    std::uint8_t len_be[8];
    for (int i = 0; i < 8; ++i)
        len_be[i] = static_cast<std::uint8_t>(bit_len >> (56 - i * 8));
    feed(len_be, 8);

    std::uint8_t out[32];
    for (int i = 0; i < 8; ++i)
        storeBigEndian(state_[i], out + i * 4);

    static const char* hex = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (int i = 0; i < 32; ++i) {
        result.push_back(hex[out[i] >> 4]);
        result.push_back(hex[out[i] & 0x0f]);
    }
    return result;
}

std::string hashBytes(const void* data, std::size_t len) {
    Hasher h;
    h.update(data, len);
    return h.final();
}

std::string hashString(const std::string& data) {
    return hashBytes(data.data(), data.size());
}

bool hashFile(const std::string& path, std::string& out_hex, std::string& out_error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        out_error = "не удалось открыть файл: " + path;
        return false;
    }

    Hasher h;
    std::vector<char> buf(64 * 1024);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got > 0)
            h.update(buf.data(), static_cast<std::size_t>(got));
    }
    // bad() означает ошибку чтения, а не конец файла: хеш такого файла
    // доверять нельзя — иначе повреждённая загрузка пройдёт проверку.
    if (in.bad()) {
        out_error = "ошибка чтения файла: " + path;
        return false;
    }

    out_hex = h.final();
    out_error.clear();
    return true;
}

} // namespace sha256
} // namespace gno
