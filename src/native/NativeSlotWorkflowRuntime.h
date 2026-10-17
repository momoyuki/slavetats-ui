#pragma once

#include "native/NativeSlotWorkflowModel.h"
#include "native/CatalogPreviewModel.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace stui::native {

using NativeSlotTask = std::function<void()>;
using NativeSlotScheduler = std::function<void(NativeSlotTask)>;
using LivePreviewClock = std::function<std::chrono::steady_clock::time_point()>;
using ActorTargetOperation = std::function<ActorTargetResult()>;
using NearbyActorScanOperation = std::function<NearbyActorScanResult()>;
using LoadedActorScanOperation = std::function<LoadedActorScanResult()>;
using SlotQueryOperation = std::function<core::TattooSlotsResult(
    std::uint32_t actorFormId,
    core::TattooArea area)>;
using SlotApplyOperation = std::function<core::ApplyTattooResult(
    const core::ApplyTattooRequest& request)>;
using CatalogPreviewBeginOperation = std::function<core::CatalogPreviewBeginResult(
    const core::CatalogPreviewBeginRequest&)>;
using CatalogPreviewPaintOperation = std::function<core::CatalogPreviewPaintResult(
    const core::CatalogPreviewPaintRequest&)>;
using CatalogPreviewRestoreOperation = std::function<core::CatalogPreviewRestoreResult(
    const core::CatalogPreviewRestoreRequest&)>;
using CatalogPreviewCommitOperation = std::function<core::CatalogPreviewCommitResult(
    const core::CatalogPreviewCommitRequest&)>;
using SlotRemoveOperation = std::function<core::RemoveTattooResult(
    const core::RemoveTattooRequest& request)>;
using SlotAppearanceOperation = std::function<core::UpdateTattooAppearanceResult(
    const core::UpdateTattooAppearanceRequest& request)>;
using SlotLockOperation = std::function<core::SetTattooLockedResult(
    const core::SetTattooLockedRequest& request)>;
using FavoriteOperation = std::function<runtime::FavoriteResult(
    const repository::FavoriteIdentity& identity,
    bool enabled)>;
using RecentTattooLoadOperation = std::function<runtime::RecentTattooResult()>;
using RecentTattooRecordOperation = std::function<runtime::RecentTattooResult(
    const repository::RecentTattooIdentity& identity)>;
using AppearancePresetOperation = std::function<runtime::AppearancePresetResult(
    const AppearancePresetTicket& ticket)>;
using TattooLoadoutStoreOperation = std::function<runtime::TattooLoadoutResult(
    const TattooLoadoutStoreTicket& ticket)>;
using TattooLoadoutCaptureOperation = std::function<core::CaptureTattooLoadoutResult(
    const core::CaptureTattooLoadoutRequest& request)>;
using TattooLoadoutPreviewOperation = std::function<core::PreviewTattooLoadoutResult(
    const core::PreviewTattooLoadoutRequest& request)>;
using TattooLoadoutRestoreOperation = std::function<core::RestoreTattooLoadoutResult(
    const core::RestoreTattooLoadoutRequest& request)>;
using TattooLoadoutRetryOperation = std::function<core::RestoreTattooLoadoutResult(
    const core::RetryTattooLoadoutSynchronizationRequest& request)>;

struct NearbyActorRuntimeTag {};

class NativeSlotWorkflowRuntime {
public:
    NativeSlotWorkflowRuntime(
        NativeSlotWorkflowModel& model,
        ActorTargetOperation resolveActorTarget,
        SlotQueryOperation query,
        SlotApplyOperation apply,
        SlotRemoveOperation remove,
        SlotAppearanceOperation updateAppearance,
        SlotLockOperation setLocked,
        NativeSlotScheduler scheduler,
        LivePreviewClock livePreviewClock,
        FavoriteOperation favoriteOperation = {},
        RecentTattooLoadOperation recentTattooLoadOperation = {},
        RecentTattooRecordOperation recentTattooRecordOperation = {},
        AppearancePresetOperation appearancePresetOperation = {},
        TattooLoadoutStoreOperation tattooLoadoutStoreOperation = {},
        TattooLoadoutCaptureOperation tattooLoadoutCaptureOperation = {},
        TattooLoadoutPreviewOperation tattooLoadoutPreviewOperation = {},
        TattooLoadoutRestoreOperation tattooLoadoutRestoreOperation = {},
        TattooLoadoutRetryOperation tattooLoadoutRetryOperation = {},
        LoadedActorScanOperation loadedActorScanOperation = {},
        CatalogPreviewBeginOperation catalogPreviewBegin = {},
        CatalogPreviewPaintOperation catalogPreviewPaint = {},
        CatalogPreviewRestoreOperation catalogPreviewRestore = {},
        CatalogPreviewCommitOperation catalogPreviewCommit = {});

    NativeSlotWorkflowRuntime(
        NativeSlotWorkflowModel& model,
        NearbyActorRuntimeTag,
        NearbyActorScanOperation scanNearbyActors,
        SlotQueryOperation query,
        SlotApplyOperation apply,
        SlotRemoveOperation remove,
        SlotAppearanceOperation updateAppearance,
        SlotLockOperation setLocked,
        NativeSlotScheduler scheduler,
        LivePreviewClock livePreviewClock,
        FavoriteOperation favoriteOperation = {},
        RecentTattooLoadOperation recentTattooLoadOperation = {},
        RecentTattooRecordOperation recentTattooRecordOperation = {},
        AppearancePresetOperation appearancePresetOperation = {},
        TattooLoadoutStoreOperation tattooLoadoutStoreOperation = {},
        TattooLoadoutCaptureOperation tattooLoadoutCaptureOperation = {},
        TattooLoadoutPreviewOperation tattooLoadoutPreviewOperation = {},
        TattooLoadoutRestoreOperation tattooLoadoutRestoreOperation = {},
        TattooLoadoutRetryOperation tattooLoadoutRetryOperation = {},
        LoadedActorScanOperation loadedActorScanOperation = {},
        CatalogPreviewBeginOperation catalogPreviewBegin = {},
        CatalogPreviewPaintOperation catalogPreviewPaint = {},
        CatalogPreviewRestoreOperation catalogPreviewRestore = {},
        CatalogPreviewCommitOperation catalogPreviewCommit = {});

    void pump();
    [[nodiscard]] bool catalogPreviewActive() const noexcept;
    [[nodiscard]] bool catalogPreviewStarting() const noexcept;
    [[nodiscard]] const core::ServiceError* catalogPreviewError() const noexcept;
    [[nodiscard]] std::optional<core::TattooEntry> catalogPreviewSelection() const;
    [[nodiscard]] std::optional<core::TattooEntry> catalogPreviewDisplay() const;
    [[nodiscard]] bool catalogPreviewIsPinned(
        const repository::TattooDefinition& tattoo) const;
    void hoverCatalogPreview(std::optional<repository::TattooDefinition> tattoo);
    void pinCatalogPreview(const repository::TattooDefinition& tattoo);
    void setCatalogPreviewInk(std::int32_t color);
    void setCatalogPreviewGlow(std::int32_t color);
    void setCatalogPreviewGlossy(bool enabled);
    void undoCatalogPreview();
    [[nodiscard]] bool saveCatalogPreview();
    [[nodiscard]] bool closeCatalogPreview(bool closeMenu);
    void retryCatalogPreview();
    void invalidateCatalogPreview() noexcept;

private:
    void scheduleActorTarget(ActorTargetResolutionTicket ticket);
    void scheduleNearbyActorScan(NearbyActorScanTicket ticket);
    void scheduleLoadedActorScan(LoadedActorScanTicket ticket);
    void scheduleQuery(SlotQueryTicket ticket);
    void scheduleApply(SlotApplyTicket ticket);
    void scheduleRemove(SlotRemoveTicket ticket);
    void scheduleAppearance(SlotAppearanceTicket ticket);
    void scheduleLock(SlotLockTicket ticket);
    void scheduleFavorite(FavoriteTicket ticket);
    void scheduleRecentTattoo(RecentTattooTicket ticket);
    void scheduleAppearancePreset(AppearancePresetTicket ticket);
    void scheduleTattooLoadoutStore(TattooLoadoutStoreTicket ticket);
    void scheduleTattooLoadoutActor(TattooLoadoutActorTicket ticket);
    void drainFavoriteCompletions();
    void drainRecentTattooCompletions();
    void drainAppearancePresetCompletions();
    void drainTattooLoadoutCompletions();
    void scheduleCatalogPreviewBegin(core::CatalogPreviewContext context);
    void scheduleCatalogPreviewPaint(CatalogPreviewPaint paint);
    void scheduleCatalogPreviewCommit(CatalogPreviewCommit commit);

    NativeSlotWorkflowModel& m_model;
    ActorTargetOperation m_resolveActorTarget;
    NearbyActorScanOperation m_scanNearbyActors;
    LoadedActorScanOperation m_scanLoadedActors;
    SlotQueryOperation m_query;
    SlotApplyOperation m_apply;
    SlotRemoveOperation m_remove;
    SlotAppearanceOperation m_updateAppearance;
    SlotLockOperation m_setLocked;
    NativeSlotScheduler m_scheduler;
    LivePreviewClock m_livePreviewClock;
    FavoriteOperation m_favoriteOperation;
    RecentTattooLoadOperation m_recentTattooLoadOperation;
    RecentTattooRecordOperation m_recentTattooRecordOperation;
    AppearancePresetOperation m_appearancePresetOperation;
    TattooLoadoutStoreOperation m_tattooLoadoutStoreOperation;
    TattooLoadoutCaptureOperation m_tattooLoadoutCaptureOperation;
    TattooLoadoutPreviewOperation m_tattooLoadoutPreviewOperation;
    TattooLoadoutRestoreOperation m_tattooLoadoutRestoreOperation;
    TattooLoadoutRetryOperation m_tattooLoadoutRetryOperation;
    CatalogPreviewBeginOperation m_catalogPreviewBegin;
    CatalogPreviewPaintOperation m_catalogPreviewPaint;
    CatalogPreviewRestoreOperation m_catalogPreviewRestore;
    CatalogPreviewCommitOperation m_catalogPreviewCommit;
    CatalogPreviewModel m_catalogPreviewModel;
    std::optional<core::CatalogPreviewContext> m_catalogPreviewAttempt;
    std::optional<core::CatalogPreviewSessionId> m_catalogPreviewSession;
    std::optional<core::ServiceError> m_catalogPreviewError;
    std::optional<repository::RecentTattooIdentity> m_catalogPreviewPinnedIdentity;
    bool m_catalogPreviewBeginPending{};
    bool m_catalogPreviewCancelBegin{};
    bool m_catalogPreviewCloseRequested{};
    bool m_catalogPreviewCloseMenu{};
    std::uint64_t m_catalogPreviewEpoch{};
    std::mutex m_favoriteCompletionMutex;
    std::vector<std::pair<std::uint64_t, runtime::FavoriteResult>> m_favoriteCompletions;
    std::mutex m_recentTattooCompletionMutex;
    std::vector<std::pair<std::uint64_t, runtime::RecentTattooResult>>
        m_recentTattooCompletions;
    std::mutex m_appearancePresetCompletionMutex;
    std::vector<std::pair<std::uint64_t, runtime::AppearancePresetResult>>
        m_appearancePresetCompletions;
    std::mutex m_tattooLoadoutCompletionMutex;
    std::vector<std::pair<std::uint64_t, runtime::TattooLoadoutResult>>
        m_tattooLoadoutStoreCompletions;
    std::vector<std::pair<std::uint64_t, core::CaptureTattooLoadoutResult>>
        m_tattooLoadoutCaptureCompletions;
    std::vector<std::pair<std::uint64_t, core::PreviewTattooLoadoutResult>>
        m_tattooLoadoutPreviewCompletions;
    std::vector<std::pair<std::uint64_t, core::RestoreTattooLoadoutResult>>
        m_tattooLoadoutRestoreCompletions;
    std::atomic_bool m_inFlight{false};
};

}  // namespace stui::native
