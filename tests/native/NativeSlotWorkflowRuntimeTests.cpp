#include "native/NativeSlotWorkflowRuntime.h"

#include <chrono>
#include <expected>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using stui::core::ApplyTattooRequest;
using stui::core::ApplyTattooSuccess;
using stui::core::RemoveTattooRequest;
using stui::core::RemoveTattooSuccess;
using stui::core::ServiceErrorCode;
using stui::core::SetTattooLockedRequest;
using stui::core::SetTattooLockedSuccess;
using stui::core::SlotOccupancy;
using stui::core::TattooArea;
using stui::core::TattooSlot;
using stui::core::TattooSlots;
using stui::core::UpdateTattooAppearanceMode;
using stui::core::UpdateTattooAppearanceRequest;
using stui::core::UpdateTattooAppearanceResult;
using stui::core::UpdateTattooAppearanceSuccess;
using stui::native::ActorTarget;
using stui::native::ActorTargetKind;
using stui::native::ActorTargetResult;
using stui::native::LivePreviewStatus;
using stui::native::NativeCatalogBrowserModel;
using stui::native::NativeSlotTask;
using stui::native::NativeSlotWorkflowModel;
using stui::native::NativeSlotWorkflowRuntime;
using stui::native::SlotWorkflowScreen;
using stui::repository::TattooCatalog;
using stui::repository::TattooDefinition;
using namespace std::chrono_literals;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

TattooSlots bodySlots() {
    return TattooSlots{
        .actorFormId = 0x14,
        .area = TattooArea::body,
        .configuredCount = 2,
        .slots = {
            TattooSlot{.index = 0, .occupancy = SlotOccupancy::empty},
            TattooSlot{.index = 1, .occupancy = SlotOccupancy::empty},
        },
    };
}

TattooSlots bodySlotsWithOwnedTattoo() {
    auto result = bodySlots();
    result.slots[1].occupancy = SlotOccupancy::slaveTats;
    result.slots[1].tattoo = stui::core::TattooEntry{
        .runtimeHandle = 73,
        .section = "Marks",
        .name = "Existing",
        .area = "BODY",
        .slot = 1,
    };
    return result;
}

TattooDefinition tattoo() {
    return TattooDefinition{
        .sourceId = "fixture.json",
        .sourceFile = "fixture.json",
        .packName = "Fixture",
        .name = "Rose",
        .section = "Marks",
        .texturePath = "rose.dds",
        .area = "Body",
    };
}

struct Fixture {
    Fixture()
        : catalog([this] { return snapshot; }),
          model(catalog),
          runtime(
              model,
              [this]() -> ActorTargetResult {
                  ++targetResolutionCount;
                  if (targetResolutionThrows) {
                      throw std::runtime_error("resolver failed");
                  }
                  return targetResult;
              },
              [this](std::uint32_t actor, TattooArea area) {
                  ++queryCount;
                  queriedActor = actor;
                  queriedArea = area;
                  if (queryThrows) {
                      throw std::runtime_error("query failed");
                  }
                  auto result = returnOwnedSlot ? bodySlotsWithOwnedTattoo() : bodySlots();
                  result.actorFormId = actor;
                  result.area = area;
                  return stui::core::TattooSlotsResult(std::move(result));
              },
              [this](const ApplyTattooRequest& request) {
                  ++applyCount;
                  appliedRequest = request;
                  if (applyThrows) {
                      throw std::runtime_error("apply failed");
                  }
                  return stui::core::ApplyTattooResult(ApplyTattooSuccess{
                      .actorFormId = request.actorFormId,
                      .area = request.area,
                      .slot = request.slot,
                      .section = request.section,
                      .name = request.name,
                  });
              },
              [this](const RemoveTattooRequest& request) {
                  ++removeCount;
                  removedRequest = request;
                  if (removeThrows) {
                      throw std::runtime_error("remove failed");
                  }
                  return stui::core::RemoveTattooResult(RemoveTattooSuccess{
                      .actorFormId = request.actorFormId,
                      .area = request.area,
                      .slot = request.slot,
                  });
              },
              [this](const UpdateTattooAppearanceRequest& request) {
                  ++appearanceCount;
                  appearanceRequest = request;
                  if (appearanceThrows) {
                      throw std::runtime_error("appearance update failed");
                  }
                  if (appearanceSyncFails) {
                      return UpdateTattooAppearanceResult(std::unexpected(stui::core::ServiceError{
                          .code = ServiceErrorCode::synchronizeFailed,
                          .message = "appearance synchronization failed",
                      }));
                  }
                  return UpdateTattooAppearanceResult(UpdateTattooAppearanceSuccess{
                      .actorFormId = request.actorFormId,
                      .runtimeHandle = request.runtimeHandle,
                  });
              },
              [this](const SetTattooLockedRequest& request) {
                  ++lockCount;
                  lockRequest = request;
                  return stui::core::SetTattooLockedResult(SetTattooLockedSuccess{
                      .actorFormId = request.actorFormId,
                      .runtimeHandle = request.runtimeHandle,
                      .locked = request.locked,
                  });
              },
              [this](NativeSlotTask task) {
                  if (schedulerThrows) {
                      schedulerThrows = false;
                      throw std::runtime_error("scheduler failed");
                  }
                  scheduled.push_back(std::move(task));
              },
              [this] { return now; }) {}

    stui::repository::TattooCatalogSnapshot snapshot =
        std::make_shared<const TattooCatalog>(TattooCatalog{
            .repository = stui::repository::TattooRepository({}),
            .sourceCount = 0,
        });
    NativeCatalogBrowserModel catalog;
    NativeSlotWorkflowModel model;
    std::vector<NativeSlotTask> scheduled;
    std::size_t targetResolutionCount{};
    ActorTargetResult targetResult{ActorTarget{
        .kind = ActorTargetKind::crosshair, .formId = 0x1234, .displayName = "Lydia"}};
    std::size_t queryCount{};
    std::size_t applyCount{};
    std::size_t removeCount{};
    std::size_t appearanceCount{};
    std::size_t lockCount{};
    std::uint32_t queriedActor{};
    TattooArea queriedArea{TattooArea::feet};
    ApplyTattooRequest appliedRequest;
    RemoveTattooRequest removedRequest;
    UpdateTattooAppearanceRequest appearanceRequest;
    SetTattooLockedRequest lockRequest;
    bool returnOwnedSlot{};
    bool targetResolutionThrows{};
    bool queryThrows{};
    bool applyThrows{};
    bool removeThrows{};
    bool appearanceThrows{};
    bool appearanceSyncFails{};
    bool schedulerThrows{};
    std::chrono::steady_clock::time_point now{};
    NativeSlotWorkflowRuntime runtime;
};

void completeInitialOwnedSlotQuery(Fixture& fixture) {
    fixture.returnOwnedSlot = true;
    fixture.model.start();
    fixture.runtime.pump();
    fixture.scheduled.back()();
}

void beginAppearanceEdit(Fixture& fixture) {
    completeInitialOwnedSlotQuery(fixture);
    expect(fixture.model.selectSlot(1) && fixture.model.beginEditAppearance(),
        "expected editable owned slot");
    fixture.model.setEditedAppearance(0x123456, 0.35F);
}

void prepareSynchronizeOnlyRetry(Fixture& fixture) {
    beginAppearanceEdit(fixture);
    expect(fixture.model.confirmAppearanceUpdate(), "expected full appearance update request");
    const auto fullUpdate = fixture.model.takeAppearanceRequest();
    expect(static_cast<bool>(fullUpdate), "expected full appearance update ticket");
    fixture.model.completeAppearanceUpdate(fullUpdate->generation, std::unexpected(
        stui::core::ServiceError{
            .code = ServiceErrorCode::synchronizeFailed,
            .message = "appearance synchronization failed",
        }));
    expect(fixture.model.confirmAppearanceUpdate(), "expected synchronization retry request");
}

void previewSchedulesOnceAtTheExactDebounceDeadline() {
    Fixture fixture;
    beginAppearanceEdit(fixture);
    fixture.model.setEditedAppearance(0x123456, 0.35F, 0x654321, 0.25F, 0.75F, 2.0F);
    fixture.runtime.pump();
    for (const auto elapsed : {0ms, 500ms, 999ms}) {
        fixture.now = std::chrono::steady_clock::time_point{} + elapsed;
        fixture.runtime.pump();
        expect(fixture.scheduled.size() == 1 && fixture.appearanceCount == 0,
            "expected no appearance work before 1000ms");
    }
    fixture.now += 1ms;
    fixture.runtime.pump();
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 2 && fixture.appearanceCount == 0,
        "expected exactly one deferred appearance operation at 1000ms");
    fixture.scheduled.back()();
    expect(fixture.appearanceCount == 1 && fixture.appearanceRequest.actorFormId == 0x14 &&
            fixture.appearanceRequest.runtimeHandle == 73 &&
            fixture.appearanceRequest.color == 0x123456 && fixture.appearanceRequest.alpha == 0.35F &&
            fixture.appearanceRequest.glow == 0x654321 && fixture.appearanceRequest.glossiness == 0.25F &&
            fixture.appearanceRequest.specularStrength == 0.75F && fixture.appearanceRequest.emissiveMult == 2.0F &&
            fixture.appearanceRequest.mode == UpdateTattooAppearanceMode::updateAndSynchronize,
        "expected exact preview identity and all editable values forwarded once");
    fixture.now += 5000ms;
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 2 && fixture.model.editAppearance() &&
            fixture.model.livePreviewStatus() == LivePreviewStatus::applied,
        "expected successful preview to keep editor open without repeat writes or refresh");
}

void previewObservesLatestEditsBeforeTheInFlightGuard() {
    for (const bool deadlineElapsed : {false, true}) {
        Fixture fixture;
        beginAppearanceEdit(fixture);
        fixture.runtime.pump();
        fixture.now += 1000ms;
        fixture.runtime.pump();
        fixture.now += 100ms;
        fixture.model.setEditedAppearance(0x222222, 0.2F);
        fixture.runtime.pump();
        fixture.now += 500ms;
        fixture.model.setEditedAppearance(0x333333, 0.3F, 0, 0, 0, 0);
        fixture.runtime.pump();
        if (deadlineElapsed) {
            fixture.now += 1400ms;
            fixture.runtime.pump();
        }
        expect(fixture.scheduled.size() == 2,
            "expected later edits to coalesce while the first preview is scheduled");
        fixture.scheduled.back()();
        expect(fixture.appearanceRequest.color == 0x123456 &&
                fixture.model.livePreviewStatus() == LivePreviewStatus::pending,
            "expected queued request immutable and latest local edit retained");
        if (!deadlineElapsed) {
            fixture.now += 999ms;
            fixture.runtime.pump();
            expect(fixture.scheduled.size() == 2, "expected latest edit to restart debounce");
            fixture.now += 1ms;
        }
        fixture.runtime.pump();
        fixture.runtime.pump();
        expect(fixture.scheduled.size() == 3,
            "expected latest observed edit scheduled once after completion and deadline");
        fixture.scheduled.back()();
        expect(fixture.appearanceCount == 2 && fixture.appearanceRequest.color == 0x333333 &&
                fixture.appearanceRequest.alpha == 0.3F && fixture.appearanceRequest.glow == 0 &&
                fixture.appearanceRequest.emissiveMult == 0,
            "expected only newest appearance after in-flight completion");
    }
}

void saveFlushesPendingPreviewWithoutWaitingForClock() {
    Fixture fixture;
    beginAppearanceEdit(fixture);
    fixture.runtime.pump();
    fixture.now += 400ms;
    fixture.model.setEditedAppearance(0xABCDEF, 0.8F);
    expect(fixture.model.confirmAppearanceUpdate(), "expected immediate Save intent");
    fixture.runtime.pump();
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 2, "expected Save to bypass debounce exactly once");
    fixture.scheduled.back()();
    expect(fixture.appearanceCount == 1 && fixture.appearanceRequest.color == 0xABCDEF &&
            fixture.model.screen() == SlotWorkflowScreen::currentSlots,
        "expected Save to commit latest edit before its preview deadline");
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 3, "expected Save completion to release guard for refresh");
}

void previewExceptionsRemainRetryableAndReleaseTheGuard() {
    for (const bool rejectScheduler : {false, true}) {
        Fixture fixture;
        beginAppearanceEdit(fixture);
        fixture.runtime.pump();
        fixture.now += 1000ms;
        fixture.schedulerThrows = rejectScheduler;
        fixture.appearanceThrows = !rejectScheduler;
        fixture.runtime.pump();
        if (!rejectScheduler) {
            fixture.scheduled.back()();
        }
        expect(fixture.model.editAppearance() && fixture.model.error() &&
                fixture.model.error()->code == ServiceErrorCode::updateFailed &&
                fixture.model.livePreviewStatus() == LivePreviewStatus::previewError,
            "expected preview exception reported as retryable updateFailed");
        const auto beforeRetry = fixture.scheduled.size();
        fixture.now += 5000ms;
        fixture.runtime.pump();
        expect(fixture.scheduled.size() == beforeRetry, "expected no automatic retry after failure");
        fixture.appearanceThrows = false;
        expect(fixture.model.retryLivePreviewOperation(), "expected explicit preview retry accepted");
        fixture.runtime.pump();
        fixture.runtime.pump();
        expect(fixture.scheduled.size() == beforeRetry + 1, "expected failed task to release runtime guard");
        fixture.scheduled.back()();
        expect(fixture.appearanceRequest.mode == UpdateTattooAppearanceMode::updateAndSynchronize &&
                fixture.model.livePreviewStatus() == LivePreviewStatus::applied && !fixture.model.error(),
            "expected successful full retry to retain preview purpose");
    }
}

void previewSynchronizationRetryNeverRequestsAnotherWrite() {
    for (const bool rejectScheduler : {false, true}) {
        Fixture fixture;
        beginAppearanceEdit(fixture);
        fixture.runtime.pump();
        fixture.now += 1000ms;
        fixture.appearanceSyncFails = true;
        fixture.runtime.pump();
        fixture.scheduled.back()();
        expect(fixture.appearanceCount == 1 &&
                fixture.appearanceRequest.mode == UpdateTattooAppearanceMode::updateAndSynchronize,
            "expected one original write before synchronization failure");
        fixture.appearanceSyncFails = false;
        expect(fixture.model.retryLivePreviewOperation(), "expected preview synchronization retry");
        fixture.schedulerThrows = rejectScheduler;
        fixture.appearanceThrows = !rejectScheduler;
        fixture.runtime.pump();
        if (!rejectScheduler) {
            fixture.scheduled.back()();
            expect(fixture.appearanceRequest.mode == UpdateTattooAppearanceMode::synchronizeOnly,
                "expected throwing retry to request synchronization only");
        }
        expect(fixture.model.error() && fixture.model.error()->code == ServiceErrorCode::synchronizeFailed,
            "expected retry exception to retain synchronization error semantics");
        fixture.appearanceThrows = false;
        expect(fixture.model.retryLivePreviewOperation(), "expected failed sync retry to remain retryable");
        const auto beforeRetry = fixture.scheduled.size();
        fixture.runtime.pump();
        fixture.runtime.pump();
        expect(fixture.scheduled.size() == beforeRetry + 1, "expected one scheduled synchronization retry");
        fixture.scheduled.back()();
        expect(fixture.appearanceRequest.actorFormId == 0x14 && fixture.appearanceRequest.runtimeHandle == 73 &&
                fixture.appearanceRequest.mode == UpdateTattooAppearanceMode::synchronizeOnly &&
                fixture.model.livePreviewStatus() == LivePreviewStatus::applied && fixture.model.editAppearance(),
            "expected synchronization-only success to preserve identity and preview purpose");
    }
}

void stalePreviewCompletionCannotChangeAReplacementSession() {
    for (const bool operationThrows : {false, true}) {
        Fixture fixture;
        beginAppearanceEdit(fixture);
        fixture.runtime.pump();
        fixture.now += 1000ms;
        auto ticketView = fixture.model;
        ticketView.advanceLivePreview(fixture.now);
        const auto oldTicket = ticketView.takeAppearanceRequest();
        expect(oldTicket.has_value(), "expected copied model to expose the scheduled generation");
        fixture.runtime.pump();

        // Inject an earlier delivered failure so the delayed scheduler completion is obsolete.
        fixture.model.completeAppearanceUpdate(oldTicket->generation, std::unexpected(stui::core::ServiceError{
            .code = ServiceErrorCode::updateFailed, .message = "earlier delivered failure",
        }));
        fixture.model.cancelEditAppearance();
        expect(fixture.model.selectCrosshairTarget(), "expected safe target replacement after failed write");
        const auto target = fixture.model.takeActorTargetRequest();
        expect(target.has_value(), "expected replacement target resolution");
        fixture.model.completeActorTargetResolution(target->generation, fixture.targetResult);
        const auto query = fixture.model.takeSlotQuery();
        expect(query.has_value(), "expected replacement Actor query");
        auto slots = bodySlotsWithOwnedTattoo();
        slots.actorFormId = 0x1234;
        slots.slots[1].tattoo->runtimeHandle = 99;
        slots.slots[1].tattoo->texturePath = "replacement.dds";
        fixture.model.completeSlotQuery(query->generation, std::move(slots));
        expect(fixture.model.selectSlot(1) && fixture.model.beginEditAppearance(), "expected replacement edit session");
        fixture.model.setEditedAppearance(0xABCDEF, 0.8F);
        fixture.runtime.pump();
        fixture.now += 1000ms;
        fixture.runtime.pump();
        expect(fixture.scheduled.size() == 2, "expected old task to retain coordinator guard");
        fixture.appearanceThrows = operationThrows;
        fixture.scheduled.back()();
        expect(fixture.model.actorTarget()->formId == 0x1234 && fixture.model.editAppearance() &&
                fixture.model.editAppearance()->runtimeHandle == 99 &&
                fixture.model.editAppearance()->texturePath == "replacement.dds" &&
                fixture.model.editAppearance()->edited.color == 0xABCDEF &&
                !fixture.model.error() && !fixture.model.takeMenuCloseRequest(),
            "expected stale success or failure not to alter replacement identity, edits, error, or Close");
        fixture.appearanceThrows = false;
        fixture.runtime.pump();
        fixture.runtime.pump();
        expect(fixture.scheduled.size() == 3, "expected stale completion to release guard for replacement preview");
        fixture.scheduled.back()();
        expect(fixture.appearanceRequest.actorFormId == 0x1234 && fixture.appearanceRequest.runtimeHandle == 99 &&
                fixture.appearanceRequest.color == 0xABCDEF &&
                fixture.model.livePreviewStatus() == LivePreviewStatus::applied,
            "expected only current Actor appearance completion accepted");
    }
}

void schedulesOnlyOneQueryAndCompletesModel() {
    Fixture fixture;
    fixture.model.start();

    fixture.runtime.pump();
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 1,
        "expected repeated pumps to preserve one in-flight query");
    expect(fixture.queryCount == 0, "expected operation deferred to scheduler task");

    fixture.scheduled.front()();
    expect(fixture.queryCount == 1 && fixture.queriedActor == 0x14 &&
            fixture.queriedArea == TattooArea::body,
        "expected Player BODY query on scheduled task");
    expect(fixture.model.slots() && fixture.model.slots()->slots.size() == 2,
        "expected query completion published to workflow model");
}

void applySchedulesOnlyAfterExplicitConfirmation() {
    Fixture fixture;
    fixture.model.start();
    fixture.runtime.pump();
    fixture.scheduled.front()();
    expect(fixture.model.selectSlot(1), "expected empty slot selection");
    fixture.model.selectTattoo(tattoo());

    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 1,
        "expected preview alone not to schedule Apply");
    expect(fixture.model.confirmApply(), "expected explicit Apply confirmation");
    fixture.runtime.pump();
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 2,
        "expected one Apply task after confirmation");

    fixture.scheduled.back()();
    expect(fixture.applyCount == 1 && fixture.appliedRequest.slot == 1,
        "expected confirmed slot request forwarded once");
    expect(fixture.model.screen() == SlotWorkflowScreen::currentSlots,
        "expected successful Apply to return to Current Slots");
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 3,
        "expected successful Apply to schedule a fresh slot query");
}

void removeSchedulesOnlyAfterExplicitConfirmation() {
    Fixture fixture;
    fixture.returnOwnedSlot = true;
    fixture.model.start();
    fixture.runtime.pump();
    fixture.scheduled.front()();
    expect(fixture.model.selectSlot(1), "expected owned slot selection");
    expect(fixture.model.requestRemove(), "expected Remove action");

    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 1,
        "expected confirmation screen alone not to schedule Remove");
    expect(fixture.model.confirmRemove(), "expected explicit Remove confirmation");
    fixture.runtime.pump();
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 2,
        "expected one Remove task after confirmation");

    fixture.scheduled.back()();
    expect(fixture.removeCount == 1 &&
            fixture.removedRequest.actorFormId == 0x14 &&
            fixture.removedRequest.area == TattooArea::body &&
            fixture.removedRequest.slot == 1,
        "expected confirmed Remove target forwarded once");
    expect(fixture.model.screen() == SlotWorkflowScreen::currentSlots,
        "expected successful Remove to return to Current Slots");
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 3,
        "expected successful Remove to schedule a fresh slot query");
}

void lockSchedulesOneActorScopedMutationAndRefreshes() {
    Fixture fixture;
    completeInitialOwnedSlotQuery(fixture);
    expect(fixture.model.selectSlot(1), "expected owned slot selected");
    expect(fixture.model.toggleSelectedSlotLock(), "expected Lock action accepted");

    fixture.runtime.pump();
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 2, "expected one scheduled lock mutation");
    fixture.scheduled.back()();

    expect(fixture.lockCount == 1 && fixture.lockRequest.actorFormId == 0x14 &&
            fixture.lockRequest.runtimeHandle == 73 && fixture.lockRequest.locked,
        "expected actor-scoped Lock request forwarded once");
    expect(fixture.model.screen() == SlotWorkflowScreen::currentSlots,
        "expected successful Lock to return to Current Slots");
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 3,
        "expected successful Lock to refresh selected slots");
}

void convertsOperationAndSchedulerExceptionsToModelErrors() {
    Fixture queryFailure;
    queryFailure.queryThrows = true;
    queryFailure.model.start();
    queryFailure.runtime.pump();
    queryFailure.scheduled.front()();
    expect(queryFailure.model.error() &&
            queryFailure.model.error()->code == ServiceErrorCode::slotQueryFailed,
        "expected query exception converted to slotQueryFailed");

    Fixture schedulingFailure;
    schedulingFailure.schedulerThrows = true;
    schedulingFailure.model.start();
    schedulingFailure.runtime.pump();
    expect(schedulingFailure.model.error() &&
            schedulingFailure.model.error()->code == ServiceErrorCode::slotQueryFailed,
        "expected scheduler exception to complete the pending query");
}

void convertsApplyAndApplySchedulerExceptionsToModelErrors() {
    Fixture applyFailure;
    applyFailure.model.start();
    applyFailure.runtime.pump();
    applyFailure.scheduled.front()();
    expect(applyFailure.model.selectSlot(0), "expected Apply failure target slot");
    applyFailure.model.selectTattoo(tattoo());
    expect(applyFailure.model.confirmApply(), "expected Apply failure confirmation");
    applyFailure.applyThrows = true;
    applyFailure.runtime.pump();
    applyFailure.scheduled.back()();
    expect(applyFailure.model.screen() == SlotWorkflowScreen::preview &&
            applyFailure.model.error() &&
            applyFailure.model.error()->code == ServiceErrorCode::applyFailed,
        "expected Apply exception converted to retryable applyFailed error");

    Fixture schedulingFailure;
    schedulingFailure.model.start();
    schedulingFailure.runtime.pump();
    schedulingFailure.scheduled.front()();
    expect(schedulingFailure.model.selectSlot(0),
        "expected Apply scheduler failure target slot");
    schedulingFailure.model.selectTattoo(tattoo());
    expect(schedulingFailure.model.confirmApply(),
        "expected Apply scheduler failure confirmation");
    schedulingFailure.schedulerThrows = true;
    schedulingFailure.runtime.pump();
    expect(schedulingFailure.model.screen() == SlotWorkflowScreen::preview &&
            schedulingFailure.model.error() &&
            schedulingFailure.model.error()->code == ServiceErrorCode::applyFailed,
        "expected Apply scheduler exception converted to retryable error");
}

void convertsRemoveExceptionsToRetryableModelErrors() {
    Fixture removeFailure;
    removeFailure.returnOwnedSlot = true;
    removeFailure.model.start();
    removeFailure.runtime.pump();
    removeFailure.scheduled.front()();
    expect(removeFailure.model.selectSlot(1) &&
            removeFailure.model.requestRemove() &&
            removeFailure.model.confirmRemove(),
        "expected confirmed Remove failure flow");
    removeFailure.removeThrows = true;
    removeFailure.runtime.pump();
    removeFailure.scheduled.back()();

    expect(removeFailure.model.screen() == SlotWorkflowScreen::removeConfirmation &&
            removeFailure.model.error() &&
            removeFailure.model.error()->code == ServiceErrorCode::removeFailed,
        "expected Remove exception converted to retryable error");
}

void schedulesFullAppearanceUpdateOnceAndReleasesInFlightGuard() {
    Fixture fixture;
    beginAppearanceEdit(fixture);
    expect(fixture.model.confirmAppearanceUpdate(), "expected full appearance update request");

    fixture.runtime.pump();
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 2,
        "expected repeated pumps to schedule one full appearance update");
    expect(fixture.appearanceCount == 0,
        "expected appearance operation deferred to the game-thread task");

    fixture.scheduled.back()();
    expect(fixture.appearanceCount == 1 && fixture.appearanceRequest.actorFormId == 0x14 &&
            fixture.appearanceRequest.runtimeHandle == 73 &&
            fixture.appearanceRequest.color == 0x123456 &&
            fixture.appearanceRequest.alpha == 0.35F &&
            fixture.appearanceRequest.mode == UpdateTattooAppearanceMode::updateAndSynchronize,
        "expected full appearance request forwarded to the scheduled operation");
    expect(fixture.model.screen() == SlotWorkflowScreen::currentSlots,
        "expected successful full appearance update to complete the model");

    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 3,
        "expected full appearance completion to release the in-flight guard for refresh");
}

void schedulesSynchronizeOnlyAppearanceUpdateAndReleasesInFlightGuard() {
    Fixture fixture;
    prepareSynchronizeOnlyRetry(fixture);

    fixture.runtime.pump();
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 2,
        "expected repeated pumps to schedule one synchronization-only update");
    fixture.scheduled.back()();

    expect(fixture.appearanceCount == 1 &&
            fixture.appearanceRequest.mode == UpdateTattooAppearanceMode::synchronizeOnly,
        "expected synchronization retry forwarded without a second full update");
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 3,
        "expected synchronization completion to release the in-flight guard for refresh");
}

void mapsFullAppearanceExceptionsAndSchedulerRejectionToUpdateFailed() {
    Fixture operationFailure;
    beginAppearanceEdit(operationFailure);
    expect(operationFailure.model.confirmAppearanceUpdate(),
        "expected full appearance operation failure request");
    operationFailure.appearanceThrows = true;
    operationFailure.runtime.pump();
    operationFailure.scheduled.back()();
    expect(operationFailure.model.screen() == SlotWorkflowScreen::editAppearance &&
            operationFailure.model.error() &&
            operationFailure.model.error()->code == ServiceErrorCode::updateFailed,
        "expected full appearance exception converted to updateFailed");
    operationFailure.appearanceThrows = false;
    expect(operationFailure.model.confirmAppearanceUpdate(),
        "expected full appearance exception to remain retryable");
    operationFailure.runtime.pump();
    expect(operationFailure.scheduled.size() == 3,
        "expected operation exception to release the in-flight guard");

    Fixture schedulingFailure;
    beginAppearanceEdit(schedulingFailure);
    expect(schedulingFailure.model.confirmAppearanceUpdate(),
        "expected full appearance scheduler failure request");
    schedulingFailure.schedulerThrows = true;
    schedulingFailure.runtime.pump();
    expect(schedulingFailure.model.screen() == SlotWorkflowScreen::editAppearance &&
            schedulingFailure.model.error() &&
            schedulingFailure.model.error()->code == ServiceErrorCode::updateFailed,
        "expected full appearance scheduler rejection converted to updateFailed");
    expect(schedulingFailure.model.confirmAppearanceUpdate(),
        "expected full appearance scheduler rejection to remain retryable");
    schedulingFailure.runtime.pump();
    expect(schedulingFailure.scheduled.size() == 2,
        "expected scheduler rejection to release the in-flight guard");
}

void mapsSynchronizeOnlyExceptionsAndSchedulerRejectionToSynchronizeFailed() {
    Fixture operationFailure;
    prepareSynchronizeOnlyRetry(operationFailure);
    operationFailure.appearanceThrows = true;
    operationFailure.runtime.pump();
    operationFailure.scheduled.back()();
    expect(operationFailure.model.screen() == SlotWorkflowScreen::editAppearance &&
            operationFailure.model.error() &&
            operationFailure.model.error()->code == ServiceErrorCode::synchronizeFailed,
        "expected synchronization exception converted to synchronizeFailed");
    expect(operationFailure.model.confirmAppearanceUpdate(),
        "expected synchronization exception to remain retryable");
    operationFailure.runtime.pump();
    expect(operationFailure.scheduled.size() == 3,
        "expected synchronization exception to release the in-flight guard");

    Fixture schedulingFailure;
    prepareSynchronizeOnlyRetry(schedulingFailure);
    schedulingFailure.schedulerThrows = true;
    schedulingFailure.runtime.pump();
    expect(schedulingFailure.model.screen() == SlotWorkflowScreen::editAppearance &&
            schedulingFailure.model.error() &&
            schedulingFailure.model.error()->code == ServiceErrorCode::synchronizeFailed,
        "expected synchronization scheduler rejection converted to synchronizeFailed");
    expect(schedulingFailure.model.confirmAppearanceUpdate(),
        "expected synchronization scheduler rejection to remain retryable");
    schedulingFailure.runtime.pump();
    expect(schedulingFailure.scheduled.size() == 2,
        "expected synchronization scheduler rejection to release the in-flight guard");
}

void schedulesOneCrosshairResolutionBeforeQueryingResolvedActor() {
    Fixture fixture;
    fixture.model.start();
    expect(fixture.model.selectCrosshairTarget(), "expected Crosshair selection");
    fixture.runtime.pump();
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 1 && fixture.targetResolutionCount == 0 &&
            fixture.queryCount == 0,
        "expected exactly one deferred target resolution and no early query");
    fixture.scheduled.front()();
    expect(fixture.targetResolutionCount == 1 && fixture.model.actorTarget() &&
            fixture.model.actorTarget()->formId == 0x1234 &&
            fixture.model.actorTarget()->displayName == "Lydia" &&
            !fixture.model.isActorTargetResolutionInFlight(),
        "expected resolved Crosshair target delivered to model");
    fixture.runtime.pump();
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 2, "expected one query after resolution completes");
    fixture.scheduled.back()();
    expect(fixture.queryCount == 1 && fixture.queriedActor == 0x1234 &&
            fixture.model.slots() && fixture.model.slots()->actorFormId == 0x1234,
        "expected slots queried for resolved Actor only");
}

void resolverExceptionFailsWithoutFallbackAndAllowsRefresh() {
    Fixture fixture;
    fixture.model.start();
    fixture.targetResolutionThrows = true;
    expect(fixture.model.selectCrosshairTarget(), "expected Crosshair selection");
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 1, "expected scheduled resolver");
    fixture.scheduled.front()();
    expect(!fixture.model.isActorTargetResolutionInFlight() &&
            !fixture.model.actorTarget() && fixture.model.error() &&
            fixture.model.error()->code == ServiceErrorCode::actorNotFound &&
            fixture.model.error()->message == "Failed to resolve crosshair Actor.",
        "expected resolver exception delivered as stable actorNotFound error");
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 1 && fixture.queryCount == 0 &&
            fixture.model.selectedTargetKind() == ActorTargetKind::crosshair,
        "expected failed resolution to retain Crosshair mode without Player query");
    fixture.targetResolutionThrows = false;
    expect(fixture.model.refreshCrosshairTarget(), "expected explicit refresh accepted");
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 2, "expected resolver exception to release guard");
    fixture.scheduled.back()();
    expect(fixture.model.actorTarget() && fixture.model.actorTarget()->formId == 0x1234,
        "expected successful explicit retry");
}

void schedulerExceptionFailsWithoutFallbackAndAllowsRefresh() {
    Fixture fixture;
    fixture.model.start();
    fixture.schedulerThrows = true;
    expect(fixture.model.selectCrosshairTarget(), "expected Crosshair selection");
    fixture.runtime.pump();
    expect(!fixture.model.isActorTargetResolutionInFlight() &&
            !fixture.model.actorTarget() && fixture.model.error() &&
            fixture.model.error()->code == ServiceErrorCode::actorNotFound &&
            fixture.model.error()->message == "Failed to resolve crosshair Actor.",
        "expected scheduler exception delivered as stable actorNotFound error");
    fixture.runtime.pump();
    expect(fixture.scheduled.empty() && fixture.targetResolutionCount == 0 &&
            fixture.queryCount == 0 &&
            fixture.model.selectedTargetKind() == ActorTargetKind::crosshair,
        "expected scheduling failure to produce no resolution or Player query");
    expect(fixture.model.refreshCrosshairTarget(), "expected explicit refresh accepted");
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 1, "expected scheduler exception to release guard");
    fixture.scheduled.back()();
    expect(fixture.model.actorTarget() && fixture.model.actorTarget()->formId == 0x1234,
        "expected successful refresh after scheduler failure");
}

void deliversResolverFailureWithoutReplacingItsError() {
    Fixture fixture;
    fixture.model.start();
    fixture.targetResult = std::unexpected(stui::core::ServiceError{
        .code = ServiceErrorCode::actorNotFound, .message = "No valid crosshair Actor"});
    expect(fixture.model.selectCrosshairTarget(), "expected Crosshair selection");
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 1, "expected scheduled resolver");
    fixture.scheduled.front()();
    fixture.runtime.pump();
    expect(!fixture.model.actorTarget() && fixture.model.error() &&
            fixture.model.error()->message == "No valid crosshair Actor" &&
            fixture.queryCount == 0 && fixture.scheduled.size() == 1,
        "expected typed provider failure preserved without fallback query");
}

void ignoresCrosshairCompletionAfterPlayerReselection() {
    for (bool resolverThrows : {false, true}) {
        Fixture fixture;
        fixture.model.start();
        fixture.targetResolutionThrows = resolverThrows;
        expect(fixture.model.selectCrosshairTarget(), "expected Crosshair selection");
        fixture.runtime.pump();
        expect(fixture.scheduled.size() == 1, "expected scheduled Crosshair resolution");
        expect(fixture.model.selectPlayerTarget(), "expected explicit Player reselection");
        fixture.runtime.pump();
        expect(fixture.scheduled.size() == 1, "expected old resolution to retain runtime guard");
        fixture.scheduled.front()();
        expect(fixture.model.actorTarget() && fixture.model.actorTarget()->formId == 0x14 &&
                !fixture.model.error() && !fixture.model.slots(),
            "expected stale resolution success or failure ignored after Player reselection");
        fixture.runtime.pump();
        expect(fixture.scheduled.size() == 2, "expected Player query after stale completion");
        fixture.scheduled.back()();
        expect(fixture.queryCount == 1 && fixture.queriedActor == 0x14 &&
                fixture.model.slots() && fixture.model.slots()->actorFormId == 0x14,
            "expected explicit Player query only after reselection");
    }
}

void ignoresStaleCompletionAfterAReplacementQuery() {
    Fixture fixture;
    fixture.model.start();
    fixture.runtime.pump();
    fixture.model.refreshSelectedArea();

    fixture.scheduled.front()();
    expect(!fixture.model.slots(), "expected stale completion ignored by generation guard");
    fixture.runtime.pump();
    expect(fixture.scheduled.size() == 2,
        "expected replacement query scheduled after old task completes");
    fixture.scheduled.back()();
    expect(fixture.model.slots(), "expected replacement completion accepted");
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
    failures += run("preview schedules once at exact debounce deadline", previewSchedulesOnceAtTheExactDebounceDeadline);
    failures += run("preview observes latest edits before in-flight guard", previewObservesLatestEditsBeforeTheInFlightGuard);
    failures += run("Save flushes pending preview without waiting for clock", saveFlushesPendingPreviewWithoutWaitingForClock);
    failures += run("preview exceptions remain retryable and release guard", previewExceptionsRemainRetryableAndReleaseTheGuard);
    failures += run("preview synchronization retry never requests another write", previewSynchronizationRetryNeverRequestsAnotherWrite);
    failures += run("stale preview completion cannot change replacement session", stalePreviewCompletionCannotChangeAReplacementSession);
    failures += run("schedules one Crosshair resolution before querying resolved Actor",
        schedulesOneCrosshairResolutionBeforeQueryingResolvedActor);
    failures += run("resolver exception fails without fallback and allows refresh",
        resolverExceptionFailsWithoutFallbackAndAllowsRefresh);
    failures += run("scheduler exception fails without fallback and allows refresh",
        schedulerExceptionFailsWithoutFallbackAndAllowsRefresh);
    failures += run("delivers resolver failure without replacing its error",
        deliversResolverFailureWithoutReplacingItsError);
    failures += run("ignores Crosshair completion after Player reselection",
        ignoresCrosshairCompletionAfterPlayerReselection);
    failures += run("schedules only one query and completes model", schedulesOnlyOneQueryAndCompletesModel);
    failures += run("apply schedules only after explicit confirmation", applySchedulesOnlyAfterExplicitConfirmation);
    failures += run("remove schedules only after explicit confirmation", removeSchedulesOnlyAfterExplicitConfirmation);
    failures += run("lock schedules one actor-scoped mutation and refreshes",
        lockSchedulesOneActorScopedMutationAndRefreshes);
    failures += run("converts operation and scheduler exceptions to model errors", convertsOperationAndSchedulerExceptionsToModelErrors);
    failures += run("converts Apply and scheduler exceptions to model errors", convertsApplyAndApplySchedulerExceptionsToModelErrors);
    failures += run("converts Remove exceptions to retryable model errors", convertsRemoveExceptionsToRetryableModelErrors);
    failures += run("schedules full appearance update once and releases in-flight guard", schedulesFullAppearanceUpdateOnceAndReleasesInFlightGuard);
    failures += run("schedules synchronization-only appearance update and releases in-flight guard", schedulesSynchronizeOnlyAppearanceUpdateAndReleasesInFlightGuard);
    failures += run("maps full appearance exceptions and scheduler rejection to updateFailed", mapsFullAppearanceExceptionsAndSchedulerRejectionToUpdateFailed);
    failures += run("maps synchronization-only exceptions and scheduler rejection to synchronizeFailed", mapsSynchronizeOnlyExceptionsAndSchedulerRejectionToSynchronizeFailed);
    failures += run("ignores stale completion after replacement query", ignoresStaleCompletionAfterAReplacementQuery);
    return failures == 0 ? 0 : 1;
}
