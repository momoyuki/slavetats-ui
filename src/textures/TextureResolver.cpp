#include "textures/TextureResolver.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <utility>

namespace stui::textures {
namespace {

inline constexpr std::string_view kTattooTextureResourceRoot =
    "textures\\actors\\character\\slavetats\\";

TextureBytesResult readLooseBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::unexpected(TextureResolveError::readFailed);
    }

    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > 128ULL * 1024 * 1024)
        return std::unexpected(TextureResolveError::readFailed);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        return std::unexpected(TextureResolveError::readFailed);
    }
    return bytes;
}

std::expected<std::filesystem::path, TextureResolveError> containedLoosePath(
    const std::filesystem::path& root,
    std::string_view normalizedPath) {
    std::error_code error;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
    if (error) {
        return std::unexpected(TextureResolveError::readFailed);
    }

    const auto candidate = std::filesystem::weakly_canonical(
        root / std::filesystem::path(normalizedPath), error);
    if (error) {
        return std::unexpected(TextureResolveError::readFailed);
    }

    const auto relative = std::filesystem::relative(candidate, canonicalRoot, error);
    if (error || relative.is_absolute()) {
        return std::unexpected(TextureResolveError::invalidPath);
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return std::unexpected(TextureResolveError::invalidPath);
        }
    }
    return candidate;
}

TextureResolveResult resolveArchiveBytes(
    std::string normalizedPath,
    const TextureArchiveReader& archiveReader) {
    if (!archiveReader) {
        return std::unexpected(TextureResolveError::readFailed);
    }
    auto bytes = archiveReader(
        std::string(kTattooTextureResourceRoot) + normalizedPath);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return ResolvedTexture{
        .normalizedPath = std::move(normalizedPath),
        .bytes = std::move(*bytes),
        .source = TextureSource::archive,
    };
}

}  // namespace

TextureResolver::TextureResolver(std::filesystem::path looseRoot)
    : m_looseRoot(std::move(looseRoot)) {}

std::optional<TextureResource> TextureResolver::matchingExternal(std::string_view property, std::string_view material) {
    const auto first = resource(TextureResourceKind::external, property);
    const auto second = resource(TextureResourceKind::external, material);
    if (!first || !second || *first != *second) return std::nullopt;
    return *first;
}

std::expected<TextureResource, TextureResolveError> TextureResolver::resource(
    TextureResourceKind kind, std::string_view path) {
    if (kind != TextureResourceKind::catalog && kind != TextureResourceKind::external)
        return std::unexpected(TextureResolveError::invalidPath);
    auto normalized = kind == TextureResourceKind::external ? normalizeExternal(path) : normalize(path);
    if (!normalized) return std::unexpected(normalized.error());
    if (kind == TextureResourceKind::catalog) normalized->insert(0, kTattooTextureResourceRoot);
    std::ranges::transform(*normalized, normalized->begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return TextureResource{kind, std::move(*normalized)};
}

std::expected<std::string, TextureResolveError> TextureResolver::normalize(
    std::string_view texturePath) {
    if (texturePath.empty() || texturePath.front() == '/' || texturePath.front() == '\\' ||
        texturePath.find(':') != std::string_view::npos) {
        return std::unexpected(TextureResolveError::invalidPath);
    }

    std::string normalized;
    std::string segment;
    const auto appendSegment = [&]() -> bool {
        if (segment.empty() || segment == ".") {
            segment.clear();
            return true;
        }
        if (segment == "..") {
            return false;
        }
        if (!normalized.empty()) {
            normalized.push_back('\\');
        }
        normalized += segment;
        segment.clear();
        return true;
    };

    for (const char character : texturePath) {
        if (character == '/' || character == '\\') {
            if (!appendSegment()) {
                return std::unexpected(TextureResolveError::invalidPath);
            }
        } else {
            segment.push_back(character);
        }
    }
    if (!appendSegment() || normalized.empty()) {
        return std::unexpected(TextureResolveError::invalidPath);
    }
    return normalized;
}

std::expected<std::string, TextureResolveError> TextureResolver::normalizeExternal(
    std::string_view texturePath) {
    if (texturePath.find('\0') != std::string_view::npos) {
        return std::unexpected(TextureResolveError::invalidPath);
    }
    auto normalized = normalize(texturePath);
    if (!normalized) {
        return std::unexpected(normalized.error());
    }
    std::string lower = *normalized;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    constexpr std::string_view prefix = "textures\\";
    // SKEE may return Actor overlay paths relative to Data/Textures.
    if (lower.starts_with("actors\\")) {
        normalized->insert(0, prefix);
        lower.insert(0, prefix);
    }
    if (!lower.starts_with(prefix) || lower.size() <= prefix.size() + 4 ||
        !lower.ends_with(".dds")) {
        return std::unexpected(TextureResolveError::invalidPath);
    }
    normalized->replace(0, prefix.size(), prefix);
    return normalized;
}

TextureResolveResult TextureResolver::resolve(
    std::string_view texturePath,
    const TextureArchiveReader& archiveReader) const {
    auto normalized = normalize(texturePath);
    if (!normalized) {
        return std::unexpected(normalized.error());
    }

    if (auto loose = resolveLoose(*normalized)) {
        return loose;
    }

    return resolveArchiveBytes(std::move(*normalized), archiveReader);
}

TextureResolveResult TextureResolver::resolve(const TextureResource& resource, const TextureArchiveReader& reader) const {
    if (resource.kind() == TextureResourceKind::external) return resolveExternal(resource.path(), reader);
    return resolve(std::string_view(resource.path()).substr(kTattooTextureResourceRoot.size()), reader);
}

TextureResolveResult TextureResolver::resolveExternal(std::string_view texturePath, const TextureArchiveReader& archiveReader) const {
    auto normalized = normalizeExternal(texturePath);
    if (!normalized) return std::unexpected(normalized.error());
    const auto textureRoot = m_looseRoot.parent_path().parent_path().parent_path();
    const auto relative = std::string_view(*normalized).substr(std::string_view("textures\\").size());
    auto path = containedLoosePath(textureRoot, relative);
    if (path) {
        std::error_code error;
        if (std::filesystem::is_regular_file(*path, error) && !error) {
            auto bytes = readLooseBytes(*path);
            if (!bytes) return std::unexpected(bytes.error());
            return ResolvedTexture{.normalizedPath = std::move(*normalized), .bytes = std::move(*bytes), .source = TextureSource::loose};
        }
    }
    if (!archiveReader) return std::unexpected(TextureResolveError::readFailed);
    auto bytes = archiveReader(*normalized);
    if (!bytes) return std::unexpected(bytes.error());
    return ResolvedTexture{.normalizedPath = std::move(*normalized), .bytes = std::move(*bytes), .source = TextureSource::archive};
}

TextureResolveResult TextureResolver::resolveLoose(std::string_view texturePath) const {
    auto normalized = normalize(texturePath);
    if (!normalized) {
        return std::unexpected(normalized.error());
    }

    auto loosePath = containedLoosePath(m_looseRoot, *normalized);
    if (!loosePath) {
        return std::unexpected(loosePath.error());
    }
    std::error_code error;
    const bool looseExists = std::filesystem::is_regular_file(*loosePath, error);
    if (error == std::errc::no_such_file_or_directory) {
        error.clear();
    }
    if (error) {
        return std::unexpected(TextureResolveError::readFailed);
    }
    if (looseExists) {
        auto bytes = readLooseBytes(*loosePath);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        return ResolvedTexture{
            .normalizedPath = std::move(*normalized),
            .bytes = std::move(*bytes),
            .source = TextureSource::loose,
        };
    }

    return std::unexpected(TextureResolveError::notFound);
}

TextureResolveResult TextureResolver::resolveArchive(
    std::string_view texturePath,
    const TextureArchiveReader& archiveReader) const {
    auto normalized = normalize(texturePath);
    if (!normalized) {
        return std::unexpected(normalized.error());
    }

    return resolveArchiveBytes(std::move(*normalized), archiveReader);
}

}  // namespace stui::textures
