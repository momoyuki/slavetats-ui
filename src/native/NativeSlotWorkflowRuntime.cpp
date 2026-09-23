#include "native/NativeSlotWorkflowRuntime.h"

#include <expected>
#include <utility>

namespace stui::native {
namespace {

core::ServiceError operationError(core::ServiceErrorCode code, const char* message) {
    return core::ServiceError{.code = code, .message = message};
}

core::ServiceError appearanceOperationError(
    core::ServiceErrorCode code,
    const char* message,
    core::UpdateTattooAppearanceMode mode) {
    return core::ServiceError{
        .code = code,
        .message = message,
        .mutationSideEffect = mode == core::UpdateTattooAppearanceMode::updateAndSynchronize
            ? core::MutationSideEffect::mayHaveOccurred
            : core::MutationSideEffect::none,
    };
}

core::ServiceErrorCode appearanceErrorCode(
    core::UpdateTattooAppearanceMode mode) noexcept {
    return mode == core::UpdateTattooAppearanceMode::synchronizeOnly
        ? core::ServiceErrorCode::synchronizeFailed
        : core::ServiceErrorCode::updateFailed;
}

class InFlightGuard {
public:
    explicit InFlightGuard(std::atomic_bool& inFlight) noexcept : m_inFlight(inFlight) {}
    ~InFlightGuard() {
        m_inFlight.store(false);
    }

    InFlightGuard(const InFlightGuard&) = delete;
    InFlightGuard& operator=(const InFlightGuard&) = delete;

private:
    std::atomic_bool& m_inFlight;
};

}  // namespace

NativeSlotWorkflowRuntime::NativeSlotWorkflowRuntime(
    NativeSlotWorkflowModel& model,
    ActorTargetOperation resolveActorTarget,
    SlotQueryOperation query,
    SlotApplyOperation apply,
    SlotRemoveOperation remove,
    SlotAppearanceOperation updateAppearance,
    SlotLockOperation setLocked,
    NativeSlotScheduler scheduler,
    LivePreviewClock livePreviewClock)
    : m_model(model),
      m_resolveActorTarget(std::move(resolveActorTarget)),
      m_query(std::move(query)),
      m_apply(std::move(apply)),
      m_remove(std::move(remove)),
      m_updateAppearance(std::move(updateAppearance)),
      m_setLocked(std::move(setLocked)),
      m_scheduler(std::move(scheduler)),
      m_livePreviewClock(std::move(livePreviewClock)) {}

void NativeSlotWorkflowRuntime::pump() {
    m_model.advanceLivePreview(m_livePreviewClock());

    bool expected = false;
    if (!m_inFlight.compare_exchange_strong(expected, true)) {
        return;
    }

    if (auto target = m_model.takeActorTargetRequest()) {
        scheduleActorTarget(*target);
        return;
    }
    if (auto query = m_model.takeSlotQuery()) {
        scheduleQuery(std::move(*query));
        return;
    }
    if (auto apply = m_model.takeApplyRequest()) {
        scheduleApply(std::move(*apply));
        return;
    }
    if (auto remove = m_model.takeRemoveRequest()) {
        scheduleRemove(std::move(*remove));
        return;
    }
    if (auto appearance = m_model.takeAppearanceRequest()) {
        scheduleAppearance(std::move(*appearance));
        return;
    }
    if (auto lock = m_model.takeLockRequest()) {
        scheduleLock(std::move(*lock));
        return;
    }

    m_inFlight.store(false);
}

void NativeSlotWorkflowRuntime::scheduleActorTarget(ActorTargetResolutionTicket ticket) {
    NativeSlotTask task = [this, ticket] {
        InFlightGuard guard(m_inFlight);
        ActorTargetResult result = std::unexpected(operationError(
            core::ServiceErrorCode::actorNotFound,
            "Failed to resolve crosshair Actor."));
        try {
            result = m_resolveActorTarget();
        } catch (...) {
        }
        m_model.completeActorTargetResolution(ticket.generation, std::move(result));
    };

    try {
        m_scheduler(std::move(task));
    } catch (...) {
        m_model.completeActorTargetResolution(
            ticket.generation,
            std::unexpected(operationError(
                core::ServiceErrorCode::actorNotFound,
                "Failed to resolve crosshair Actor.")));
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleQuery(SlotQueryTicket ticket) {
    NativeSlotTask task = [this, ticket] {
        InFlightGuard guard(m_inFlight);
        core::TattooSlotsResult result = std::unexpected(operationError(
            core::ServiceErrorCode::slotQueryFailed,
            "Slot query failed."));
        try {
            result = m_query(ticket.actorFormId, ticket.area);
        } catch (...) {
        }
        m_model.completeSlotQuery(ticket.generation, std::move(result));
    };

    try {
        m_scheduler(std::move(task));
    } catch (...) {
        m_model.completeSlotQuery(
            ticket.generation,
            std::unexpected(operationError(
                core::ServiceErrorCode::slotQueryFailed,
                "Failed to schedule slot query.")));
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleApply(SlotApplyTicket ticket) {
    const std::uint64_t generation = ticket.generation;
    NativeSlotTask task = [this, ticket = std::move(ticket)] {
        InFlightGuard guard(m_inFlight);
        core::ApplyTattooResult result = std::unexpected(operationError(
            core::ServiceErrorCode::applyFailed,
            "Tattoo apply failed."));
        try {
            result = m_apply(ticket.request);
        } catch (...) {
        }
        m_model.completeApply(ticket.generation, std::move(result));
    };

    try {
        m_scheduler(std::move(task));
    } catch (...) {
        m_model.completeApply(
            generation,
            std::unexpected(operationError(
                core::ServiceErrorCode::applyFailed,
                "Failed to schedule tattoo apply.")));
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleRemove(SlotRemoveTicket ticket) {
    const std::uint64_t generation = ticket.generation;
    NativeSlotTask task = [this, ticket = std::move(ticket)] {
        InFlightGuard guard(m_inFlight);
        core::RemoveTattooResult result = std::unexpected(operationError(
            core::ServiceErrorCode::removeFailed,
            "Tattoo remove failed."));
        try {
            result = m_remove(ticket.request);
        } catch (...) {
        }
        m_model.completeRemove(ticket.generation, std::move(result));
    };

    try {
        m_scheduler(std::move(task));
    } catch (...) {
        m_model.completeRemove(
            generation,
            std::unexpected(operationError(
                core::ServiceErrorCode::removeFailed,
                "Failed to schedule tattoo remove.")));
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleAppearance(SlotAppearanceTicket ticket) {
    const std::uint64_t generation = ticket.generation;
    const auto errorCode = appearanceErrorCode(ticket.request.mode);
    const auto mode = ticket.request.mode;
    NativeSlotTask task = [this, ticket = std::move(ticket), errorCode, mode] {
        InFlightGuard guard(m_inFlight);
        core::UpdateTattooAppearanceResult result = std::unexpected(appearanceOperationError(
            errorCode,
            "Tattoo appearance update failed.",
            mode));
        try {
            result = m_updateAppearance(ticket.request);
        } catch (...) {
        }
        m_model.completeAppearanceUpdate(ticket.generation, std::move(result));
    };

    try {
        m_scheduler(std::move(task));
    } catch (...) {
        m_model.completeAppearanceUpdate(
            generation,
            std::unexpected(operationError(
                errorCode,
                "Failed to schedule tattoo appearance update.")));
        m_inFlight.store(false);
    }
}

void NativeSlotWorkflowRuntime::scheduleLock(SlotLockTicket ticket) {
    const std::uint64_t generation = ticket.generation;
    NativeSlotTask task = [this, ticket = std::move(ticket)] {
        InFlightGuard guard(m_inFlight);
        core::SetTattooLockedResult result = std::unexpected(operationError(
            core::ServiceErrorCode::lockFailed,
            "Tattoo lock state update failed."));
        try {
            result = m_setLocked(ticket.request);
        } catch (...) {
        }
        m_model.completeLockStateChange(ticket.generation, std::move(result));
    };
    try {
        m_scheduler(std::move(task));
    } catch (...) {
        m_model.completeLockStateChange(
            generation,
            std::unexpected(operationError(
                core::ServiceErrorCode::lockFailed,
                "Failed to schedule tattoo lock state update.")));
        m_inFlight.store(false);
    }
}

}  // namespace stui::native
