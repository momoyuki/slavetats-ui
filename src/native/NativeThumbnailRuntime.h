#pragma once

#include "native/NativeThumbnailController.h"
#include "native/NativeThumbnailWorker.h"
#include "textures/TextureCache.h"
#include "core/TextureResource.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stui::native {

using NativeThumbnailTask = std::function<void()>;
using NativeThumbnailScheduler = std::function<void(NativeThumbnailTask)>;
using NativeThumbnailNow = std::function<textures::TextureCacheTimePoint()>;
using NativeThumbnailCancellationCheck = std::function<bool()>;

class NativeThumbnailTextureSource {
public:
    virtual ~NativeThumbnailTextureSource() = default;
    virtual std::shared_ptr<textures::D3D11Texture> find(
        std::string_view texturePath,
        textures::TextureCacheTimePoint now) = 0;
    virtual NativeThumbnailLoadResult load(
        std::string_view texturePath,
        const NativeThumbnailCancellationCheck& isCurrent,
        textures::TextureCacheTimePoint now) = 0;
    virtual NativeThumbnailLoadResult load(const core::TextureResource&, const NativeThumbnailCancellationCheck&,
        textures::TextureCacheTimePoint) { return std::unexpected(NativeThumbnailFailure::broken); }
    virtual void pruneExpired(textures::TextureCacheTimePoint now) = 0;
    virtual void clear() = 0;
    [[nodiscard]] virtual bool setDiskCacheLimit(std::uintmax_t) { return false; }
    [[nodiscard]] virtual bool clearDiskCache() { return false; }
    [[nodiscard]] virtual bool prepareDiskCache(std::string_view) { return false; }
    [[nodiscard]] virtual std::uintmax_t diskCacheBytes() const { return 0; }
    virtual bool available() const noexcept = 0;
};

struct ThumbnailCacheBuildStatus {
    std::size_t completed{};
    std::size_t total{};
    std::size_t failed{};
    bool active{};
};

enum class ThumbnailCacheClearState {
    idle,
    pending,
    succeeded,
    failed,
};

class NativeThumbnailRuntime {
public:
    NativeThumbnailRuntime(
        std::unique_ptr<NativeThumbnailTextureSource> source,
        NativeThumbnailScheduler scheduler,
        NativeThumbnailNow now, bool background = false);
    ~NativeThumbnailRuntime();

    void synchronize(
        repository::TattooCatalogSnapshot snapshot,
        const repository::TattooPage& page);
    void synchronize(
        NativeThumbnailEpoch epoch,
        std::span<const std::string> texturePaths);
    void synchronizeResources(NativeThumbnailEpoch epoch, std::span<const core::TextureResource> resources);
    void pump();
    [[nodiscard]] std::vector<NativeThumbnailView> views() const;
    void reset();
    [[nodiscard]] bool requestDiskCacheLimit(
        std::uint32_t limitMiB, std::function<bool(std::uint32_t)> persist = {});
    [[nodiscard]] ThumbnailCacheClearState diskCacheLimitState() const;
    [[nodiscard]] std::uint32_t diskCacheLimitMiB() const;
    [[nodiscard]] bool requestDiskCacheClear();
    [[nodiscard]] ThumbnailCacheClearState diskCacheClearState() const;
    void startDiskCacheBuild(std::vector<std::string> texturePaths);
    void pumpDiskCacheBuild();
    [[nodiscard]] ThumbnailCacheBuildStatus diskCacheBuildStatus() const;
    [[nodiscard]] std::uintmax_t diskCacheBytes() const;

private:
    void dispatch(NativeThumbnailTask task);
    [[nodiscard]] bool clearDiskCacheNow();
    std::unique_ptr<NativeThumbnailTextureSource> m_source;
    NativeThumbnailScheduler m_scheduler;
    NativeThumbnailNow m_now;
    NativeThumbnailController m_controller;
    std::optional<textures::TextureCacheTimePoint> m_lastPrune;
    mutable std::mutex m_buildMutex;
    std::vector<std::string> m_buildPaths;
    ThumbnailCacheBuildStatus m_buildStatus;
    ThumbnailCacheClearState m_clearState{ThumbnailCacheClearState::idle};
    ThumbnailCacheClearState m_limitState{ThumbnailCacheClearState::idle};
    std::uint32_t m_diskCacheLimitMiB{256};
    bool m_buildInFlight{};
    std::uint64_t m_buildGeneration{};
    std::atomic_bool m_stopping{};
    // Must join before controller/source members are destroyed.
    std::unique_ptr<NativeThumbnailWorker> m_worker;
};

}  // namespace stui::native
