#include "runtime/TattooLoadoutOrchestration.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace core = stui::core;
namespace runtime = stui::runtime;

void expect(const bool condition, const std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

core::TattooLoadoutPlan planFor(const std::uint32_t actorFormId = 0x1234) {
    return {
        .actorFormId = actorFormId,
        .actions = {{
            .kind = core::TattooLoadoutActionKind::add,
            .area = core::TattooArea::body,
            .slot = 1,
            .after = core::TattooLoadoutEntry{
                .area = core::TattooArea::body,
                .slot = 1,
                .domain = "default",
                .section = "Marks",
                .name = "Sigil",
            },
        }},
    };
}

core::RestoreTattooLoadoutRequest requestFor() {
    return {
        .actorFormId = 0x1234,
        .loadout = {.name = "Set"},
    };
}

core::TattooLoadoutPlan threeActionPlan() {
    auto plan = planFor();
    plan.actions.push_back(plan.actions.front());
    plan.actions.back().slot = 2;
    plan.actions.back().after->slot = 2;
    plan.actions.push_back(plan.actions.front());
    plan.actions.back().slot = 3;
    plan.actions.back().after->slot = 3;
    return plan;
}

class FakeBackend final : public runtime::ITattooLoadoutBackend {
public:
    core::TattooLoadoutPlanResult preflight(
        const core::PreviewTattooLoadoutRequest& request) override {
        ++preflightCalls;
        preflightActor = request.actorFormId;
        return preflightResult;
    }

    std::expected<core::TattooLoadoutRollbackSnapshot, core::ServiceError>
        captureRollback(const core::TattooLoadoutPlan&) override {
        ++captureCalls;
        if (failCapture) return std::unexpected(error("capture failed"));
        return core::TattooLoadoutRollbackSnapshot{.actorFormId = 0x1234};
    }

    std::expected<void, core::ServiceError> applyAction(
        const core::TattooLoadoutPlanAction&) override {
        const auto index = forwardWrites++;
        if (index == failActionIndex) return std::unexpected(error("write failed"));
        return {};
    }

    bool synchronize(const std::uint32_t actorFormId) override {
        synchronizedActors.push_back(actorFormId);
        ++synchronizeCalls;
        if (synchronizeResults.empty()) return true;
        const bool result = synchronizeResults.front();
        synchronizeResults.erase(synchronizeResults.begin());
        return result;
    }

    core::TattooLoadoutVerification verifyForward(
        const core::TattooLoadoutPlan&) override {
        ++forwardVerifyCalls;
        return {.matches = forwardMatches, .affectedSlots = affected};
    }

    std::expected<void, core::ServiceError> applyRollback(
        const core::TattooLoadoutRollbackSnapshot&) override {
        ++rollbackWrites;
        if (failRollbackWrite) return std::unexpected(error("rollback write failed"));
        return {};
    }

    core::TattooLoadoutVerification verifyRollback(
        const core::TattooLoadoutRollbackSnapshot&) override {
        ++rollbackVerifyCalls;
        return {.matches = rollbackMatches, .affectedSlots = affected};
    }

    static core::ServiceError error(std::string message) {
        return {.code = core::ServiceErrorCode::applyFailed,
            .message = std::move(message),
            .mutationSideEffect = core::MutationSideEffect::mayHaveOccurred};
    }

    core::TattooLoadoutPlanResult preflightResult{planFor()};
    int preflightCalls{};
    std::uint32_t preflightActor{};
    int captureCalls{};
    bool failCapture{};
    int forwardWrites{};
    int failActionIndex{-1};
    int synchronizeCalls{};
    std::vector<bool> synchronizeResults;
    std::vector<std::uint32_t> synchronizedActors;
    int forwardVerifyCalls{};
    bool forwardMatches{true};
    int rollbackWrites{};
    bool failRollbackWrite{};
    int rollbackVerifyCalls{};
    bool rollbackMatches{true};
    std::vector<core::TattooLoadoutAffectedSlot> affected{{
        .area = core::TattooArea::body, .slot = 1, .message = "inspect"}};
};

void successUsesOneSynchronization() {
    FakeBackend backend;
    runtime::TattooLoadoutRetryRegistry retries;

    const auto result = runtime::executeTattooLoadout(requestFor(), backend, retries);

    expect(result && result->state == core::TattooLoadoutRestoreState::success,
        "expected successful restore");
    expect(backend.preflightCalls == 1 && backend.captureCalls == 1 &&
            backend.forwardWrites == 1 && backend.synchronizeCalls == 1 &&
            backend.forwardVerifyCalls == 1 && backend.rollbackWrites == 0,
        "expected deterministic forward sequence with one synchronization");
}

void preflightAndCaptureFailuresDoNotWrite() {
    FakeBackend blocked;
    blocked.preflightResult = std::unexpected(std::vector<core::TattooLoadoutBlocker>{{
        .code = core::TattooLoadoutBlockerCode::externalConflict,
        .area = core::TattooArea::body,
        .slot = 1,
        .message = "blocked",
    }});
    runtime::TattooLoadoutRetryRegistry retries;
    const auto rejected = runtime::executeTattooLoadout(requestFor(), blocked, retries);
    expect(rejected && rejected->state == core::TattooLoadoutRestoreState::preflightRejected &&
            rejected->blockers.size() == 1 && blocked.captureCalls == 0 &&
            blocked.forwardWrites == 0,
        "preflight rejection must not mutate");

    FakeBackend captureFailure;
    captureFailure.failCapture = true;
    const auto failed = runtime::executeTattooLoadout(requestFor(), captureFailure, retries);
    expect(!failed && captureFailure.forwardWrites == 0 && captureFailure.synchronizeCalls == 0,
        "rollback snapshot failure must stop before mutation");
}

void actionAndVerificationFailuresRollback() {
    runtime::TattooLoadoutRetryRegistry retries;
    FakeBackend actionFailure;
    actionFailure.failActionIndex = 0;
    const auto action = runtime::executeTattooLoadout(requestFor(), actionFailure, retries);
    expect(action && action->state == core::TattooLoadoutRestoreState::rollbackComplete &&
            actionFailure.rollbackWrites == 1 && actionFailure.synchronizeCalls == 1 &&
            actionFailure.rollbackVerifyCalls == 1,
        "action failure after possible side effects must roll back and verify");

    FakeBackend verificationFailure;
    verificationFailure.forwardMatches = false;
    const auto verification =
        runtime::executeTattooLoadout(requestFor(), verificationFailure, retries);
    expect(verification &&
            verification->state == core::TattooLoadoutRestoreState::rollbackComplete &&
            verificationFailure.rollbackWrites == 1 &&
            verificationFailure.synchronizeCalls == 2,
        "forward verification failure must run compensating rollback");
}

void failureAfterEveryActionIndexRollsBack() {
    runtime::TattooLoadoutRetryRegistry retries;
    for (int failureIndex = 0; failureIndex < 3; ++failureIndex) {
        FakeBackend backend;
        backend.preflightResult = threeActionPlan();
        backend.failActionIndex = failureIndex;

        const auto result = runtime::executeTattooLoadout(requestFor(), backend, retries);

        expect(result && result->state == core::TattooLoadoutRestoreState::rollbackComplete &&
                backend.forwardWrites == failureIndex + 1 && backend.rollbackWrites == 1,
            "every forward action failure index must trigger one rollback");
    }
}

void forwardSynchronizationRetryDoesNotRepeatWrites() {
    FakeBackend backend;
    backend.synchronizeResults = {false, true};
    runtime::TattooLoadoutRetryRegistry retries;
    const auto first = runtime::executeTattooLoadout(requestFor(), backend, retries);
    expect(first &&
            first->state == core::TattooLoadoutRestoreState::forwardSynchronizationPending &&
            first->retryId.has_value() && backend.forwardWrites == 1,
        "forward sync failure must expose an opaque retry");

    const auto retried = runtime::retryTattooLoadoutSynchronization(
        {.actorFormId = 0x1234, .retryId = *first->retryId}, backend, retries);
    expect(retried && retried->state == core::TattooLoadoutRestoreState::success &&
            backend.forwardWrites == 1 && backend.rollbackWrites == 0 &&
            backend.synchronizeCalls == 2 && backend.forwardVerifyCalls == 1,
        "forward sync retry must not repeat writes");
}

void rollbackFailuresExposeTypedRecoveryStates() {
    runtime::TattooLoadoutRetryRegistry retries;
    FakeBackend writeFailure;
    writeFailure.failActionIndex = 0;
    writeFailure.failRollbackWrite = true;
    const auto incomplete = runtime::executeTattooLoadout(requestFor(), writeFailure, retries);
    expect(incomplete &&
            incomplete->state == core::TattooLoadoutRestoreState::rollbackIncomplete &&
            incomplete->affectedSlots.size() == 1,
        "rollback write failure must report incomplete recovery");

    FakeBackend verificationFailure;
    verificationFailure.failActionIndex = 0;
    verificationFailure.rollbackMatches = false;
    const auto unverified =
        runtime::executeTattooLoadout(requestFor(), verificationFailure, retries);
    expect(unverified &&
            unverified->state == core::TattooLoadoutRestoreState::rollbackIncomplete &&
            unverified->affectedSlots.size() == 1,
        "rollback verification failure must report affected slots");

    FakeBackend syncFailure;
    syncFailure.failActionIndex = 0;
    syncFailure.synchronizeResults = {false, true};
    const auto pending = runtime::executeTattooLoadout(requestFor(), syncFailure, retries);
    expect(pending &&
            pending->state == core::TattooLoadoutRestoreState::rollbackSynchronizationPending &&
            pending->retryId.has_value() && syncFailure.rollbackWrites == 1,
        "rollback sync failure must expose synchronization-only retry");
    const auto retried = runtime::retryTattooLoadoutSynchronization(
        {.actorFormId = 0x1234, .retryId = *pending->retryId}, syncFailure, retries);
    expect(retried && retried->state == core::TattooLoadoutRestoreState::rollbackComplete &&
            syncFailure.forwardWrites == 1 && syncFailure.rollbackWrites == 1 &&
            syncFailure.synchronizeCalls == 2 && syncFailure.rollbackVerifyCalls == 1,
        "rollback sync retry must not repeat compensating writes");
}

void retryRejectsActorMismatchAndConsumesContext() {
    FakeBackend backend;
    backend.synchronizeResults = {false};
    runtime::TattooLoadoutRetryRegistry retries;
    const auto first = runtime::executeTattooLoadout(requestFor(), backend, retries);
    const auto mismatched = runtime::retryTattooLoadoutSynchronization(
        {.actorFormId = 0x14, .retryId = *first->retryId}, backend, retries);
    const auto repeated = runtime::retryTattooLoadoutSynchronization(
        {.actorFormId = 0x1234, .retryId = *first->retryId}, backend, retries);
    expect(!mismatched && !repeated && backend.synchronizeCalls == 1,
        "Actor mismatch must reject and erase the retry context");
}

template <class Test>
int run(const std::string_view name, Test&& test) {
    try {
        std::forward<Test>(test)();
        std::cout << "PASS " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        return 1;
    }
}

}  // namespace

int main() {
    int failures{};
    failures += run("success synchronizes once", successUsesOneSynchronization);
    failures += run("preflight and capture failures do not write",
        preflightAndCaptureFailuresDoNotWrite);
    failures += run("action and verification failures rollback",
        actionAndVerificationFailuresRollback);
    failures += run("failure after every action index rolls back",
        failureAfterEveryActionIndexRollsBack);
    failures += run("forward synchronization retry does not repeat writes",
        forwardSynchronizationRetryDoesNotRepeatWrites);
    failures += run("rollback failures expose typed recovery states",
        rollbackFailuresExposeTypedRecoveryStates);
    failures += run("retry rejects Actor mismatch", retryRejectsActorMismatchAndConsumesContext);
    return failures == 0 ? 0 : 1;
}
