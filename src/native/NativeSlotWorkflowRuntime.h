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
        FavoriteOperation favoriteOperation = {});

    void pump();

private:
    void scheduleActorTarget(ActorTargetResolutionTicket ticket);
    void scheduleQuery(SlotQueryTicket ticket);
    void scheduleApply(SlotApplyTicket ticket);
    void scheduleRemove(SlotRemoveTicket ticket);
    void scheduleAppearance(SlotAppearanceTicket ticket);
    void scheduleLock(SlotLockTicket ticket);
    void scheduleFavorite(FavoriteTicket ticket);
    void drainFavoriteCompletions();

    NativeSlotWorkflowModel& m_model;
    ActorTargetOperation m_resolveActorTarget;
    SlotQueryOperation m_query;
    SlotApplyOperation m_apply;
    SlotRemoveOperation m_remove;
    SlotAppearanceOperation m_updateAppearance;
    SlotLockOperation m_setLocked;
    NativeSlotScheduler m_scheduler;
    LivePreviewClock m_livePreviewClock;
    FavoriteOperation m_favoriteOperation;
    std::mutex m_favoriteCompletionMutex;
    std::vector<std::pair<std::uint64_t, runtime::FavoriteResult>> m_favoriteCompletions;
    std::atomic_bool m_inFlight{false};
};

}  // namespace stui::native
