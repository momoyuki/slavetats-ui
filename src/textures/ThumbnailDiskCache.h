#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace stui::textures {

class ThumbnailDiskCache {
public:
    ThumbnailDiskCache(std::filesystem::path directory,
        std::filesystem::path looseRoot,
        std::uintmax_t maxBytes = 256 * 1024 * 1024);

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> load(
        std::string_view texturePath) const;
    void store(std::string_view texturePath, std::span<const std::uint8_t> ddsBytes);
    [[nodiscard]] bool clear();

private:
    [[nodiscard]] std::filesystem::path entryPath(std::string_view texturePath) const;
    void prune();

    std::filesystem::path m_directory;
    std::filesystem::path m_looseRoot;
    std::uint64_t m_archiveStamp{};
    std::uintmax_t m_maxBytes;
    std::uintmax_t m_estimatedBytes{};
};

}  // namespace stui::textures
