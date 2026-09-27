#include "core/ITattooRuntime.h"
#include "core/SlaveTatsService.h"

#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using stui::core::ITattooRuntime;
using stui::core::ApplyTattooMode;
using stui::core::ApplyTattooRequest;
using stui::core::ApplyTattooResult;
using stui::core::RemoveTattooRequest;
using stui::core::RemoveTattooResult;
using stui::core::RemoveTattooMode;
using stui::core::ServiceError;
using stui::core::ServiceErrorCode;
using stui::core::SlaveTatsService;
using stui::core::SlotOccupancy;
using stui::core::SetTattooLockedRequest;
using stui::core::SetTattooLockedResult;
using stui::core::TattooArea;
using stui::core::TattooEntry;
using stui::core::TattooQueryResult;
using stui::core::TattooSlots;
using stui::core::TattooSlotsResult;
using stui::core::UpdateTattooAppearanceMode;
using stui::core::UpdateTattooAppearanceRequest;
using stui::core::UpdateTattooAppearanceResult;

class FakeTattooRuntime final : public ITattooRuntime {
public:
    [[nodiscard]] bool apiAvailable() const noexcept override {
        return apiAvailableValue;
    }

    [[nodiscard]] bool jContainersReady() const noexcept override {
        return jContainersReadyValue;
    }

    TattooQueryResult queryAvailable(std::string_view domain) override {
        requestedDomain = std::string(domain);
        ++queryCount;
        return queryResult;
    }

    TattooSlotsResult querySlots(std::uint32_t actorFormId, TattooArea area) override {
        queriedActor = actorFormId;
        queriedArea = area;
        ++slotQueryCount;
        return slotQueryResult;
    }

    ApplyTattooResult applyToSlot(const ApplyTattooRequest& request) override {
        appliedRequest = request;
        ++applyCount;
        return applyResult;
    }

    RemoveTattooResult removeFromSlot(const RemoveTattooRequest& request) override {
        removedRequest = request;
        ++removeCount;
        return removeResult;
    }

    UpdateTattooAppearanceResult updateAppearance(
        const UpdateTattooAppearanceRequest& request) override {
        ++updateCount;
        updatedRequest = request;
        return stui::core::UpdateTattooAppearanceSuccess{
            .actorFormId = request.actorFormId,
            .runtimeHandle = request.runtimeHandle,
        };
    }

    SetTattooLockedResult setTattooLocked(const SetTattooLockedRequest& request) override {
        lockedRequest = request;
        ++lockCount;
        return lockResult;
    }

    bool apiAvailableValue{true};
    bool jContainersReadyValue{true};
    int queryCount{0};
    std::string requestedDomain;
    TattooQueryResult queryResult{std::vector<TattooEntry>{}};
    std::uint32_t queriedActor{0};
    TattooArea queriedArea{TattooArea::body};
    int slotQueryCount{0};
    TattooSlotsResult slotQueryResult{TattooSlots{}};
    ApplyTattooRequest appliedRequest;
    int applyCount{0};
    ApplyTattooResult applyResult{stui::core::ApplyTattooSuccess{}};
    RemoveTattooRequest removedRequest;
    int removeCount{0};
    RemoveTattooResult removeResult{stui::core::RemoveTattooSuccess{}};
    UpdateTattooAppearanceRequest updatedRequest;
    int updateCount{0};
    SetTattooLockedRequest lockedRequest;
    int lockCount{0};
    SetTattooLockedResult lockResult{stui::core::SetTattooLockedSuccess{}};
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <class Value>
void expectError(const std::expected<Value, ServiceError>& result, ServiceErrorCode code, std::string_view message) {
    expect(!result.has_value(), "expected operation to fail");
    expect(result.error().code == code, "unexpected service error code");
    expect(result.error().message == message, "unexpected service error message");
}

ApplyTattooRequest validApplyRequest() {
    return ApplyTattooRequest{
        .actorFormId = 0x14,
        .area = TattooArea::body,
        .slot = 2,
        .domain = "default",
        .section = "LewdMarks",
        .name = "Corruption",
        .color = 0xFF00FF,
        .alpha = 0.75F,
    };
}

RemoveTattooRequest validRemoveRequest() {
    return RemoveTattooRequest{
        .actorFormId = 0x14,
        .area = TattooArea::body,
        .slot = 2,
        .mode = RemoveTattooMode::removeAndSynchronize,
    };
}

UpdateTattooAppearanceRequest validAppearanceRequest() {
    return UpdateTattooAppearanceRequest{
        .actorFormId = 0x14,
        .runtimeHandle = 42,
        .color = 0xFF00FF,
        .alpha = 0.75F,
        .glow = 0x102030,
        .glossiness = 2.5F,
        .specularStrength = 1.25F,
        .emissiveMult = 3.0F,
        .mode = UpdateTattooAppearanceMode::updateAndSynchronize,
    };
}

SetTattooLockedRequest validLockRequest() {
    return SetTattooLockedRequest{
        .actorFormId = 0x14,
        .runtimeHandle = 42,
        .locked = true,
    };
}

void unavailableApiStopsBeforeRuntimeQuery() {
    FakeTattooRuntime runtime;
    runtime.apiAvailableValue = false;
    SlaveTatsService service(runtime);

    const auto result = service.queryAvailable("default");

    expectError(result, ServiceErrorCode::slaveTatsUnavailable, "SlaveTatsNG not available");
    expect(runtime.queryCount == 0, "runtime query must not run without the SlaveTats API");
}

void unavailableJContainersStopsBeforeRuntimeQuery() {
    FakeTattooRuntime runtime;
    runtime.jContainersReadyValue = false;
    SlaveTatsService service(runtime);

    const auto result = service.queryAvailable("default");

    expectError(result, ServiceErrorCode::jContainersUnavailable, "JContainers not ready");
    expect(runtime.queryCount == 0, "runtime query must not run before JContainers is ready");
}

void successfulQueryReturnsCopiedEntriesAndPreservesDomain() {
    FakeTattooRuntime runtime;
    runtime.queryResult = std::vector<TattooEntry>{TattooEntry{
        .runtimeHandle = 42,
        .domain = "custom",
        .section = "Roses",
        .name = "Red Rose",
        .texturePath = "roses/red.dds",
        .area = "Body",
        .slot = -1,
        .color = 0xFFFFFF,
        .locked = false,
        .alpha = 1.0F,
    }};
    SlaveTatsService service(runtime);

    const auto result = service.queryAvailable("custom");

    expect(result.has_value(), "expected query to succeed");
    expect(runtime.requestedDomain == "custom", "service must preserve the requested domain");
    expect(runtime.queryCount == 1, "service must issue exactly one runtime query");
    expect(result.value() == runtime.queryResult.value(), "service must return the runtime entries unchanged");
}

void runtimeFailureIsReturnedUnchanged() {
    FakeTattooRuntime runtime;
    runtime.queryResult = std::unexpected(ServiceError{
        ServiceErrorCode::queryAvailableFailed,
        "query_available_tattoos failed",
    });
    SlaveTatsService service(runtime);

    const auto result = service.queryAvailable("default");

    expectError(result, ServiceErrorCode::queryAvailableFailed, "query_available_tattoos failed");
}

void unavailableApiStopsSlotQuery() {
    FakeTattooRuntime runtime;
    runtime.apiAvailableValue = false;
    SlaveTatsService service(runtime);

    const auto result = service.querySlots(0x14, TattooArea::body);

    expectError(result, ServiceErrorCode::slaveTatsUnavailable, "SlaveTatsNG not available");
    expect(runtime.slotQueryCount == 0, "slot runtime must not run without the SlaveTats API");
}

void unavailableJContainersStopsSlotQuery() {
    FakeTattooRuntime runtime;
    runtime.jContainersReadyValue = false;
    SlaveTatsService service(runtime);

    const auto result = service.querySlots(0x14, TattooArea::body);

    expectError(result, ServiceErrorCode::jContainersUnavailable, "JContainers not ready");
    expect(runtime.slotQueryCount == 0, "slot runtime must not run before JContainers is ready");
}

void invalidSlotQueryActorIsRejected() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);

    const auto result = service.querySlots(0, TattooArea::body);

    expectError(result, ServiceErrorCode::actorNotFound, "Actor not found");
    expect(runtime.slotQueryCount == 0, "invalid actor must not reach the slot runtime");
}

void invalidSlotQueryAreaIsRejected() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);

    const auto result = service.querySlots(0x14, static_cast<TattooArea>(99));

    expectError(result, ServiceErrorCode::invalidArea, "Invalid tattoo area");
    expect(runtime.slotQueryCount == 0, "invalid area must not reach the slot runtime");
}

void validSlotQueryIsForwardedExactlyOnce() {
    FakeTattooRuntime runtime;
    runtime.slotQueryResult = TattooSlots{
        .actorFormId = 0x14,
        .area = TattooArea::hands,
        .configuredCount = 3,
        .slots = {{.index = 0, .occupancy = SlotOccupancy::empty}},
    };
    SlaveTatsService service(runtime);

    const auto result = service.querySlots(0x14, TattooArea::hands);

    expect(result.has_value(), "expected slot query success");
    expect(runtime.slotQueryCount == 1, "expected exactly one slot runtime query");
    expect(runtime.queriedActor == 0x14, "expected Player actor forwarded unchanged");
    expect(runtime.queriedArea == TattooArea::hands, "expected tattoo area forwarded unchanged");
    expect(result->configuredCount == 3 && result->slots.size() == 1,
        "expected runtime slot result returned unchanged");
}

void slotQueryRuntimeFailureIsReturnedUnchanged() {
    FakeTattooRuntime runtime;
    runtime.slotQueryResult = std::unexpected(ServiceError{
        ServiceErrorCode::slotQueryFailed,
        "query slots failed",
    });
    SlaveTatsService service(runtime);

    const auto result = service.querySlots(0x14, TattooArea::feet);

    expectError(result, ServiceErrorCode::slotQueryFailed, "query slots failed");
    expect(runtime.slotQueryCount == 1, "expected one failed slot runtime query");
}

void unavailableApiStopsApply() {
    FakeTattooRuntime runtime;
    runtime.apiAvailableValue = false;
    SlaveTatsService service(runtime);

    const auto result = service.applyToSlot(validApplyRequest());

    expectError(result, ServiceErrorCode::slaveTatsUnavailable, "SlaveTatsNG not available");
    expect(runtime.applyCount == 0, "apply runtime must not run without the SlaveTats API");
}

void unavailableJContainersStopsApply() {
    FakeTattooRuntime runtime;
    runtime.jContainersReadyValue = false;
    SlaveTatsService service(runtime);

    const auto result = service.applyToSlot(validApplyRequest());

    expectError(result, ServiceErrorCode::jContainersUnavailable, "JContainers not ready");
    expect(runtime.applyCount == 0, "apply runtime must not run before JContainers is ready");
}

void invalidApplyActorIsRejected() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto request = validApplyRequest();
    request.actorFormId = 0;

    const auto result = service.applyToSlot(request);

    expectError(result, ServiceErrorCode::actorNotFound, "Actor not found");
    expect(runtime.applyCount == 0, "invalid actor must not reach the apply runtime");
}

void invalidApplyAreaIsRejected() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto request = validApplyRequest();
    request.area = static_cast<TattooArea>(99);

    const auto result = service.applyToSlot(request);

    expectError(result, ServiceErrorCode::invalidArea, "Invalid tattoo area");
    expect(runtime.applyCount == 0, "invalid area must not reach the apply runtime");
}

void negativeApplySlotIsRejected() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto request = validApplyRequest();
    request.slot = -1;

    const auto result = service.applyToSlot(request);

    expectError(result, ServiceErrorCode::invalidSlot, "Invalid tattoo slot");
    expect(runtime.applyCount == 0, "negative slot must not reach the apply runtime");
}

void emptyApplySectionIsRejected() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto request = validApplyRequest();
    request.section.clear();

    const auto result = service.applyToSlot(request);

    expectError(result, ServiceErrorCode::tattooNotFound, "Tattoo section and name are required");
    expect(runtime.applyCount == 0, "empty section must not reach the apply runtime");
}

void emptyApplyNameIsRejected() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto request = validApplyRequest();
    request.name.clear();

    const auto result = service.applyToSlot(request);

    expectError(result, ServiceErrorCode::tattooNotFound, "Tattoo section and name are required");
    expect(runtime.applyCount == 0, "empty name must not reach the apply runtime");
}

void outOfRangeApplyAlphaIsRejected() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto belowRange = validApplyRequest();
    belowRange.alpha = -0.01F;
    auto aboveRange = validApplyRequest();
    aboveRange.alpha = 1.01F;
    auto notANumber = validApplyRequest();
    notANumber.alpha = std::numeric_limits<float>::quiet_NaN();

    const auto belowResult = service.applyToSlot(belowRange);
    const auto aboveResult = service.applyToSlot(aboveRange);
    const auto nanResult = service.applyToSlot(notANumber);

    expectError(belowResult, ServiceErrorCode::applyFailed, "Tattoo alpha must be between 0 and 1");
    expectError(aboveResult, ServiceErrorCode::applyFailed, "Tattoo alpha must be between 0 and 1");
    expectError(nanResult, ServiceErrorCode::applyFailed, "Tattoo alpha must be between 0 and 1");
    expect(runtime.applyCount == 0, "invalid alpha must not reach the apply runtime");
}

void boundaryApplyAlphaIsForwarded() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto request = validApplyRequest();
    request.alpha = 0.0F;

    const auto zeroResult = service.applyToSlot(request);
    request.alpha = 1.0F;
    const auto oneResult = service.applyToSlot(request);

    expect(zeroResult.has_value() && oneResult.has_value(), "expected inclusive alpha boundaries accepted");
    expect(runtime.applyCount == 2, "expected both boundary requests forwarded");
    expect(runtime.appliedRequest.alpha == 1.0F, "expected upper alpha boundary forwarded unchanged");
}

void validApplyRequestIsForwardedExactlyOnce() {
    FakeTattooRuntime runtime;
    runtime.applyResult = stui::core::ApplyTattooSuccess{
        .actorFormId = 0x14,
        .area = TattooArea::body,
        .slot = 2,
        .section = "LewdMarks",
        .name = "Corruption",
    };
    SlaveTatsService service(runtime);
    const auto request = validApplyRequest();

    const auto result = service.applyToSlot(request);

    expect(result.has_value(), "expected apply success");
    expect(runtime.applyCount == 1, "expected exactly one apply runtime call");
    expect(runtime.appliedRequest.actorFormId == 0x14 && runtime.appliedRequest.area == TattooArea::body,
        "expected apply target forwarded unchanged");
    expect(runtime.appliedRequest.slot == 2 && runtime.appliedRequest.domain == "default",
        "expected apply slot and domain forwarded unchanged");
    expect(runtime.appliedRequest.section == "LewdMarks" && runtime.appliedRequest.name == "Corruption",
        "expected tattoo identity forwarded unchanged");
    expect(runtime.appliedRequest.color == 0xFF00FF && runtime.appliedRequest.alpha == 0.75F,
        "expected tattoo appearance forwarded unchanged");
    expect(result->slot == 2 && result->name == "Corruption",
        "expected runtime apply result returned unchanged");
}

void synchronizeOnlyApplyBypassesMutationValidation() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto request = validApplyRequest();
    request.slot = -1;
    request.section.clear();
    request.name.clear();
    request.alpha = std::numeric_limits<float>::quiet_NaN();
    request.mode = ApplyTattooMode::synchronizeOnly;

    const auto result = service.applyToSlot(request);

    expect(result.has_value(), "expected synchronization-only Apply retry accepted");
    expect(runtime.applyCount == 1, "expected synchronization-only retry forwarded once");
    expect(runtime.appliedRequest.mode == ApplyTattooMode::synchronizeOnly,
        "expected exact synchronization-only Apply mode forwarded");
}

void applyRuntimeFailureIsReturnedUnchanged() {
    FakeTattooRuntime runtime;
    runtime.applyResult = std::unexpected(ServiceError{
        ServiceErrorCode::externalSlot,
        "slot is occupied by an external overlay",
    });
    SlaveTatsService service(runtime);

    const auto result = service.applyToSlot(validApplyRequest());

    expectError(result, ServiceErrorCode::externalSlot, "slot is occupied by an external overlay");
    expect(runtime.applyCount == 1, "expected one failed apply runtime call");
}

void unavailableDependenciesStopRemove() {
    FakeTattooRuntime unavailableApi;
    unavailableApi.apiAvailableValue = false;
    SlaveTatsService apiService(unavailableApi);
    FakeTattooRuntime unavailableJContainers;
    unavailableJContainers.jContainersReadyValue = false;
    SlaveTatsService jContainersService(unavailableJContainers);

    const auto apiResult = apiService.removeFromSlot(validRemoveRequest());
    const auto jContainersResult = jContainersService.removeFromSlot(validRemoveRequest());

    expectError(apiResult, ServiceErrorCode::slaveTatsUnavailable, "SlaveTatsNG not available");
    expectError(jContainersResult, ServiceErrorCode::jContainersUnavailable, "JContainers not ready");
    expect(unavailableApi.removeCount == 0 && unavailableJContainers.removeCount == 0,
        "remove runtime must not run before dependencies are ready");
}

void invalidRemoveTargetIsRejected() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto invalidActor = validRemoveRequest();
    invalidActor.actorFormId = 0;
    auto invalidArea = validRemoveRequest();
    invalidArea.area = static_cast<TattooArea>(99);
    auto invalidSlot = validRemoveRequest();
    invalidSlot.slot = -1;

    expectError(service.removeFromSlot(invalidActor), ServiceErrorCode::actorNotFound, "Actor not found");
    expectError(service.removeFromSlot(invalidArea), ServiceErrorCode::invalidArea, "Invalid tattoo area");
    expectError(service.removeFromSlot(invalidSlot), ServiceErrorCode::invalidSlot, "Invalid tattoo slot");
    expect(runtime.removeCount == 0, "invalid remove targets must not reach the runtime");
}

void validRemoveRequestIsForwardedExactlyOnce() {
    FakeTattooRuntime runtime;
    runtime.removeResult = stui::core::RemoveTattooSuccess{
        .actorFormId = 0x14,
        .area = TattooArea::body,
        .slot = 2,
    };
    SlaveTatsService service(runtime);

    const auto result = service.removeFromSlot(validRemoveRequest());

    expect(result.has_value(), "expected remove success");
    expect(runtime.removeCount == 1, "expected exactly one remove runtime call");
    expect(runtime.removedRequest.actorFormId == 0x14 &&
            runtime.removedRequest.area == TattooArea::body &&
            runtime.removedRequest.slot == 2 &&
            runtime.removedRequest.mode == RemoveTattooMode::removeAndSynchronize,
        "expected remove target forwarded unchanged");
    expect(result->slot == 2, "expected runtime remove result returned unchanged");
}

void removeRuntimeFailureIsReturnedUnchanged() {
    FakeTattooRuntime runtime;
    runtime.removeResult = std::unexpected(ServiceError{
        ServiceErrorCode::externalSlot,
        "Slot is occupied by an external overlay",
    });
    SlaveTatsService service(runtime);

    const auto result = service.removeFromSlot(validRemoveRequest());

    expectError(result, ServiceErrorCode::externalSlot, "Slot is occupied by an external overlay");
    expect(runtime.removeCount == 1, "expected one failed remove runtime call");
}

void unavailableDependenciesStopAppearanceUpdate() {
    FakeTattooRuntime unavailableApi;
    unavailableApi.apiAvailableValue = false;
    SlaveTatsService apiService(unavailableApi);
    FakeTattooRuntime unavailableJContainers;
    unavailableJContainers.jContainersReadyValue = false;
    SlaveTatsService jContainersService(unavailableJContainers);

    const auto apiResult = apiService.updateAppearance(validAppearanceRequest());
    const auto jContainersResult = jContainersService.updateAppearance(validAppearanceRequest());

    expectError(apiResult, ServiceErrorCode::slaveTatsUnavailable, "SlaveTatsNG not available");
    expectError(jContainersResult, ServiceErrorCode::jContainersUnavailable, "JContainers not ready");
    expect(unavailableApi.updateCount == 0 && unavailableJContainers.updateCount == 0,
        "appearance runtime must not run before dependencies are ready");
}

void zeroAppearanceActorIsRejected() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto request = validAppearanceRequest();
    request.actorFormId = 0;

    const auto result = service.updateAppearance(request);

    expectError(result, ServiceErrorCode::actorNotFound, "Actor not found");
    expect(runtime.updateCount == 0, "zero actor must not reach the appearance runtime");
}

void zeroAppearanceHandleIsRejectedForFullUpdate() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto request = validAppearanceRequest();
    request.runtimeHandle = 0;

    const auto result = service.updateAppearance(request);

    expectError(
        result,
        ServiceErrorCode::staleTattooHandle,
        "Tattoo handle is invalid; refresh the slot snapshot and try again");
    expect(runtime.updateCount == 0, "zero handle must not reach the full appearance update runtime");
}

void outOfRangeAppearanceColorIsRejected() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto belowRange = validAppearanceRequest();
    belowRange.color = -1;
    auto aboveRange = validAppearanceRequest();
    aboveRange.color = 0x1000000;

    const auto belowResult = service.updateAppearance(belowRange);
    const auto aboveResult = service.updateAppearance(aboveRange);

    expectError(belowResult, ServiceErrorCode::updateFailed, "Tattoo color must be between 0 and 0xFFFFFF");
    expectError(aboveResult, ServiceErrorCode::updateFailed, "Tattoo color must be between 0 and 0xFFFFFF");
    expect(runtime.updateCount == 0, "out-of-range color must not reach the appearance runtime");
}

void outOfRangeAppearanceAlphaIsRejected() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto belowRange = validAppearanceRequest();
    belowRange.alpha = -0.01F;
    auto aboveRange = validAppearanceRequest();
    aboveRange.alpha = 1.01F;
    auto notANumber = validAppearanceRequest();
    notANumber.alpha = std::numeric_limits<float>::quiet_NaN();
    auto infinity = validAppearanceRequest();
    infinity.alpha = std::numeric_limits<float>::infinity();

    const auto belowResult = service.updateAppearance(belowRange);
    const auto aboveResult = service.updateAppearance(aboveRange);
    const auto nanResult = service.updateAppearance(notANumber);
    const auto infinityResult = service.updateAppearance(infinity);

    expectError(belowResult, ServiceErrorCode::updateFailed, "Tattoo alpha must be between 0 and 1");
    expectError(aboveResult, ServiceErrorCode::updateFailed, "Tattoo alpha must be between 0 and 1");
    expectError(nanResult, ServiceErrorCode::updateFailed, "Tattoo alpha must be between 0 and 1");
    expectError(infinityResult, ServiceErrorCode::updateFailed, "Tattoo alpha must be between 0 and 1");
    expect(runtime.updateCount == 0, "out-of-range alpha must not reach the appearance runtime");
}

void invalidAppearanceGlowIsRejected() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto belowRange = validAppearanceRequest();
    belowRange.glow = -1;
    auto aboveRange = validAppearanceRequest();
    aboveRange.glow = 0x1000000;

    const auto belowResult = service.updateAppearance(belowRange);
    const auto aboveResult = service.updateAppearance(aboveRange);

    expectError(belowResult, ServiceErrorCode::updateFailed, "Tattoo glow must be between 0 and 0xFFFFFF");
    expectError(aboveResult, ServiceErrorCode::updateFailed, "Tattoo glow must be between 0 and 0xFFFFFF");
    expect(runtime.updateCount == 0, "out-of-range glow must not reach the appearance runtime");
}

void invalidAppearanceMaterialValuesAreRejected() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    const auto quietNan = std::numeric_limits<float>::quiet_NaN();
    const auto infinity = std::numeric_limits<float>::infinity();

    auto negativeGlossiness = validAppearanceRequest();
    negativeGlossiness.glossiness = -0.01F;
    auto nanGlossiness = validAppearanceRequest();
    nanGlossiness.glossiness = quietNan;
    auto infiniteGlossiness = validAppearanceRequest();
    infiniteGlossiness.glossiness = infinity;
    auto negativeSpecular = validAppearanceRequest();
    negativeSpecular.specularStrength = -0.01F;
    auto nanSpecular = validAppearanceRequest();
    nanSpecular.specularStrength = quietNan;
    auto infiniteSpecular = validAppearanceRequest();
    infiniteSpecular.specularStrength = infinity;
    auto negativeEmissive = validAppearanceRequest();
    negativeEmissive.emissiveMult = -0.01F;
    auto nanEmissive = validAppearanceRequest();
    nanEmissive.emissiveMult = quietNan;
    auto infiniteEmissive = validAppearanceRequest();
    infiniteEmissive.emissiveMult = infinity;

    expectError(service.updateAppearance(negativeGlossiness), ServiceErrorCode::updateFailed,
        "Tattoo glossiness must be finite and non-negative");
    expectError(service.updateAppearance(nanGlossiness), ServiceErrorCode::updateFailed,
        "Tattoo glossiness must be finite and non-negative");
    expectError(service.updateAppearance(infiniteGlossiness), ServiceErrorCode::updateFailed,
        "Tattoo glossiness must be finite and non-negative");
    expectError(service.updateAppearance(negativeSpecular), ServiceErrorCode::updateFailed,
        "Tattoo specular strength must be finite and non-negative");
    expectError(service.updateAppearance(nanSpecular), ServiceErrorCode::updateFailed,
        "Tattoo specular strength must be finite and non-negative");
    expectError(service.updateAppearance(infiniteSpecular), ServiceErrorCode::updateFailed,
        "Tattoo specular strength must be finite and non-negative");
    expectError(service.updateAppearance(negativeEmissive), ServiceErrorCode::updateFailed,
        "Tattoo emissive multiplier must be finite and non-negative");
    expectError(service.updateAppearance(nanEmissive), ServiceErrorCode::updateFailed,
        "Tattoo emissive multiplier must be finite and non-negative");
    expectError(service.updateAppearance(infiniteEmissive), ServiceErrorCode::updateFailed,
        "Tattoo emissive multiplier must be finite and non-negative");
    expect(runtime.updateCount == 0, "invalid material values must not reach the appearance runtime");
}

void appearanceBoundariesAreForwardedUnchanged() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto black = validAppearanceRequest();
    black.color = 0;
    black.alpha = 0.0F;
    auto white = validAppearanceRequest();
    white.color = 0xFFFFFF;
    white.alpha = 1.0F;

    const auto blackResult = service.updateAppearance(black);
    expect(blackResult.has_value(), "expected black and transparent appearance accepted");
    expect(runtime.updatedRequest.color == 0 && runtime.updatedRequest.alpha == 0.0F,
        "expected black and transparent appearance forwarded unchanged");
    const auto whiteResult = service.updateAppearance(white);

    expect(whiteResult.has_value(), "expected white and opaque appearance accepted");
    expect(runtime.updateCount == 2, "expected both appearance boundaries forwarded");
    expect(runtime.updatedRequest.color == 0xFFFFFF && runtime.updatedRequest.alpha == 1.0F,
        "expected white and opaque appearance forwarded unchanged");
    expect(runtime.updatedRequest.glow == 0x102030 && runtime.updatedRequest.glossiness == 2.5F &&
            runtime.updatedRequest.specularStrength == 1.25F && runtime.updatedRequest.emissiveMult == 3.0F,
        "expected advanced appearance values forwarded unchanged");
}

void zeroEmissiveAppearanceIsAcceptedAndForwardedUnchanged() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto request = validAppearanceRequest();
    request.emissiveMult = 0.0F;

    const auto result = service.updateAppearance(request);

    expect(result.has_value(), "expected zero emissive multiplier accepted");
    expect(runtime.updateCount == 1, "expected zero emissive update forwarded exactly once");
    expect(runtime.updatedRequest.actorFormId == 0x14 && runtime.updatedRequest.runtimeHandle == 42 &&
            runtime.updatedRequest.emissiveMult == 0.0F,
        "expected zero emissive multiplier forwarded unchanged");
}

void synchronizeOnlyAppearanceRequestIsForwardedUnchanged() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    const auto request = UpdateTattooAppearanceRequest{
        .actorFormId = 0x14,
        .runtimeHandle = 0,
        .color = 0,
        .alpha = 0.0F,
        .mode = UpdateTattooAppearanceMode::synchronizeOnly,
    };

    const auto result = service.updateAppearance(request);

    expect(result.has_value(), "expected synchronization-only request accepted without a handle");
    expect(runtime.updateCount == 1, "expected one synchronization-only runtime call");
    expect(runtime.updatedRequest.actorFormId == 0x14 && runtime.updatedRequest.runtimeHandle == 0,
        "expected synchronization actor and handle forwarded unchanged");
    expect(runtime.updatedRequest.color == 0 && runtime.updatedRequest.alpha == 0.0F &&
            runtime.updatedRequest.mode == UpdateTattooAppearanceMode::synchronizeOnly,
        "expected synchronization appearance mode and values forwarded unchanged");
    expect(result->actorFormId == 0x14 && result->runtimeHandle == 0,
        "expected synchronization runtime result returned unchanged");
}

void synchronizeOnlyAppearanceBypassesAppearanceValueValidation() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    const auto request = UpdateTattooAppearanceRequest{
        .actorFormId = 0x14,
        .runtimeHandle = 0,
        .color = -1,
        .alpha = std::numeric_limits<float>::quiet_NaN(),
        .glow = 0x1000000,
        .glossiness = -1.0F,
        .specularStrength = std::numeric_limits<float>::infinity(),
        .emissiveMult = -1.0F,
        .mode = UpdateTattooAppearanceMode::synchronizeOnly,
    };

    const auto result = service.updateAppearance(request);

    expect(result.has_value(), "expected synchronization-only request to bypass appearance-value validation");
    expect(runtime.updateCount == 1, "expected synchronization-only request forwarded despite stale values");
}

void validLockRequestIsForwardedExactlyOnce() {
    FakeTattooRuntime runtime;
    runtime.lockResult = stui::core::SetTattooLockedSuccess{
        .actorFormId = 0x14,
        .runtimeHandle = 42,
        .locked = true,
    };
    SlaveTatsService service(runtime);

    const auto result = service.setTattooLocked(validLockRequest());

    expect(result.has_value(), "expected lock request success");
    expect(runtime.lockCount == 1, "expected exactly one lock runtime call");
    expect(runtime.lockedRequest.actorFormId == 0x14 && runtime.lockedRequest.runtimeHandle == 42 &&
            runtime.lockedRequest.locked,
        "expected lock request forwarded unchanged");
}

void invalidLockRequestStopsBeforeRuntime() {
    FakeTattooRuntime runtime;
    SlaveTatsService service(runtime);
    auto zeroActor = validLockRequest();
    zeroActor.actorFormId = 0;
    auto zeroHandle = validLockRequest();
    zeroHandle.runtimeHandle = 0;

    expectError(service.setTattooLocked(zeroActor), ServiceErrorCode::actorNotFound,
        "Actor not found");
    expectError(service.setTattooLocked(zeroHandle), ServiceErrorCode::staleTattooHandle,
        "Tattoo handle is invalid; refresh the slot snapshot and try again");
    expect(runtime.lockCount == 0, "invalid lock request must not reach the runtime");
}

void unavailableDependenciesStopLockRequest() {
    FakeTattooRuntime unavailableApi;
    unavailableApi.apiAvailableValue = false;
    SlaveTatsService apiService(unavailableApi);
    FakeTattooRuntime unavailableJContainers;
    unavailableJContainers.jContainersReadyValue = false;
    SlaveTatsService jContainersService(unavailableJContainers);

    expectError(apiService.setTattooLocked(validLockRequest()),
        ServiceErrorCode::slaveTatsUnavailable, "SlaveTatsNG not available");
    expectError(jContainersService.setTattooLocked(validLockRequest()),
        ServiceErrorCode::jContainersUnavailable, "JContainers not ready");
    expect(unavailableApi.lockCount == 0 && unavailableJContainers.lockCount == 0,
        "unavailable dependencies must not reach the lock runtime");
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
    failures += run("unavailable API stops before runtime query", unavailableApiStopsBeforeRuntimeQuery);
    failures += run("unavailable JContainers stops before runtime query", unavailableJContainersStopsBeforeRuntimeQuery);
    failures += run("successful query returns entries and preserves domain", successfulQueryReturnsCopiedEntriesAndPreservesDomain);
    failures += run("runtime failure is returned unchanged", runtimeFailureIsReturnedUnchanged);
    failures += run("unavailable API stops slot query", unavailableApiStopsSlotQuery);
    failures += run("unavailable JContainers stops slot query", unavailableJContainersStopsSlotQuery);
    failures += run("invalid slot query actor is rejected", invalidSlotQueryActorIsRejected);
    failures += run("invalid slot query area is rejected", invalidSlotQueryAreaIsRejected);
    failures += run("valid slot query is forwarded exactly once", validSlotQueryIsForwardedExactlyOnce);
    failures += run("slot query runtime failure is returned unchanged", slotQueryRuntimeFailureIsReturnedUnchanged);
    failures += run("unavailable API stops apply", unavailableApiStopsApply);
    failures += run("unavailable JContainers stops apply", unavailableJContainersStopsApply);
    failures += run("invalid apply actor is rejected", invalidApplyActorIsRejected);
    failures += run("invalid apply area is rejected", invalidApplyAreaIsRejected);
    failures += run("negative apply slot is rejected", negativeApplySlotIsRejected);
    failures += run("empty apply section is rejected", emptyApplySectionIsRejected);
    failures += run("empty apply name is rejected", emptyApplyNameIsRejected);
    failures += run("out-of-range apply alpha is rejected", outOfRangeApplyAlphaIsRejected);
    failures += run("boundary apply alpha is forwarded", boundaryApplyAlphaIsForwarded);
    failures += run("valid apply request is forwarded exactly once", validApplyRequestIsForwardedExactlyOnce);
    failures += run("synchronize-only apply bypasses mutation validation",
        synchronizeOnlyApplyBypassesMutationValidation);
    failures += run("apply runtime failure is returned unchanged", applyRuntimeFailureIsReturnedUnchanged);
    failures += run("unavailable dependencies stop remove", unavailableDependenciesStopRemove);
    failures += run("invalid remove target is rejected", invalidRemoveTargetIsRejected);
    failures += run("valid remove request is forwarded exactly once", validRemoveRequestIsForwardedExactlyOnce);
    failures += run("remove runtime failure is returned unchanged", removeRuntimeFailureIsReturnedUnchanged);
    failures += run("unavailable dependencies stop appearance update", unavailableDependenciesStopAppearanceUpdate);
    failures += run("zero appearance actor is rejected", zeroAppearanceActorIsRejected);
    failures += run("zero appearance handle is rejected for full update", zeroAppearanceHandleIsRejectedForFullUpdate);
    failures += run("out-of-range appearance color is rejected", outOfRangeAppearanceColorIsRejected);
    failures += run("out-of-range appearance alpha is rejected", outOfRangeAppearanceAlphaIsRejected);
    failures += run("invalid appearance glow is rejected", invalidAppearanceGlowIsRejected);
    failures += run("invalid appearance material values are rejected", invalidAppearanceMaterialValuesAreRejected);
    failures += run("appearance boundaries are forwarded unchanged", appearanceBoundariesAreForwardedUnchanged);
    failures += run("zero emissive appearance is accepted and forwarded unchanged",
        zeroEmissiveAppearanceIsAcceptedAndForwardedUnchanged);
    failures += run("synchronize-only appearance request is forwarded unchanged", synchronizeOnlyAppearanceRequestIsForwardedUnchanged);
    failures += run("synchronize-only appearance bypasses appearance validation", synchronizeOnlyAppearanceBypassesAppearanceValueValidation);
    failures += run("valid lock request is forwarded exactly once", validLockRequestIsForwardedExactlyOnce);
    failures += run("invalid lock request stops before runtime", invalidLockRequestStopsBeforeRuntime);
    failures += run("unavailable dependencies stop lock request", unavailableDependenciesStopLockRequest);
    return failures == 0 ? 0 : 1;
}
