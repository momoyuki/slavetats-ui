#include "native/D3D11NativeThumbnailSource.h"
#include "textures/DdsDecoder.h"

#include <chrono>
#include <utility>

namespace stui::native {
namespace {

NativeThumbnailFailure mapResolveError(textures::TextureResolveError error) {
    switch (error) {
    case textures::TextureResolveError::temporarilyUnavailable:
        return NativeThumbnailFailure::cancelled;
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
    std::filesystem::path diskCacheDirectory,
    NativeThumbnailTimingObserver timingObserver)
    : m_available(device != nullptr),
      m_resolver(looseRoot),
      m_manager(device, capacity, idleTtl, 16 * 1024 * 1024),
      m_revisionCache({}, looseRoot),
      m_diskCache(diskCacheDirectory.empty() ? nullptr
          : std::make_unique<textures::ThumbnailDiskCache>(
                std::move(diskCacheDirectory), looseRoot)),
      m_archiveReader(std::move(archiveReader)),
      m_failureObserver(std::move(failureObserver)),
      m_timingObserver(std::move(timingObserver)) {
    m_cachedDiskBytes.store(m_diskCache ? m_diskCache->bytes() : 0);
}

std::shared_ptr<textures::D3D11Texture> D3D11NativeThumbnailSource::find(
    std::string_view texturePath,
    textures::TextureCacheTimePoint now) {
    if (!available()) {
        return nullptr;
    }

    std::unique_lock lock(m_mutex, std::try_to_lock);
    return lock.owns_lock() ? m_manager.find(texturePath, now) : nullptr;
}

NativeThumbnailLoadResult D3D11NativeThumbnailSource::load(
    std::string_view texturePath,
    const NativeThumbnailCancellationCheck& isCurrent,
    textures::TextureCacheTimePoint now) {
    const auto report = [this, texturePath](NativeThumbnailTimingStage stage,
                            std::chrono::steady_clock::time_point start) {
        if (m_timingObserver) {
            const auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            m_timingObserver(texturePath, stage, elapsed);
        }
    };
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
        std::scoped_lock lock(m_mutex);
        const auto lookupStart = std::chrono::steady_clock::now();
        if (auto bytes = m_diskCache->load(texturePath)) {
            report(NativeThumbnailTimingStage::diskLookup, lookupStart);
            if (isCurrent && !isCurrent())
                return std::unexpected(NativeThumbnailFailure::cancelled);
            const auto uploadStart = std::chrono::steady_clock::now();
            if (auto cached = m_manager.getOrLoad(texturePath, *bytes, now)) {
                report(NativeThumbnailTimingStage::upload, uploadStart);
                return std::move(*cached);
            }
            report(NativeThumbnailTimingStage::upload, uploadStart);
        } else {
            report(NativeThumbnailTimingStage::diskLookup, lookupStart);
        }
    }

    textures::TextureResolveResult resolved = std::unexpected(textures::TextureResolveError::readFailed);
    try {
        const auto resolveStart = std::chrono::steady_clock::now();
        resolved = m_resolver.resolve(texturePath, m_archiveReader);
        report(NativeThumbnailTimingStage::resolve, resolveStart);
    } catch (...) {
        if (m_failureObserver) {
            m_failureObserver(texturePath, NativeThumbnailFailureStage::exception);
        }
        return std::unexpected(NativeThumbnailFailure::broken);
    }
    if (!resolved) {
        if (m_failureObserver && resolved.error() != textures::TextureResolveError::temporarilyUnavailable) {
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
        const auto prepareStart = std::chrono::steady_clock::now();
        auto diskThumbnail = m_diskCache
            ? textures::makeCachedThumbnailDds(resolved->bytes) : std::nullopt;
        report(NativeThumbnailTimingStage::prepare, prepareStart);
        const auto bytes = diskThumbnail
            ? std::span<const std::uint8_t>(*diskThumbnail)
            : std::span<const std::uint8_t>(resolved->bytes);
        const auto uploadStart = std::chrono::steady_clock::now();
        auto loaded = m_manager.getOrLoad(resolved->normalizedPath, bytes, now);
        report(NativeThumbnailTimingStage::upload, uploadStart);
        if (!loaded) {
            if (m_failureObserver) {
                m_failureObserver(texturePath, NativeThumbnailFailureStage::upload);
            }
            return std::unexpected(NativeThumbnailFailure::broken);
        }
        if (diskThumbnail && m_diskCache) {
            const auto storeStart = std::chrono::steady_clock::now();
            m_diskCache->store(resolved->normalizedPath, *diskThumbnail);
            m_cachedDiskBytes.store(m_diskCache->bytes());
            report(NativeThumbnailTimingStage::store, storeStart);
        }
        return std::move(*loaded);
    } catch (...) {
        if (m_failureObserver) {
            m_failureObserver(texturePath, NativeThumbnailFailureStage::exception);
        }
        return std::unexpected(NativeThumbnailFailure::broken);
    }
}

NativeThumbnailLoadResult D3D11NativeThumbnailSource::load(const core::TextureResource& resource,
    const NativeThumbnailCancellationCheck& isCurrent, textures::TextureCacheTimePoint now) {
    if (!available()) return std::unexpected(NativeThumbnailFailure::deviceUnavailable);
    if (isCurrent && !isCurrent()) return std::unexpected(NativeThumbnailFailure::cancelled);
    const auto revision = m_revisionCache.revision(resource);
    if (!revision) return std::unexpected(NativeThumbnailFailure::broken);
    {
        std::scoped_lock lock(m_mutex);
        if (auto cached = m_manager.find(resource, *revision, now)) return cached;
        if (m_diskCache) {
            if (auto bytes = m_diskCache->load(resource)) {
                if (isCurrent && !isCurrent()) return std::unexpected(NativeThumbnailFailure::cancelled);
                if (m_revisionCache.revision(resource) != revision) return std::unexpected(NativeThumbnailFailure::cancelled);
                if (auto cached = m_manager.getOrLoad(resource, *revision, *bytes, now)) return *cached;
            }
        }
    }
    try {
        const auto resolved = m_resolver.resolve(resource, m_archiveReader);
        if (!resolved) return std::unexpected(mapResolveError(resolved.error()));
        if (isCurrent && !isCurrent()) return std::unexpected(NativeThumbnailFailure::cancelled);
        auto thumbnail = m_diskCache ? textures::makeCachedThumbnailDds(resolved->bytes) : std::nullopt;
        std::scoped_lock lock(m_mutex);
        if (isCurrent && !isCurrent()) return std::unexpected(NativeThumbnailFailure::cancelled);
        if (m_revisionCache.revision(resource) != revision) return std::unexpected(NativeThumbnailFailure::cancelled);
        const auto bytes = thumbnail ? std::span<const std::uint8_t>(*thumbnail) : std::span<const std::uint8_t>(resolved->bytes);
        auto loaded = m_manager.getOrLoad(resource, *revision, bytes, now);
        if (!loaded) return std::unexpected(NativeThumbnailFailure::broken);
        if (thumbnail && m_diskCache) {
            m_diskCache->store(resource, *thumbnail, *revision);
            m_cachedDiskBytes.store(m_diskCache->bytes());
        }
        return std::move(*loaded);
    } catch (...) {
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

bool D3D11NativeThumbnailSource::setDiskCacheLimit(std::uintmax_t bytes) {
    const std::scoped_lock lock(m_mutex);
    const bool result = m_diskCache && m_diskCache->setMaxBytes(bytes);
    m_cachedDiskBytes.store(m_diskCache ? m_diskCache->bytes() : 0);
    return result;
}

bool D3D11NativeThumbnailSource::clearDiskCache() {
    std::scoped_lock lock(m_mutex);
    m_manager.clear();
    const bool result = m_diskCache && m_diskCache->clear();
    m_cachedDiskBytes.store(m_diskCache ? m_diskCache->bytes() : 0);
    return result;
}

bool D3D11NativeThumbnailSource::prepareDiskCache(std::string_view texturePath) {
    if (!m_diskCache) return false;
    {
        std::scoped_lock lock(m_mutex);
        if (auto cached = m_diskCache->load(texturePath);
            cached && textures::decodeDds(*cached)) return true;
    }
    textures::TextureResolveResult resolved =
        std::unexpected(textures::TextureResolveError::readFailed);
    try {
        resolved = m_resolver.resolve(texturePath, m_archiveReader);
        if (!resolved) return false;
        auto thumbnail = textures::makeCachedThumbnailDds(resolved->bytes);
        if (!thumbnail) return false;
        std::scoped_lock lock(m_mutex);
        m_diskCache->store(resolved->normalizedPath, *thumbnail);
        m_cachedDiskBytes.store(m_diskCache->bytes());
        const auto stored = m_diskCache->load(resolved->normalizedPath);
        return stored && textures::decodeDds(*stored).has_value();
    } catch (...) {
        return false;
    }
}

std::uintmax_t D3D11NativeThumbnailSource::diskCacheBytes() const {
    return m_cachedDiskBytes.load();
}

bool D3D11NativeThumbnailSource::available() const noexcept {
    return m_available;
}

}  // namespace stui::native
