#pragma once

#include <cstdint>
#include <expected>
#include <vector>

namespace stui::textures {

enum class ExactStreamReadError {
    readFailed,
};

template <class Stream>
[[nodiscard]] std::expected<std::vector<std::uint8_t>, ExactStreamReadError>
readExactBytes(Stream& stream, std::uint32_t size) {
    // Bound archive allocations before asking the engine to read.
    if (size > 128U * 1024 * 1024)
        return std::unexpected(ExactStreamReadError::readFailed);
    std::vector<std::uint8_t> bytes(size);
    if (size > 0 && !stream.read(bytes.data(), size)) {
        return std::unexpected(ExactStreamReadError::readFailed);
    }
    return bytes;
}

}  // namespace stui::textures
