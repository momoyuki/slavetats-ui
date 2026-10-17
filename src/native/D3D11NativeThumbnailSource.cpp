#include "native/D3D11NativeThumbnailSource.h"

#include <utility>

namespace stui::native {
namespace {

NativeThumbnailFailure mapResolveError(textures::TextureResolveError error) {
    switch (error) {
    case textures::TextureResolveError::notFound:
        return NativeThumbnailFailure::missing;
    case textures::TextureResolveError::invalidPath:
    case textures::TextureResolveError::readFailed:
        return NativeThumbnailFailure::broken;
    }

    return NativeThumbnailFailure::broken;
}

}  // namespace

D3D11NativeThumbnailSource::D3D11NativeThumbnailSource(
    std::filesystem::path looseRoot,
    ID3D11Device* device,
    std::size_t capacity,
    textures::TextureCacheDuration idleTtl,
    textures::TextureArchiveReader archiveReader,
    NativeThumbnailFailureObserver failureObserver,
    std::filesystem::path diskCacheDirectory)
    : m_available(device != nullptr),
      m_resolver(looseRoot),
      m_manager(device, capacity, idleTtl, 16 * 1024 * 1024),
      m_diskCache(diskCacheDirectory.empty() ? nullptr
          : std::make_unique<textures::ThumbnailDiskCache>(
                std::move(diskCacheDirectory), looseRoot)),
      m_archiveReader(std::move(archiveReader)),
      m_failureObserver(std::move(failureObserver)) {}

std::shared_ptr<textures::D3D11Texture> D3D11NativeThumbnailSource::find(
    std::string_view texturePath,
    textures::TextureCacheTimePoint now) {
    if (!available()) {
        return nullptr;
    }

    std::scoped_lock lock(m_mutex);
    return m_manager.find(texturePath, now);
}

NativeThumbnailLoadResult D3D11NativeThumbnailSource::load(
    std::string_view texturePath,
    const NativeThumbnailCancellationCheck& isCurrent,
    textures::TextureCacheTimePoint now) {
    if (!available()) {
        return std::unexpected(NativeThumbnailFailure::deviceUnavailable);
    }
    if (isCurrent && !isCurrent()) {
        return std::unexpected(NativeThumbnailFailure::cancelled);
    }
    if (auto cached = find(texturePath, now)) {
        return cached;
    }
    if (m_diskCache) {
        if (auto bytes = m_diskCache->load(texturePath)) {
            std::scoped_lock lock(m_mutex);
            if (isCurrent && !isCurrent())
                return std::unexpected(NativeThumbnailFailure::cancelled);
            if (auto cached = m_manager.getOrLoad(texturePath, *bytes, now))
                return std::move(*cached);
        }
    }

    textures::TextureResolveResult resolved = std::unexpected(textures::TextureResolveError::readFailed);
    try {
        resolved = m_resolver.resolve(texturePath, m_archiveReader);
    } catch (...) {
        if (m_failureObserver) {
            m_failureObserver(texturePath, NativeThumbnailFailureStage::exception);
        }
        return std::unexpected(NativeThumbnailFailure::broken);
    }
    if (!resolved) {
        if (m_failureObserver) {
            m_failureObserver(texturePath, NativeThumbnailFailureStage::resolve);
        }
        return std::unexpected(mapResolveError(resolved.error()));
    }
    if (isCurrent && !isCurrent()) {
        return std::unexpected(NativeThumbnailFailure::cancelled);
    }

    std::scoped_lock lock(m_mutex);
    if (isCurrent && !isCurrent()) {
        return std::unexpected(NativeThumbnailFailure::cancelled);
    }
    try {
        auto diskThumbnail = m_diskCache
            ? textures::makeCachedThumbnailDds(resolved->bytes) : std::nullopt;
        const auto bytes = diskThumbnail
            ? std::span<const std::uint8_t>(*diskThumbnail)
            : std::span<const std::uint8_t>(resolved->bytes);
        auto loaded = m_manager.getOrLoad(resolved->normalizedPath, bytes, now);
        if (!loaded) {
            if (m_failureObserver) {
                m_failureObserver(texturePath, NativeThumbnailFailureStage::upload);
            }
            return std::unexpected(NativeThumbnailFailure::broken);
        }
        if (diskThumbnail && m_diskCache)
            m_diskCache->store(resolved->normalizedPath, *diskThumbnail);
        return std::move(*loaded);
    } catch (...) {
        if (m_failureObserver) {
            m_failureObserver(texturePath, NativeThumbnailFailureStage::exception);
        }
        return std::unexpected(NativeThumbnailFailure::broken);
    }
}

void D3D11NativeThumbnailSource::pruneExpired(textures::TextureCacheTimePoint now) {
    std::scoped_lock lock(m_mutex);
    m_manager.pruneExpired(now);
}

void D3D11NativeThumbnailSource::clear() {
    std::scoped_lock lock(m_mutex);
    m_manager.clear();
}

bool D3D11NativeThumbnailSource::clearDiskCache() {
    std::scoped_lock lock(m_mutex);
    m_manager.clear();
    return m_diskCache && m_diskCache->clear();
}

bool D3D11NativeThumbnailSource::available() const noexcept {
    return m_available;
}

}  // namespace stui::native
