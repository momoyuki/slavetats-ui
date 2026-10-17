#pragma once

#include "core/TattooModels.h"
#include "core/TattooLoadoutPlanner.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace stui::core {

struct CaptureTattooLoadoutRequest {
    std::uint32_t actorFormId{};
};

struct PreviewTattooLoadoutRequest {
    std::uint32_t actorFormId{};
    TattooLoadout loadout;
};

struct RestoreTattooLoadoutRequest {
    std::uint32_t actorFormId{};
    TattooLoadout loadout;
    std::optional<TattooLoadoutPlan> approvedPlan;
};

using TattooLoadoutRetryId = std::uint64_t;

struct RetryTattooLoadoutSynchronizationRequest {
    std::uint32_t actorFormId{};
    TattooLoadoutRetryId retryId{};
};

struct TattooLoadoutCaptureValue {
    std::uint32_t actorFormId{};
    std::vector<TattooLoadoutEntry> entries;
};

struct TattooLoadoutPreviewValue {
    std::uint32_t actorFormId{};
    TattooLoadoutPlan plan;
    std::vector<TattooLoadoutBlocker> blockers;
};

struct TattooLoadoutAffectedSlot {
    TattooArea area{TattooArea::body};
    std::int32_t slot{-1};
    std::string message;
};

enum class TattooLoadoutRestoreState {
    success,
    preflightRejected,
    forwardSynchronizationPending,
    rollbackSynchronizationPending,
    rollbackComplete,
    rollbackIncomplete,
};

struct TattooLoadoutRestoreResultValue {
    std::uint32_t actorFormId{};
    TattooLoadoutRestoreState state{TattooLoadoutRestoreState::success};
    std::vector<TattooLoadoutBlocker> blockers;
    std::vector<TattooLoadoutAffectedSlot> affectedSlots;
    bool synchronizationRetryAvailable{};
    std::optional<TattooLoadoutRetryId> retryId;
};

using CaptureTattooLoadoutResult =
    std::expected<TattooLoadoutCaptureValue, ServiceError>;
using PreviewTattooLoadoutResult =
    std::expected<TattooLoadoutPreviewValue, ServiceError>;
using RestoreTattooLoadoutResult =
    std::expected<TattooLoadoutRestoreResultValue, ServiceError>;

class ITattooRuntime {
public:
    virtual ~ITattooRuntime() = default;

    [[nodiscard]] virtual bool apiAvailable() const noexcept = 0;
    [[nodiscard]] virtual bool jContainersReady() const noexcept = 0;
    virtual TattooQueryResult queryAvailable(std::string_view domain) = 0;
    virtual TattooSlotsResult querySlots(std::uint32_t actorFormId, TattooArea area) = 0;
    virtual ApplyTattooResult applyToSlot(const ApplyTattooRequest& request) = 0;
    virtual RemoveTattooResult removeFromSlot(const RemoveTattooRequest& request) = 0;
    virtual UpdateTattooAppearanceResult updateAppearance(
        const UpdateTattooAppearanceRequest& request) = 0;
    virtual SetTattooLockedResult setTattooLocked(
        const SetTattooLockedRequest& request) = 0;
    virtual CatalogPreviewBeginResult beginCatalogPreview(
        const CatalogPreviewBeginRequest&) {
        return std::unexpected(ServiceError{ServiceErrorCode::previewUnavailable,
            "Catalog preview is unavailable"});
    }
    virtual CatalogPreviewPaintResult paintCatalogPreview(
        const CatalogPreviewPaintRequest&) {
        return std::unexpected(ServiceError{ServiceErrorCode::previewUnavailable,
            "Catalog preview is unavailable"});
    }
    virtual CatalogPreviewRestoreResult restoreCatalogPreview(
        const CatalogPreviewRestoreRequest&) {
        return std::unexpected(ServiceError{ServiceErrorCode::previewUnavailable,
            "Catalog preview is unavailable"});
    }
    virtual CatalogPreviewCommitResult commitCatalogPreview(
        const CatalogPreviewCommitRequest&) {
        return std::unexpected(ServiceError{ServiceErrorCode::previewUnavailable,
            "Catalog preview is unavailable"});
    }
    virtual CaptureTattooLoadoutResult captureLoadout(
        const CaptureTattooLoadoutRequest&) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::queryAvailableFailed,
            "Tattoo loadout capture is unavailable",
        });
    }
    virtual PreviewTattooLoadoutResult previewLoadout(
        const PreviewTattooLoadoutRequest&) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::queryAvailableFailed,
            "Tattoo loadout preview is unavailable",
        });
    }
    virtual RestoreTattooLoadoutResult restoreLoadout(
        const RestoreTattooLoadoutRequest&) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::applyFailed,
            "Tattoo loadout restore is unavailable",
        });
    }
    virtual RestoreTattooLoadoutResult retryLoadoutSynchronization(
        const RetryTattooLoadoutSynchronizationRequest&) {
        return std::unexpected(ServiceError{
            ServiceErrorCode::synchronizeFailed,
            "Tattoo loadout synchronization retry is unavailable",
        });
    }
};

}  // namespace stui::core
