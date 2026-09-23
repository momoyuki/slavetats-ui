#include "runtime/UpdateTattooAppearanceOrchestration.h"

#include <exception>
#include <expected>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using stui::core::ServiceError;
using stui::core::ServiceErrorCode;
using stui::core::MutationSideEffect;
using stui::core::UpdateTattooAppearanceMode;
using stui::core::UpdateTattooAppearanceRequest;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class FakeAppearanceBackend final : public stui::runtime::IUpdateTattooAppearanceBackend {
public:
    ActorHandle resolveActor(std::uint32_t actorFormId) override {
        ++resolveCount;
        resolvedActorFormId = actorFormId;
        return actorAvailable ? static_cast<ActorHandle>(&actorStorage) : nullptr;
    }

    std::expected<std::vector<std::int32_t>, ServiceError> queryAppliedTattooHandles(
        ActorHandle actor) override {
        ++queryCount;
        queriedActor = actor;
        if (!querySucceeds) {
            return std::unexpected(ServiceError{
                ServiceErrorCode::updateFailed,
                "query_applied_tattoos failed",
            });
        }
        return appliedHandles;
    }

    bool writeAppearance(
        std::int32_t runtimeHandle,
        const UpdateTattooAppearanceRequest& request) override {
        ++writeCount;
        writtenHandle = runtimeHandle;
        writtenRequest = request;
        return writeSucceeds;
    }

    bool markActorUpdated(ActorHandle actor) override {
        ++markUpdatedCount;
        updatedActor = actor;
        return markUpdatedSucceeds;
    }

    bool synchronize(ActorHandle actor) override {
        ++synchronizeCount;
        synchronizedActor = actor;
        return synchronizationSucceeds;
    }

    bool actorAvailable{true};
    bool querySucceeds{true};
    bool writeSucceeds{true};
    bool markUpdatedSucceeds{true};
    bool synchronizationSucceeds{true};
    std::vector<std::int32_t> appliedHandles{73};
    int resolveCount{};
    int queryCount{};
    int writeCount{};
    int markUpdatedCount{};
    int synchronizeCount{};
    std::uint32_t resolvedActorFormId{};
    std::int32_t writtenHandle{};
    UpdateTattooAppearanceRequest writtenRequest{};
    int actorStorage{};
    ActorHandle queriedActor{};
    ActorHandle updatedActor{};
    ActorHandle synchronizedActor{};
};

UpdateTattooAppearanceRequest validRequest() {
    return {
        .actorFormId = 0x14,
        .runtimeHandle = 73,
        .color = 0x123456,
        .alpha = 0.25F,
        .glow = 0x102030,
        .glossiness = 2.5F,
        .specularStrength = 1.25F,
        .emissiveMult = 3.0F,
        .mode = UpdateTattooAppearanceMode::updateAndSynchronize,
    };
}

void expectErrorCode(
    const stui::core::UpdateTattooAppearanceResult& result,
    ServiceErrorCode code,
    std::string_view message) {
    expect(!result, message);
    expect(result.error().code == code, "unexpected service error code");
}

void missingActorStopsBeforeMutation() {
    FakeAppearanceBackend backend;
    backend.actorAvailable = false;

    const auto result = stui::runtime::updateTattooAppearance(validRequest(), backend);

    expectErrorCode(result, ServiceErrorCode::actorNotFound,
        "missing actor must return actorNotFound");
    expect(backend.resolveCount == 1 && backend.resolvedActorFormId == 0x14,
        "expected the requested actor to be resolved once");
    expect(backend.queryCount == 0 && backend.writeCount == 0 &&
            backend.markUpdatedCount == 0 && backend.synchronizeCount == 0,
        "missing actor must not query, write, mark updated, or synchronize");
}

void staleHandleStopsBeforeMutation(
    std::int32_t candidate,
    std::vector<std::int32_t> appliedHandles,
    std::string_view description) {
    FakeAppearanceBackend backend;
    backend.appliedHandles = std::move(appliedHandles);
    auto request = validRequest();
    request.runtimeHandle = candidate;

    const auto result = stui::runtime::updateTattooAppearance(request, backend);

    expectErrorCode(result, ServiceErrorCode::staleTattooHandle, description);
    expect(result.error().message.find("refresh") != std::string::npos,
        "stale handle error must direct the caller to refresh");
    expect(backend.queryCount == 1 && backend.writeCount == 0 &&
            backend.markUpdatedCount == 0 && backend.synchronizeCount == 0,
        "stale handle must not write, mark updated, or synchronize");
}

void zeroHandleStopsBeforeMutation() {
    staleHandleStopsBeforeMutation(0, {0, 73},
        "zero handle must return staleTattooHandle");
}

void absentHandleStopsBeforeMutation() {
    staleHandleStopsBeforeMutation(404, {73, 91},
        "absent handle must return staleTattooHandle");
}

void foreignHandleStopsBeforeMutation() {
    staleHandleStopsBeforeMutation(91, {73},
        "foreign actor handle must return staleTattooHandle");
}

void appearanceWriteFailureStopsBeforeUpdatedAndSynchronization() {
    FakeAppearanceBackend backend;
    backend.writeSucceeds = false;

    const auto result = stui::runtime::updateTattooAppearance(validRequest(), backend);

    expectErrorCode(result, ServiceErrorCode::updateFailed,
        "appearance write failure must return updateFailed");
    expect(result.error().mutationSideEffect == MutationSideEffect::mayHaveOccurred,
        "appearance write failure must disclose that a partial write may have occurred");
    expect(backend.writeCount == 1,
        "valid handle must attempt one appearance write");
    expect(backend.markUpdatedCount == 0 && backend.synchronizeCount == 0,
        "appearance write failure must not mark updated or synchronize");
}

void updatedMarkerFailureReportsThatAppearanceWasWritten() {
    FakeAppearanceBackend backend;
    backend.markUpdatedSucceeds = false;

    const auto result = stui::runtime::updateTattooAppearance(validRequest(), backend);

    expectErrorCode(result, ServiceErrorCode::updateFailed,
        "updated marker failure must return updateFailed");
    expect(result.error().mutationSideEffect == MutationSideEffect::mayHaveOccurred,
        "updated marker failure must disclose the completed appearance write");
    expect(backend.writeCount == 1 && backend.markUpdatedCount == 1 &&
            backend.synchronizeCount == 0,
        "updated marker failure occurs after the appearance write and before synchronization");
}

void validHandleWritesAppearanceMarksUpdatedAndSynchronizesOnce() {
    FakeAppearanceBackend backend;

    const auto result = stui::runtime::updateTattooAppearance(validRequest(), backend);

    expect(result.has_value(), "valid appearance update must succeed");
    expect(result->actorFormId == 0x14 && result->runtimeHandle == 73,
        "success must preserve actor and handle identity");
    expect(backend.queryCount == 1 && backend.writeCount == 1,
        "valid update must query membership and write once");
    expect(backend.writtenHandle == 73 &&
            backend.writtenRequest.color == 0x123456 &&
            backend.writtenRequest.alpha == 0.25F &&
            backend.writtenRequest.glow == 0x102030 &&
            backend.writtenRequest.glossiness == 2.5F &&
            backend.writtenRequest.specularStrength == 1.25F &&
            backend.writtenRequest.emissiveMult == 3.0F,
        "valid update must forward the complete editable appearance request");
    expect(backend.markUpdatedCount == 1 && backend.synchronizeCount == 1,
        "valid update must mark updated and synchronize exactly once");
    expect(backend.updatedActor == backend.queriedActor &&
            backend.synchronizedActor == backend.queriedActor,
        "all runtime operations must target the resolved actor");
}

void synchronizationFailureReportsPartialSuccess() {
    FakeAppearanceBackend backend;
    backend.synchronizationSucceeds = false;

    const auto result = stui::runtime::updateTattooAppearance(validRequest(), backend);

    expectErrorCode(result, ServiceErrorCode::synchronizeFailed,
        "synchronization failure must return synchronizeFailed");
    expect(result.error().message.find("appearance changed") != std::string::npos,
        "synchronization error must disclose partial success");
    expect(backend.writeCount == 1 && backend.markUpdatedCount == 1 &&
            backend.synchronizeCount == 1,
        "partial success must retain one write, updated mark, and sync attempt");
}

void synchronizeOnlySkipsAppearanceMutationAndSynchronizesOnce() {
    FakeAppearanceBackend backend;
    auto request = validRequest();
    request.runtimeHandle = 0;
    request.mode = UpdateTattooAppearanceMode::synchronizeOnly;

    const auto result = stui::runtime::updateTattooAppearance(request, backend);

    expect(result.has_value(), "synchronize-only retry must succeed without a handle");
    expect(backend.queryCount == 0 && backend.writeCount == 0,
        "synchronize-only retry must not query or write appearance");
    expect(backend.markUpdatedCount == 1 && backend.synchronizeCount == 1,
        "synchronize-only retry must mark updated and synchronize exactly once");
}

template <class Test>
int run(std::string_view name, Test&& test) {
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
    int failures = 0;
    failures += run("missing actor stops before mutation", missingActorStopsBeforeMutation);
    failures += run("zero handle stops before mutation", zeroHandleStopsBeforeMutation);
    failures += run("absent handle stops before mutation", absentHandleStopsBeforeMutation);
    failures += run("foreign handle stops before mutation", foreignHandleStopsBeforeMutation);
    failures += run("write failure stops before updated and sync",
        appearanceWriteFailureStopsBeforeUpdatedAndSynchronization);
    failures += run("updated marker failure reports completed appearance write",
        updatedMarkerFailureReportsThatAppearanceWasWritten);
    failures += run("valid handle writes, updates, and synchronizes once",
        validHandleWritesAppearanceMarksUpdatedAndSynchronizesOnce);
    failures += run("sync failure reports partial success",
        synchronizationFailureReportsPartialSuccess);
    failures += run("synchronize-only skips writes and synchronizes once",
        synchronizeOnlySkipsAppearanceMutationAndSynchronizesOnce);
    return failures == 0 ? 0 : 1;
}
