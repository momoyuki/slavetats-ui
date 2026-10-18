#include "native/NativeThumbnailRuntime.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <unordered_set>
#include <utility>

namespace stui::native {

NativeThumbnailRuntime::NativeThumbnailRuntime(
    std::unique_ptr<NativeThumbnailTextureSource> source,
    NativeThumbnailScheduler scheduler,
    NativeThumbnailNow now)
    : m_source(std::move(source)),
      m_scheduler(std::move(scheduler)),
      m_now(std::move(now)) {}

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
        m_source->pruneExpired(currentTime);
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
            result = m_source->load(
                capturedRequest.texturePath,
                [this, capturedRequest] {
                    return m_controller.isCurrent(capturedRequest);
                },
                m_now());
        } catch (...) {
        }
        complete(std::move(result));
    };

    try {
        m_scheduler(std::move(task));
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

bool NativeThumbnailRuntime::requestDiskCacheClear() {
    {
        std::scoped_lock lock(m_buildMutex);
        if (m_buildStatus.active || m_clearState == ThumbnailCacheClearState::pending)
            return false;
        m_clearState = ThumbnailCacheClearState::pending;
    }
    try {
        m_scheduler([this] {
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
    if (m_buildStatus.active || m_clearState == ThumbnailCacheClearState::pending) return;
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
    try { m_scheduler(std::move(task)); }
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
