#include "native/NativeThumbnailController.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace stui::native {
namespace {

constexpr std::size_t kMaximumVisiblePaths = 96;
constexpr std::size_t kMaximumLegacyPagePaths = 6;

bool sameRequest(
    const NativeThumbnailRequest& left,
    const NativeThumbnailRequest& right) {
    return left.generation == right.generation && left.texturePath == right.texturePath;
}

std::optional<NativeThumbnailStatus> negativeStatus(NativeThumbnailFailure failure) {
    switch (failure) {
    case NativeThumbnailFailure::missing:
        return NativeThumbnailStatus::missing;
    case NativeThumbnailFailure::broken:
        return NativeThumbnailStatus::broken;
    case NativeThumbnailFailure::deviceUnavailable:
    case NativeThumbnailFailure::cancelled:
        return std::nullopt;
    }

    return std::nullopt;
}

}  // namespace

void NativeThumbnailController::synchronize(
    repository::TattooCatalogSnapshot snapshot,
    std::span<const repository::TattooDefinition> entries,
    const NativeThumbnailCacheLookup& lookup) {
    std::vector<std::string> texturePaths;
    texturePaths.reserve(kMaximumLegacyPagePaths);
    for (const auto& entry : entries) {
        if (texturePaths.size() == kMaximumLegacyPagePaths) {
            break;
        }
        const auto texturePath = canonicalizeTexturePath(entry.texturePath);
        if (std::ranges::find(texturePaths, texturePath) == texturePaths.end()) {
            texturePaths.push_back(texturePath);
        }
    }

    synchronize(std::move(snapshot), texturePaths, lookup);
}

void NativeThumbnailController::synchronize(
    NativeThumbnailEpoch epoch,
    std::span<const std::string> visibleTexturePaths,
    const NativeThumbnailCacheLookup& lookup) {
    std::vector<std::string> texturePaths;
    texturePaths.reserve(kMaximumVisiblePaths);
    for (const auto& path : visibleTexturePaths) {
        if (texturePaths.size() == kMaximumVisiblePaths) {
            break;
        }
        const auto texturePath = canonicalizeTexturePath(path);
        if (std::ranges::find(texturePaths, texturePath) == texturePaths.end()) {
            texturePaths.push_back(texturePath);
        }
    }

    std::uint64_t synchronizationRevision{};
    {
        std::scoped_lock lock(m_mutex);
        const bool samePage = m_views.size() == texturePaths.size() &&
            std::ranges::equal(m_views, texturePaths, {},
                [](const NativeThumbnailView& view) -> const std::string& {
                    return view.texturePath;
                });
        if (m_epoch == epoch && samePage) {
            if (m_lookupPending && (m_pendingEpoch != epoch ||
                    m_pendingPaths != texturePaths)) {
                ++m_synchronizationRevision;
                m_lookupPending = false;
                m_pendingEpoch.reset();
                m_pendingPaths.clear();
            }
            return;
        }
        if (m_lookupPending && m_pendingEpoch == epoch &&
            m_pendingPaths == texturePaths) {
            return;
        }

        synchronizationRevision = ++m_synchronizationRevision;
        m_lookupPending = true;
        m_pendingEpoch = epoch;
        m_pendingPaths = texturePaths;
    }

    std::vector<std::shared_ptr<textures::D3D11Texture>> cachedTextures;
    cachedTextures.reserve(texturePaths.size());
    try {
        for (const auto& texturePath : texturePaths) {
            cachedTextures.push_back(lookup ? lookup(texturePath) : nullptr);
        }
    } catch (...) {
        std::scoped_lock lock(m_mutex);
        if (synchronizationRevision == m_synchronizationRevision) {
            m_lookupPending = false;
            m_pendingEpoch.reset();
            m_pendingPaths.clear();
        }
        throw;
    }

    std::scoped_lock lock(m_mutex);
    if (synchronizationRevision != m_synchronizationRevision) {
        return;
    }
    m_lookupPending = false;
    m_pendingEpoch.reset();
    m_pendingPaths.clear();

    const bool sameEpoch = m_epoch == epoch;
    if (!sameEpoch) {
        m_epoch = std::move(epoch);
        m_negativeFailures.clear();
        m_deviceUnavailablePaths.clear();
    }

    ++m_generation;
    auto previousViews = std::move(m_views);
    m_views.clear();
    m_views.reserve(texturePaths.size());
    m_queuedPaths.clear();

    for (std::size_t index = 0; index < texturePaths.size(); ++index) {
        const auto& texturePath = texturePaths[index];
        NativeThumbnailView view{.texturePath = texturePath};
        bool shouldQueue = false;
        if (const auto failure = m_negativeFailures.find(texturePath);
            failure != m_negativeFailures.end()) {
            view.status = *negativeStatus(failure->second);
        } else if (cachedTextures[index]) {
            view.status = NativeThumbnailStatus::ready;
            view.texture = std::move(cachedTextures[index]);
        } else if (sameEpoch) {
            const auto previous = std::ranges::find(previousViews, texturePath,
                &NativeThumbnailView::texturePath);
            if (previous != previousViews.end() &&
                previous->status == NativeThumbnailStatus::ready && previous->texture) {
                view.status = NativeThumbnailStatus::ready;
                view.texture = previous->texture;
            } else {
                shouldQueue = true;
            }
        } else {
            shouldQueue = true;
        }
        m_views.push_back(std::move(view));
        if (shouldQueue) {
            queueIfEligibleLocked(texturePath);
        }
    }
}

std::optional<NativeThumbnailRequest> NativeThumbnailController::takeNextRequest() {
    std::scoped_lock lock(m_mutex);
    if (m_inFlight || m_queuedPaths.empty()) {
        return std::nullopt;
    }

    NativeThumbnailRequest request{
        .generation = m_generation,
        .texturePath = std::move(m_queuedPaths.front()),
    };
    m_queuedPaths.erase(m_queuedPaths.begin());
    m_inFlight = request;

    const auto view = std::ranges::find(m_views, request.texturePath,
        &NativeThumbnailView::texturePath);
    if (view != m_views.end()) {
        view->status = NativeThumbnailStatus::loading;
    }
    return request;
}

bool NativeThumbnailController::isCurrent(const NativeThumbnailRequest& request) const {
    std::scoped_lock lock(m_mutex);
    NativeThumbnailRequest canonicalRequest = request;
    canonicalRequest.texturePath = canonicalizeTexturePath(request.texturePath);
    return m_inFlight && sameRequest(*m_inFlight, canonicalRequest) &&
        canonicalRequest.generation == m_generation;
}

void NativeThumbnailController::complete(
    NativeThumbnailRequest request,
    NativeThumbnailLoadResult result) {
    request.texturePath = canonicalizeTexturePath(request.texturePath);
    std::scoped_lock lock(m_mutex);
    if (!m_inFlight || !sameRequest(*m_inFlight, request)) {
        return;
    }
    m_inFlight.reset();

    if (request.generation != m_generation) {
        const auto currentView = std::ranges::find(m_views, request.texturePath,
            &NativeThumbnailView::texturePath);
        if (currentView != m_views.end()) {
            queueIfEligibleLocked(request.texturePath);
        }
        return;
    }

    const auto view = std::ranges::find(m_views, request.texturePath,
        &NativeThumbnailView::texturePath);
    if (view == m_views.end()) {
        return;
    }

    if (result) {
        view->status = NativeThumbnailStatus::ready;
        view->texture = std::move(*result);
        return;
    }

    switch (result.error()) {
    case NativeThumbnailFailure::missing:
    case NativeThumbnailFailure::broken:
        m_negativeFailures.insert_or_assign(request.texturePath, result.error());
        view->status = *negativeStatus(result.error());
        view->texture.reset();
        return;
    case NativeThumbnailFailure::deviceUnavailable:
        m_deviceUnavailablePaths.insert(request.texturePath);
        view->status = NativeThumbnailStatus::placeholder;
        view->texture.reset();
        return;
    case NativeThumbnailFailure::cancelled:
        view->status = NativeThumbnailStatus::placeholder;
        view->texture.reset();
        queueIfEligibleLocked(request.texturePath);
        return;
    }
}

void NativeThumbnailController::retryDeviceUnavailable() {
    std::scoped_lock lock(m_mutex);
    auto unavailablePaths = std::move(m_deviceUnavailablePaths);
    m_deviceUnavailablePaths.clear();
    for (const auto& view : m_views) {
        if (unavailablePaths.contains(view.texturePath)) {
            queueIfEligibleLocked(view.texturePath);
        }
    }
}

std::vector<NativeThumbnailView> NativeThumbnailController::views() const {
    std::scoped_lock lock(m_mutex);
    return m_views;
}

void NativeThumbnailController::clear() {
    std::scoped_lock lock(m_mutex);
    m_epoch.reset();
    ++m_generation;
    ++m_synchronizationRevision;
    m_lookupPending = false;
    m_pendingEpoch.reset();
    m_pendingPaths.clear();
    m_views.clear();
    m_queuedPaths.clear();
    m_negativeFailures.clear();
    m_deviceUnavailablePaths.clear();
}

bool NativeThumbnailController::isQueuedLocked(std::string_view texturePath) const {
    const auto canonicalPath = canonicalizeTexturePath(texturePath);
    return std::ranges::find(m_queuedPaths, canonicalPath) != m_queuedPaths.end();
}

bool NativeThumbnailController::isVisibleLocked(std::string_view texturePath) const {
    const auto canonicalPath = canonicalizeTexturePath(texturePath);
    return std::ranges::find(m_views, canonicalPath,
        &NativeThumbnailView::texturePath) != m_views.end();
}

void NativeThumbnailController::queueIfEligibleLocked(std::string_view texturePath) {
    const auto canonicalPath = canonicalizeTexturePath(texturePath);
    const auto view = std::ranges::find(m_views, canonicalPath,
        &NativeThumbnailView::texturePath);
    if (view == m_views.end() || view->status != NativeThumbnailStatus::placeholder ||
        m_negativeFailures.contains(canonicalPath) ||
        m_deviceUnavailablePaths.contains(canonicalPath) ||
        isQueuedLocked(canonicalPath) ||
        (m_inFlight && m_inFlight->texturePath == canonicalPath)) {
        return;
    }
    m_queuedPaths.push_back(canonicalPath);
}

std::string NativeThumbnailController::canonicalizeTexturePath(std::string_view texturePath) {
    std::string canonicalPath(texturePath);
    for (char& character : canonicalPath) {
        if (character == '\\') {
            character = '/';
        } else if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return canonicalPath;
}

}  // namespace stui::native
