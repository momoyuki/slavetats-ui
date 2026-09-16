#pragma once

#include "native/NativeSlotWorkflowModel.h"

#include <atomic>
#include <cstdint>
#include <functional>

namespace stui::native {

using NativeSlotTask = std::function<void()>;
using NativeSlotScheduler = std::function<void(NativeSlotTask)>;
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

class NativeSlotWorkflowRuntime {
public:
    NativeSlotWorkflowRuntime(
        NativeSlotWorkflowModel& model,
        SlotQueryOperation query,
        SlotApplyOperation apply,
        SlotRemoveOperation remove,
        SlotAppearanceOperation updateAppearance,
        SlotLockOperation setLocked,
        NativeSlotScheduler scheduler);

    void pump();

private:
    void scheduleQuery(SlotQueryTicket ticket);
    void scheduleApply(SlotApplyTicket ticket);
    void scheduleRemove(SlotRemoveTicket ticket);
    void scheduleAppearance(SlotAppearanceTicket ticket);
    void scheduleLock(SlotLockTicket ticket);

    NativeSlotWorkflowModel& m_model;
    SlotQueryOperation m_query;
    SlotApplyOperation m_apply;
    SlotRemoveOperation m_remove;
    SlotAppearanceOperation m_updateAppearance;
    SlotLockOperation m_setLocked;
    NativeSlotScheduler m_scheduler;
    std::atomic_bool m_inFlight{false};
};

}  // namespace stui::native
