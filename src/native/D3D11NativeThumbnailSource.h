#pragma once

#include "native/NativeThumbnailRuntime.h"
#include "textures/D3D11TextureManager.h"
#include "textures/TextureResolver.h"
#include "textures/ThumbnailDiskCache.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>

namespace stui::native {

enum class NativeThumbnailFailureStage {
    resolve,
    upload,
    exception,
};

enum class NativeThumbnailTimingStage {
    diskLookup,
    resolve,
    prepare,
    upload,
    store,
};

using NativeThumbnailTimingObserver =
    std::function<void(std::string_view, NativeThumbnailTimingStage, double)>;

using NativeThumbnailFailureObserver =
    std::function<void(std::string_view, NativeThumbnailFailureStage)>;

class D3D11NativeThumbnailSource final : public NativeThumbnailTextureSource {
public:
    D3D11NativeThumbnailSource(
        std::filesystem::path looseRoot,
        ID3D11Device* device,
        std::size_t capacity,
        textures::TextureCacheDuration idleTtl,
        textures::TextureArchiveReader archiveReader,
        NativeThumbnailFailureObserver failureObserver = {},
        std::filesystem::path diskCacheDirectory = {},
        NativeThumbnailTimingObserver timingObserver = {});

    [[nodiscard]] std::shared_ptr<textures::D3D11Texture> find(
        std::string_view texturePath,
        textures::TextureCacheTimePoint now) override;
    [[nodiscard]] NativeThumbnailLoadResult load(
        std::string_view texturePath,
        const NativeThumbnailCancellationCheck& isCurrent,
        textures::TextureCacheTimePoint now) override;
    void pruneExpired(textures::TextureCacheTimePoint now) override;
    void clear() override;
    [[nodiscard]] bool clearDiskCache() override;
    [[nodiscard]] bool prepareDiskCache(std::string_view texturePath) override;
    [[nodiscard]] std::uintmax_t diskCacheBytes() const override;
    [[nodiscard]] bool available() const noexcept override;

private:
    bool m_available;
    textures::TextureResolver m_resolver;
    textures::D3D11TextureManager m_manager;
    std::unique_ptr<textures::ThumbnailDiskCache> m_diskCache;
    textures::TextureArchiveReader m_archiveReader;
    NativeThumbnailFailureObserver m_failureObserver;
    NativeThumbnailTimingObserver m_timingObserver;
    mutable std::mutex m_mutex;
};

}  // namespace stui::native
