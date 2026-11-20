#pragma once

#include "core/TextureResource.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace stui::textures {

enum class TextureResolveError {
    invalidPath,
    notFound,
    readFailed,
    temporarilyUnavailable,
};

using core::TextureResource;
using core::TextureResourceKind;

enum class TextureSource {
    loose,
    archive,
};

using TextureBytesResult =
    std::expected<std::vector<std::uint8_t>, TextureResolveError>;
using TextureArchiveReader =
    std::function<TextureBytesResult(std::string_view resourcePath)>;

struct ResolvedTexture {
    std::string normalizedPath;
    std::vector<std::uint8_t> bytes;
    TextureSource source{};
};

using TextureResolveResult =
    std::expected<ResolvedTexture, TextureResolveError>;

class TextureResolver {
public:
    explicit TextureResolver(std::filesystem::path looseRoot);

    [[nodiscard]] static std::optional<TextureResource> matchingExternal(
        std::string_view property, std::string_view material);
    [[nodiscard]] static std::expected<TextureResource, TextureResolveError> resource(
        TextureResourceKind kind, std::string_view path);
    [[nodiscard]] static std::expected<std::string, TextureResolveError> normalize(
        std::string_view texturePath);
    [[nodiscard]] static std::expected<std::string, TextureResolveError> normalizeExternal(
        std::string_view texturePath);
    [[nodiscard]] TextureResolveResult resolve(
        const TextureResource& resource, const TextureArchiveReader& archiveReader) const;
    [[nodiscard]] TextureResolveResult resolve(
        std::string_view texturePath,
        const TextureArchiveReader& archiveReader) const;
    [[nodiscard]] TextureResolveResult resolveExternal(std::string_view texturePath, const TextureArchiveReader& archiveReader) const;
    [[nodiscard]] TextureResolveResult resolveLoose(std::string_view texturePath) const;
    [[nodiscard]] TextureResolveResult resolveArchive(
        std::string_view texturePath,
        const TextureArchiveReader& archiveReader) const;

private:
    std::filesystem::path m_looseRoot;
};

}  // namespace stui::textures
