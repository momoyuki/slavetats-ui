#include "runtime/TattooLoadoutOrchestration.h"

#include <utility>

namespace stui::runtime {
namespace {

core::TattooLoadoutRestoreResultValue result(
    const std::uint32_t actorFormId,
    const core::TattooLoadoutRestoreState state) {
    return {.actorFormId = actorFormId, .state = state};
}

core::TattooLoadoutRestoreResultValue incompleteRollback(
    const std::uint32_t actorFormId,
    core::TattooLoadoutVerification verification) {
    auto value = result(actorFormId, core::TattooLoadoutRestoreState::rollbackIncomplete);
    value.affectedSlots = std::move(verification.affectedSlots);
    return value;
}

core::RestoreTattooLoadoutResult rollback(
    const core::TattooLoadoutPlan& plan,
    const core::TattooLoadoutRollbackSnapshot& snapshot,
    ITattooLoadoutBackend& backend,
    TattooLoadoutRetryRegistry& retries) {
    if (!backend.applyRollback(snapshot)) {
        return incompleteRollback(plan.actorFormId, backend.verifyRollback(snapshot));
    }
    if (!backend.synchronize(plan.actorFormId)) {
        auto pending = result(
            plan.actorFormId,
            core::TattooLoadoutRestoreState::rollbackSynchronizationPending);
        pending.synchronizationRetryAvailable = true;
        pending.retryId = retries.store({
            .actorFormId = plan.actorFormId,
            .phase = TattooLoadoutRetryPhase::rollbackSync,
            .forwardPlan = plan,
            .rollbackSnapshot = snapshot,
        });
        return pending;
    }
    auto verification = backend.verifyRollback(snapshot);
    if (!verification.matches) {
        return incompleteRollback(plan.actorFormId, std::move(verification));
    }
    return result(plan.actorFormId, core::TattooLoadoutRestoreState::rollbackComplete);
}

core::ServiceError invalidRetryError() {
    return {
        .code = core::ServiceErrorCode::synchronizeFailed,
        .message = "Tattoo loadout synchronization retry is stale or belongs to another Actor",
    };
}

}  // namespace

core::TattooLoadoutRetryId TattooLoadoutRetryRegistry::store(
    TattooLoadoutRetryContext context) {
    const auto id = m_nextId++;
    context.id = id;
    m_contexts.insert_or_assign(id, std::move(context));
    return id;
}

std::optional<TattooLoadoutRetryContext> TattooLoadoutRetryRegistry::take(
    const core::TattooLoadoutRetryId id,
    const std::uint32_t actorFormId) {
    const auto found = m_contexts.find(id);
    if (found == m_contexts.end()) return std::nullopt;
    if (found->second.actorFormId != actorFormId) {
        m_contexts.erase(found);
        return std::nullopt;
    }
    auto context = std::move(found->second);
    m_contexts.erase(found);
    return context;
}

void TattooLoadoutRetryRegistry::erase(const core::TattooLoadoutRetryId id) noexcept {
    m_contexts.erase(id);
}

core::RestoreTattooLoadoutResult executeTattooLoadout(
    const core::RestoreTattooLoadoutRequest& request,
    ITattooLoadoutBackend& backend,
    TattooLoadoutRetryRegistry& retries) {
    auto planned = backend.preflight({
        .actorFormId = request.actorFormId,
        .loadout = request.loadout,
    });
    if (!planned) {
        auto rejected = result(
            request.actorFormId, core::TattooLoadoutRestoreState::preflightRejected);
        rejected.blockers = std::move(planned.error());
        return rejected;
    }

    if (request.approvedPlan && *request.approvedPlan != *planned) {
        auto rejected = result(request.actorFormId, core::TattooLoadoutRestoreState::preflightRejected);
        rejected.blockers.push_back({.message = "Actor or tattoo state changed since preview. Run Preflight again."});
        return rejected;
    }
    auto snapshot = backend.captureRollback(*planned);
    if (!snapshot) return std::unexpected(std::move(snapshot.error()));

    for (const auto& action : planned->actions) {
        if (!backend.applyAction(action)) {
            return rollback(*planned, *snapshot, backend, retries);
        }
    }
    if (!backend.synchronize(request.actorFormId)) {
        auto pending = result(request.actorFormId,
            core::TattooLoadoutRestoreState::forwardSynchronizationPending);
        pending.synchronizationRetryAvailable = true;
        pending.retryId = retries.store({
            .actorFormId = request.actorFormId,
            .phase = TattooLoadoutRetryPhase::forwardSync,
            .forwardPlan = *planned,
            .rollbackSnapshot = *snapshot,
        });
        return pending;
    }

    const auto verification = backend.verifyForward(*planned);
    if (!verification.matches) {
        return rollback(*planned, *snapshot, backend, retries);
    }
    return result(request.actorFormId, core::TattooLoadoutRestoreState::success);
}

core::RestoreTattooLoadoutResult retryTattooLoadoutSynchronization(
    const core::RetryTattooLoadoutSynchronizationRequest& request,
    ITattooLoadoutBackend& backend,
    TattooLoadoutRetryRegistry& retries) {
    auto context = retries.take(request.retryId, request.actorFormId);
    if (!context) return std::unexpected(invalidRetryError());

    if (!backend.synchronize(request.actorFormId)) {
        const auto phase = context->phase;
        const auto id = retries.store(std::move(*context));
        auto pending = result(request.actorFormId,
            phase == TattooLoadoutRetryPhase::forwardSync ?
                core::TattooLoadoutRestoreState::forwardSynchronizationPending :
                core::TattooLoadoutRestoreState::rollbackSynchronizationPending);
        pending.synchronizationRetryAvailable = true;
        pending.retryId = id;
        return pending;
    }

    if (context->phase == TattooLoadoutRetryPhase::forwardSync) {
        const auto verification = backend.verifyForward(context->forwardPlan);
        if (!verification.matches) {
            return rollback(context->forwardPlan, context->rollbackSnapshot, backend, retries);
        }
        return result(request.actorFormId, core::TattooLoadoutRestoreState::success);
    }

    auto verification = backend.verifyRollback(context->rollbackSnapshot);
    if (!verification.matches) {
        return incompleteRollback(request.actorFormId, std::move(verification));
    }
    return result(request.actorFormId, core::TattooLoadoutRestoreState::rollbackComplete);
}

}  // namespace stui::runtime
