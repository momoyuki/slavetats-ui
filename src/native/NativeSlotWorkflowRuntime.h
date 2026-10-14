#pragma once

#include "native/NativeSlotWorkflowModel.h"

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
using SlotQueryOperation = std::function<core::TattooSlotsResult(
    std::uint32_t actorFormId,
    core::TattooArea area)>;
using SlotApplyOperation = std::function<core::ApplyTattooResult(
    const core::ApplyTattooRequest& request)>;
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
        TattooLoadoutRetryOperation tattooLoadoutRetryOperation = {});

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
        TattooLoadoutRetryOperation tattooLoadoutRetryOperation = {});

    void pump();

private:
    void scheduleActorTarget(ActorTargetResolutionTicket ticket);
    void scheduleNearbyActorScan(NearbyActorScanTicket ticket);
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

    NativeSlotWorkflowModel& m_model;
    ActorTargetOperation m_resolveActorTarget;
    NearbyActorScanOperation m_scanNearbyActors;
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
