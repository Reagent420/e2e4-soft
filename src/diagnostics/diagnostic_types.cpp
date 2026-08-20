#include "diagnostics/diagnostic_types.h"

namespace gno {

CancellationToken::CancellationToken(std::shared_ptr<const std::atomic<bool>> state)
    : state_(std::move(state)) {}

bool CancellationToken::isCancelled() const noexcept {
    return state_ && state_->load(std::memory_order_relaxed);
}

CancellationSource::CancellationSource()
    : state_(std::make_shared<std::atomic<bool>>(false)) {}

CancellationToken CancellationSource::token() const {
    return CancellationToken(state_);
}

void CancellationSource::cancel() noexcept {
    state_->store(true, std::memory_order_relaxed);
}

std::optional<Ipv4Address> Ipv4Address::parse(std::string_view text) noexcept {
    std::array<uint8_t, 4> bytes{};
    std::size_t position = 0;

    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (position == text.size()) return std::nullopt;

        unsigned int value = 0;
        const std::size_t component_start = position;
        while (position < text.size() && text[position] != '.') {
            const char character = text[position];
            if (character < '0' || character > '9') return std::nullopt;

            value = value * 10U + static_cast<unsigned int>(character - '0');
            if (value > 255U) return std::nullopt;
            ++position;
        }

        if (position == component_start) return std::nullopt;
        bytes[index] = static_cast<uint8_t>(value);

        if (index + 1U == bytes.size()) {
            if (position != text.size()) return std::nullopt;
        } else {
            if (position == text.size()) return std::nullopt;
            ++position;
        }
    }

    return Ipv4Address(bytes);
}

std::string Ipv4Address::toString() const {
    std::string result;
    result.reserve(15);
    for (std::size_t index = 0; index < bytes_.size(); ++index) {
        if (index != 0) result.push_back('.');

        const unsigned int value = bytes_[index];
        if (value >= 100U) result.push_back(static_cast<char>('0' + value / 100U));
        if (value >= 10U) result.push_back(static_cast<char>('0' + (value / 10U) % 10U));
        result.push_back(static_cast<char>('0' + value % 10U));
    }
    return result;
}

} // namespace gno
