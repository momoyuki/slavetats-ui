#pragma once

#include "textures/TextureCache.h"
#include "textures/TextureResolver.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace stui::textures {

enum class TextureManagerError {
    invalidPath,
    uploadFailed,
};

template <class Resource>
class TextureManager {
public:
    explicit TextureManager(
        std::size_t capacity,
        TextureCacheDuration idleTtl = TextureCacheDuration::max(),
        std::size_t maxBytes = SIZE_MAX,
        std::function<std::size_t(const Resource&)> resourceBytes = {})
        : m_cache(capacity, idleTtl, maxBytes, std::move(resourceBytes)) {}

    [[nodiscard]] std::shared_ptr<Resource> find(
        std::string_view texturePath,
        TextureCacheTimePoint now = TextureCacheClock::now()) {
        auto normalized = normalizePath(texturePath);
        if (!normalized) {
            return nullptr;
        }
        return m_cache.get(*normalized, now);
    }

    template <class Uploader>
    std::expected<std::shared_ptr<Resource>, TextureManagerError> getOrLoad(
        std::string_view texturePath,
        std::span<const std::uint8_t> ddsBytes,
        Uploader&& uploader,
        TextureCacheTimePoint now = TextureCacheClock::now()) {
        auto normalized = normalizePath(texturePath);
        if (!normalized) {
            return std::unexpected(TextureManagerError::invalidPath);
        }

        auto resource = m_cache.getOrLoad(
            *normalized,
            [&]() { return std::invoke(std::forward<Uploader>(uploader), ddsBytes); },
            now);
        if (!resource) {
            return std::unexpected(TextureManagerError::uploadFailed);
        }
        return resource;
    }

    void pruneExpired(TextureCacheTimePoint now = TextureCacheClock::now()) {
        m_cache.pruneExpired(now);
    }

    void clear() noexcept { m_cache.clear(); }

    [[nodiscard]] std::size_t size() const noexcept { return m_cache.size(); }

private:
    [[nodiscard]] static std::expected<std::string, TextureManagerError> normalizePath(
        std::string_view texturePath) {
        auto normalized = TextureResolver::normalize(texturePath);
        if (!normalized) {
            return std::unexpected(TextureManagerError::invalidPath);
        }
        for (char& character : *normalized) {
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        }
        return std::move(*normalized);
    }

    TextureCache<Resource> m_cache;
};

}  // namespace stui::textures
