#pragma once

#include "repository/TattooCatalogStore.h"
#include "textures/D3D11TextureUploader.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace stui::native {

enum class NativeThumbnailStatus {
    placeholder,
    loading,
    ready,
    missing,
    broken,
};

enum class NativeThumbnailFailure {
    missing,
    broken,
    deviceUnavailable,
    cancelled,
};

struct NativeThumbnailRequest {
    std::uint64_t generation{};
    std::string texturePath;
};

using NativeThumbnailLoadResult = std::expected<
    std::shared_ptr<textures::D3D11Texture>,
    NativeThumbnailFailure>;

struct NativeThumbnailView {
    std::string texturePath;
    NativeThumbnailStatus status{NativeThumbnailStatus::placeholder};
    std::shared_ptr<textures::D3D11Texture> texture;
};

using NativeThumbnailCacheLookup =
    std::function<std::shared_ptr<textures::D3D11Texture>(std::string_view)>;
using NativeThumbnailEpoch = std::shared_ptr<const void>;

class NativeThumbnailController {
public:
    void synchronize(
        NativeThumbnailEpoch epoch,
        std::span<const std::string> texturePaths,
        const NativeThumbnailCacheLookup& lookup);
    void synchronize(
        repository::TattooCatalogSnapshot snapshot,
        std::span<const repository::TattooDefinition> entries,
        const NativeThumbnailCacheLookup& lookup);
    [[nodiscard]] std::optional<NativeThumbnailRequest> takeNextRequest();
    [[nodiscard]] bool isCurrent(const NativeThumbnailRequest& request) const;
    void complete(NativeThumbnailRequest request, NativeThumbnailLoadResult result);
    void retryDeviceUnavailable();
    [[nodiscard]] std::vector<NativeThumbnailView> views() const;
    void clear();
    [[nodiscard]] static std::string canonicalizeTexturePath(std::string_view texturePath);

private:
    [[nodiscard]] bool isQueuedLocked(std::string_view texturePath) const;
    [[nodiscard]] bool isVisibleLocked(std::string_view texturePath) const;
    void queueIfEligibleLocked(std::string_view texturePath);

    mutable std::mutex m_mutex;
    NativeThumbnailEpoch m_epoch;
    std::uint64_t m_generation{};
    std::uint64_t m_synchronizationRevision{};
    NativeThumbnailEpoch m_pendingEpoch;
    std::vector<std::string> m_pendingPaths;
    bool m_lookupPending{};
    std::vector<NativeThumbnailView> m_views;
    std::vector<std::string> m_queuedPaths;
    std::optional<NativeThumbnailRequest> m_inFlight;
    std::unordered_map<std::string, NativeThumbnailFailure> m_negativeFailures;
    std::unordered_set<std::string> m_deviceUnavailablePaths;
};

}  // namespace stui::native
