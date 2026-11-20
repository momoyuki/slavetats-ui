#include "native/NativeThumbnailRuntime.h"
#include "runtime/ThumbnailCacheSetting.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <unordered_set>
#include <utility>

namespace stui::native {

NativeThumbnailRuntime::NativeThumbnailRuntime(
    std::unique_ptr<NativeThumbnailTextureSource> source,
    NativeThumbnailScheduler scheduler,
    NativeThumbnailNow now, bool background)
    : m_source(std::move(source)),
      m_scheduler(std::move(scheduler)),
      m_now(std::move(now)),
      m_worker(background ? std::make_unique<NativeThumbnailWorker>() : nullptr) {}

NativeThumbnailRuntime::~NativeThumbnailRuntime() {
    m_stopping.store(true);
    m_worker.reset();
}

void NativeThumbnailRuntime::dispatch(NativeThumbnailTask task) {
    if (m_worker) m_worker->submit(std::move(task));
    else m_scheduler(std::move(task));
}

void NativeThumbnailRuntime::synchronize(
    repository::TattooCatalogSnapshot snapshot,
    const repository::TattooPage& page) {
    std::vector<std::string> texturePaths;
    texturePaths.reserve(page.entries.size());
    for (const auto& entry : page.entries) {
        texturePaths.push_back(entry.texturePath);
    }
    synchronize(std::move(snapshot), texturePaths);
}

void NativeThumbnailRuntime::synchronize(
    NativeThumbnailEpoch epoch,
    std::span<const std::string> texturePaths) {
    const auto currentTime = m_now();
    const bool sourceAvailable = m_source->available();
    m_controller.synchronize(
        std::move(epoch),
        texturePaths,
        [this, currentTime, sourceAvailable](std::string_view texturePath) {
            return sourceAvailable ? m_source->find(texturePath, currentTime) : nullptr;
        });
}

void NativeThumbnailRuntime::synchronizeResources(NativeThumbnailEpoch epoch, std::span<const core::TextureResource> resources) {
    m_controller.synchronizeResources(std::move(epoch), resources);
}

void NativeThumbnailRuntime::pump() {
    using namespace std::chrono_literals;

    const auto currentTime = m_now();
    bool shouldPrune = false;
    {
        std::scoped_lock lock(m_buildMutex);
        if (!m_lastPrune || currentTime - *m_lastPrune >= 1s) {
            m_lastPrune = currentTime;
            shouldPrune = true;
        }
    }
    if (shouldPrune) {
        if (m_worker) {
            try { dispatch([this, currentTime] { m_source->pruneExpired(currentTime); }); } catch (...) {}
        } else m_source->pruneExpired(currentTime);
    }

    if (!m_source->available()) {
        return;
    }

    auto request = m_controller.takeNextRequest();
    if (!request) {
        return;
    }

    const NativeThumbnailRequest capturedRequest = std::move(*request);
    auto completionClaimed = std::make_shared<std::atomic_bool>(false);
    const auto complete = [this, capturedRequest, completionClaimed](
                              NativeThumbnailLoadResult result) {
        bool expected = false;
        if (completionClaimed->compare_exchange_strong(expected, true)) {
            m_controller.complete(capturedRequest, std::move(result));
        }
    };
    NativeThumbnailTask task = [this, capturedRequest, completionClaimed, complete] {
        if (completionClaimed->load()) {
            return;
        }

        NativeThumbnailLoadResult result =
            std::unexpected(NativeThumbnailFailure::broken);
        try {
            const auto current = [this, capturedRequest] {
                return !m_stopping.load() && m_controller.isCurrent(capturedRequest);
            };
            result = capturedRequest.resource
                ? m_source->load(*capturedRequest.resource, current, m_now())
                : m_source->load(capturedRequest.texturePath, current, m_now());
        } catch (...) {
        }
        complete(std::move(result));
    };

    try {
        dispatch(std::move(task));
    } catch (...) {
        complete(std::unexpected(NativeThumbnailFailure::broken));
    }
}

std::vector<NativeThumbnailView> NativeThumbnailRuntime::views() const {
    return m_controller.views();
}

void NativeThumbnailRuntime::reset() {
    {
        std::scoped_lock lock(m_buildMutex);
        ++m_buildGeneration;
        m_buildPaths.clear();
        m_buildStatus = {};
        m_buildInFlight = false;
        m_lastPrune.reset();
    }
    m_controller.clear();
    m_source->clear();
}

bool NativeThumbnailRuntime::clearDiskCacheNow() {
    reset();
    return m_source->clearDiskCache();
}

bool NativeThumbnailRuntime::requestDiskCacheLimit(
    std::uint32_t limitMiB, std::function<bool(std::uint32_t)> persist) {
    if (!runtime::ThumbnailCacheSetting::valid(limitMiB)) return false;
    std::uint32_t previous{};
    {
        std::scoped_lock lock(m_buildMutex);
        if (m_buildStatus.active || m_clearState == ThumbnailCacheClearState::pending ||
            m_limitState == ThumbnailCacheClearState::pending) return false;
        previous = m_diskCacheLimitMiB;
        m_limitState = ThumbnailCacheClearState::pending;
    }
    try {
        dispatch([this, limitMiB, previous, persist = std::move(persist)] {
            bool applied = false;
            bool succeeded = false;
            try {
                applied = m_source->setDiskCacheLimit(
                    static_cast<std::uintmax_t>(limitMiB) * 1024 * 1024);
                succeeded = applied && (!persist || persist(limitMiB));
            } catch (...) {}
            bool restored = false;
            if (applied && !succeeded) {
                // Evicted files remain disposable; report the new budget if restoration fails.
                try {
                    restored = m_source->setDiskCacheLimit(
                        static_cast<std::uintmax_t>(previous) * 1024 * 1024);
                } catch (...) {}
            }
            std::scoped_lock lock(m_buildMutex);
            if (applied && !restored) m_diskCacheLimitMiB = limitMiB;
            m_limitState = succeeded ? ThumbnailCacheClearState::succeeded :
                ThumbnailCacheClearState::failed;
        });
    } catch (...) {
        std::scoped_lock lock(m_buildMutex);
        m_limitState = ThumbnailCacheClearState::failed;
        return false;
    }
    return true;
}

ThumbnailCacheClearState NativeThumbnailRuntime::diskCacheLimitState() const {
    std::scoped_lock lock(m_buildMutex);
    return m_limitState;
}

std::uint32_t NativeThumbnailRuntime::diskCacheLimitMiB() const {
    std::scoped_lock lock(m_buildMutex);
    return m_diskCacheLimitMiB;
}

bool NativeThumbnailRuntime::requestDiskCacheClear() {
    {
        std::scoped_lock lock(m_buildMutex);
        if (m_buildStatus.active || m_clearState == ThumbnailCacheClearState::pending ||
            m_limitState == ThumbnailCacheClearState::pending)
            return false;
        m_clearState = ThumbnailCacheClearState::pending;
    }
    try {
        dispatch([this] {
            bool succeeded = false;
            try { succeeded = clearDiskCacheNow(); } catch (...) {}
            std::scoped_lock lock(m_buildMutex);
            m_clearState = succeeded ? ThumbnailCacheClearState::succeeded :
                ThumbnailCacheClearState::failed;
        });
    } catch (...) {
        std::scoped_lock lock(m_buildMutex);
        m_clearState = ThumbnailCacheClearState::failed;
        return false;
    }
    return true;
}

ThumbnailCacheClearState NativeThumbnailRuntime::diskCacheClearState() const {
    std::scoped_lock lock(m_buildMutex);
    return m_clearState;
}

void NativeThumbnailRuntime::startDiskCacheBuild(std::vector<std::string> texturePaths) {
    std::scoped_lock lock(m_buildMutex);
    if (m_buildStatus.active || m_clearState == ThumbnailCacheClearState::pending ||
            m_limitState == ThumbnailCacheClearState::pending) return;
    m_clearState = ThumbnailCacheClearState::idle;
    std::unordered_set<std::string> unique;
    m_buildPaths.clear();
    m_buildPaths.reserve(texturePaths.size());
    for (auto& path : texturePaths) {
        if (!path.empty() && unique.insert(
                NativeThumbnailController::canonicalizeTexturePath(path)).second)
            m_buildPaths.push_back(std::move(path));
    }
    ++m_buildGeneration;
    m_buildInFlight = false;
    m_buildStatus = {.total = m_buildPaths.size(), .active = !m_buildPaths.empty()};
}

void NativeThumbnailRuntime::pumpDiskCacheBuild() {
    std::string path;
    std::uint64_t generation{};
    {
        std::scoped_lock lock(m_buildMutex);
        if (!m_buildStatus.active || m_buildInFlight) return;
        path = m_buildPaths[m_buildStatus.completed];
        generation = m_buildGeneration;
        m_buildInFlight = true;
    }
    auto task = [this, path = std::move(path), generation] {
        {
            std::scoped_lock lock(m_buildMutex);
            if (generation != m_buildGeneration) return;
        }
        bool success = false;
        try { success = m_source->prepareDiskCache(path); } catch (...) {}
        std::scoped_lock lock(m_buildMutex);
        if (generation != m_buildGeneration) return;
        ++m_buildStatus.completed;
        if (!success) ++m_buildStatus.failed;
        m_buildInFlight = false;
        if (m_buildStatus.completed == m_buildStatus.total)
            m_buildStatus.active = false;
    };
    try { dispatch(std::move(task)); }
    catch (...) {
        std::scoped_lock lock(m_buildMutex);
        if (generation != m_buildGeneration) return;
        ++m_buildStatus.completed;
        ++m_buildStatus.failed;
        m_buildInFlight = false;
        if (m_buildStatus.completed == m_buildStatus.total)
            m_buildStatus.active = false;
    }
}

ThumbnailCacheBuildStatus NativeThumbnailRuntime::diskCacheBuildStatus() const {
    std::scoped_lock lock(m_buildMutex);
    return m_buildStatus;
}

std::uintmax_t NativeThumbnailRuntime::diskCacheBytes() const {
    return m_source->diskCacheBytes();
}

}  // namespace stui::native
