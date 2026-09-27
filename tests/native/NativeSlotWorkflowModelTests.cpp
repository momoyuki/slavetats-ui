#include "native/NativeSlotWorkflowModel.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using stui::core::ApplyTattooSuccess;
using stui::core::ApplyTattooMode;
using stui::core::RemoveTattooSuccess;
using stui::core::RemoveTattooMode;
using stui::core::ServiceError;
using stui::core::ServiceErrorCode;
using stui::core::MutationSideEffect;
using stui::core::SetTattooLockedSuccess;
using stui::core::SlotOccupancy;
using stui::core::TattooArea;
using stui::core::TattooEntry;
using stui::core::TattooSlot;
using stui::core::TattooSlots;
using stui::core::UpdateTattooAppearanceMode;
using stui::core::UpdateTattooAppearanceSuccess;
using stui::native::ActorTarget;
using stui::native::ActorTargetKind;
using stui::native::AppearanceOperationPurpose;
using stui::native::LivePreviewStatus;
using stui::native::NativeCatalogBrowserModel;
using stui::native::NativeSlotWorkflowModel;
using stui::native::SlotWorkflowScreen;
using stui::repository::TattooCatalog;
using stui::repository::TattooCatalogSnapshot;
using stui::repository::TattooDefinition;
using namespace std::chrono_literals;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

TattooDefinition tattoo(std::string name, std::size_t index, std::string area = "BODY") {
    return TattooDefinition{
        .sourceId = "source.json",
        .sourceFile = "source.json",
        .packName = "Fixture Pack",
        .sourceIndex = index,
        .name = std::move(name),
        .section = "Marks",
        .texturePath = "marks/fixture.dds",
        .area = std::move(area),
    };
}

TattooCatalogSnapshot catalogWithEntries(std::size_t count) {
    std::vector<TattooDefinition> definitions;
    definitions.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        definitions.push_back(tattoo("Entry " + std::to_string(index), index));
    }
    return std::make_shared<const TattooCatalog>(TattooCatalog{
        .repository = stui::repository::TattooRepository(std::move(definitions)),
        .sourceCount = 1,
    });
}

TattooSlots slots(TattooArea area, int count) {
    TattooSlots result{
        .actorFormId = 0x14,
        .area = area,
        .configuredCount = count,
    };
    for (int index = 0; index < count; ++index) {
        result.slots.push_back(TattooSlot{
            .index = index,
            .occupancy = SlotOccupancy::empty,
        });
    }
    return result;
}

void completeInitialQuery(NativeSlotWorkflowModel& model, TattooSlots result) {
    model.start();
    const auto ticket = model.takeSlotQuery();
    expect(ticket.has_value(), "expected initial slot query ticket");
    model.completeSlotQuery(ticket->generation, std::move(result));
}

TattooSlots slotsWithEditableOwnedTattoo() {
    auto result = slots(TattooArea::body, 4);
    result.slots[1].occupancy = SlotOccupancy::slaveTats;
    result.slots[1].tattoo = TattooEntry{
        .runtimeHandle = 73,
        .section = "Marks",
        .name = "Existing",
        .texturePath = "marks/existing.dds",
        .area = "BODY",
        .slot = 1,
        .color = 0x2468AC,
        .alpha = 0.42F,
        .glow = 0x102030,
        .glossiness = 2.5F,
        .specularStrength = 1.25F,
        .bump = "marks/existing_n.dds",
        .glowTexture = "marks/existing_g.dds",
        .emissiveMult = 3.0F,
    };
    return result;
}

void startSchedulesOnePlayerBodyQuery() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);

    model.start();
    const auto initial = model.takeSlotQuery();

    expect(initial && initial->actorFormId == 0x14 && initial->area == TattooArea::body,
        "expected one initial Player BODY query");
    expect(!model.takeSlotQuery(), "expected repeated reads not to query again");
    model.start();
    expect(!model.takeSlotQuery(), "expected repeated start not to query again");
    expect(model.screen() == SlotWorkflowScreen::currentSlots,
        "expected Current Slots initial screen");
}

void cachesAreaResultsAndPreservesPerAreaPages() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slots(TattooArea::body, 12));

    expect(model.slotPageCount() == 2 && model.slotPageIndex() == 0,
        "expected two six-slot BODY pages");
    model.nextSlotPage();
    expect(model.slotPageIndex() == 1, "expected BODY second page");

    model.selectArea(TattooArea::face);
    const auto faceTicket = model.takeSlotQuery();
    expect(faceTicket && faceTicket->area == TattooArea::face,
        "expected uncached FACE query");
    model.completeSlotQuery(faceTicket->generation, slots(TattooArea::face, 3));
    expect(model.slotPageCount() == 1 && model.slotPageIndex() == 0,
        "expected one FACE page");

    model.selectArea(TattooArea::body);
    expect(!model.takeSlotQuery(), "expected cached BODY area not queried again");
    expect(model.slotPageIndex() == 1, "expected BODY page restored");
    model.selectArea(TattooArea::face);
    expect(model.slotPageIndex() == 0, "expected FACE page restored");
}

void clampsSlotPagination() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slots(TattooArea::body, 13));

    model.previousSlotPage();
    expect(model.slotPageIndex() == 0, "expected previous clamped at first slot page");
    model.setSlotPageNumber(99);
    expect(model.slotPageIndex() == 2, "expected one-based slot page clamped at final page");
    model.nextSlotPage();
    expect(model.slotPageIndex() == 2, "expected next clamped at final slot page");
    model.setSlotPageNumber(0);
    expect(model.slotPageIndex() == 0, "expected zero page input clamped to first slot page");
}

void externalSlotsAreRejectedAndOwnedSlotsOpenActions() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    auto bodySlots = slots(TattooArea::body, 3);
    bodySlots.slots[0].occupancy = SlotOccupancy::external;
    bodySlots.slots[1].occupancy = SlotOccupancy::slaveTats;
    bodySlots.slots[1].tattoo = TattooEntry{
        .section = "Existing",
        .name = "Owned",
        .area = "BODY",
        .slot = 1,
    };
    completeInitialQuery(model, std::move(bodySlots));

    expect(!model.selectSlot(0), "expected external slot selection rejected");
    expect(model.screen() == SlotWorkflowScreen::currentSlots,
        "expected external slot to remain on Current Slots");
    expect(model.selectSlot(1), "expected owned slot replace target accepted");
    expect(model.screen() == SlotWorkflowScreen::slotActions && model.targetSlot() == 1,
        "expected owned slot to open Slot Actions");
    expect(model.replaceSelectedSlot(), "expected Replace action accepted");
    expect(model.screen() == SlotWorkflowScreen::picker,
        "expected Replace action to open Picker");

    model.backToSlots();
    expect(model.selectSlot(2), "expected empty slot target accepted");
    expect(model.screen() == SlotWorkflowScreen::picker && model.targetSlot() == 2,
        "expected empty slot to open Picker");
}

void removeRequiresConfirmationAndCreatesOneRequest() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    auto bodySlots = slots(TattooArea::body, 3);
    bodySlots.slots[1].occupancy = SlotOccupancy::slaveTats;
    bodySlots.slots[1].tattoo = TattooEntry{
        .section = "Existing",
        .name = "Owned",
        .area = "BODY",
        .slot = 1,
    };
    completeInitialQuery(model, std::move(bodySlots));
    expect(model.selectSlot(1), "expected owned slot selected");

    expect(model.requestRemove(), "expected Remove action accepted");
    expect(model.screen() == SlotWorkflowScreen::removeConfirmation,
        "expected explicit Remove confirmation screen");
    expect(!model.takeRemoveRequest(), "expected no mutation before confirmation");
    model.cancelRemove();
    expect(model.screen() == SlotWorkflowScreen::slotActions && model.targetSlot() == 1,
        "expected Cancel to return to Slot Actions");

    expect(model.requestRemove(), "expected Remove action accepted again");
    expect(model.confirmRemove(), "expected first Remove confirmation accepted");
    expect(!model.confirmRemove(), "expected duplicate Remove confirmation rejected");
    const auto ticket = model.takeRemoveRequest();

    expect(ticket.has_value(), "expected one remove ticket");
    expect(ticket->request.actorFormId == 0x14 &&
            ticket->request.area == TattooArea::body &&
            ticket->request.slot == 1 &&
            ticket->request.mode == RemoveTattooMode::removeAndSynchronize,
        "expected exact Player BODY slot remove target");
    expect(model.screen() == SlotWorkflowScreen::removing,
        "expected Removing state after confirmation");
    expect(!model.takeRemoveRequest(), "expected remove ticket consumed once");
}

void removeCompletionRefreshesOrRetainsConfirmationForRetry() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    auto bodySlots = slots(TattooArea::body, 3);
    bodySlots.slots[1].occupancy = SlotOccupancy::slaveTats;
    bodySlots.slots[1].tattoo = TattooEntry{
        .section = "Existing",
        .name = "Owned",
        .area = "BODY",
        .slot = 1,
    };
    completeInitialQuery(model, std::move(bodySlots));
    expect(model.selectSlot(1) && model.requestRemove() && model.confirmRemove(),
        "expected confirmed Remove flow");
    const auto failed = model.takeRemoveRequest();

    model.completeRemove(failed->generation, std::unexpected(ServiceError{
        ServiceErrorCode::synchronizeFailed,
        "remove sync failed",
    }));

    expect(model.screen() == SlotWorkflowScreen::removeConfirmation &&
            model.targetSlot() == 1,
        "expected failed Remove to retain target and confirmation");
    expect(model.error() && model.error()->message == "remove sync failed",
        "expected Remove failure exposed");
    expect(model.confirmRemove(), "expected failed Remove retry accepted");
    const auto retry = model.takeRemoveRequest();
    expect(retry && retry->generation > failed->generation,
        "expected retry to use a newer generation");
    expect(retry->request.mode == RemoveTattooMode::synchronizeOnly,
        "expected post-mutation failure to retry synchronization without removing again");

    model.completeRemove(retry->generation, RemoveTattooSuccess{
        .actorFormId = 0x14,
        .area = TattooArea::body,
        .slot = 1,
    });

    expect(model.screen() == SlotWorkflowScreen::currentSlots && !model.targetSlot(),
        "expected successful Remove to return to Current Slots");
    const auto refresh = model.takeSlotQuery();
    expect(refresh && refresh->area == TattooArea::body,
        "expected successful Remove to refresh selected area");
}

void emptyAndOwnedTargetsInitializeExpectedAppearance() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    auto bodySlots = slots(TattooArea::body, 3);
    bodySlots.slots[1].occupancy = SlotOccupancy::slaveTats;
    bodySlots.slots[1].tattoo = TattooEntry{
        .section = "Existing",
        .name = "Owned",
        .area = "BODY",
        .slot = 1,
        .color = 0x2468AC,
        .alpha = 0.42F,
    };
    completeInitialQuery(model, std::move(bodySlots));

    expect(model.selectSlot(1) && model.replaceSelectedSlot(),
        "expected owned slot Replace flow");
    const auto* ownedAppearance = model.previewAppearance();
    expect(ownedAppearance && ownedAppearance->color == 0x2468AC &&
            ownedAppearance->alpha == 0.42F,
        "expected Replace to preserve the occupied tattoo appearance");

    model.backToSlots();
    expect(model.selectSlot(2), "expected empty slot Add flow");
    const auto* emptyAppearance = model.previewAppearance();
    expect(emptyAppearance && emptyAppearance->color == 0xFFFFFF &&
            emptyAppearance->alpha == 1.0F,
        "expected empty slot to start white and opaque");
}

void editedAppearanceFlowsIntoApplyRequest() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slots(TattooArea::body, 3));
    expect(model.selectSlot(2), "expected empty target selected");
    model.selectTattoo(tattoo("Corruption", 7));

    model.setPreviewAppearance(0x123456, 0.35F);
    expect(model.confirmApply(), "expected Apply confirmation");
    const auto ticket = model.takeApplyRequest();

    expect(ticket && ticket->request.color == 0x123456 &&
            ticket->request.alpha == 0.35F,
        "expected edited color and alpha in the exact Apply request");
}

void rejectsTattooOutsideTheSelectedArea() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slots(TattooArea::body, 3));
    expect(model.selectSlot(2), "expected empty Body target selected");

    model.selectTattoo(tattoo("Face Mark", 9, "Face"));
    expect(model.screen() == SlotWorkflowScreen::picker && !model.previewTattoo(),
        "expected Face tattoo rejected for a Body target");

    model.selectTattoo(tattoo("Body Mark", 10, "body"));
    expect(model.screen() == SlotWorkflowScreen::preview && model.previewTattoo(),
        "expected case-insensitive Body tattoo accepted for a Body target");
}

void findsInUseSlotsBySlaveTatsTattooIdentity() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    auto bodySlots = slots(TattooArea::body, 4);
    for (const int index : {0, 2}) {
        bodySlots.slots[index].occupancy = SlotOccupancy::slaveTats;
        bodySlots.slots[index].tattoo = TattooEntry{
            .section = "Marks",
            .name = "Corruption",
            .area = "BODY",
            .slot = index,
        };
    }
    bodySlots.slots[1].occupancy = SlotOccupancy::slaveTats;
    bodySlots.slots[1].tattoo = TattooEntry{
        .section = "Marks",
        .name = "Different",
        .area = "BODY",
        .slot = 1,
    };
    bodySlots.slots[3].occupancy = SlotOccupancy::external;
    bodySlots.slots[3].tattoo = TattooEntry{
        .section = "Marks",
        .name = "Corruption",
        .area = "BODY",
        .slot = 3,
    };
    completeInitialQuery(model, std::move(bodySlots));

    expect(model.inUseSlots(tattoo("Corruption", 7)) == std::vector<std::int32_t>{0, 2},
        "expected exact identity matches from SlaveTats-managed slots only");
    expect(model.inUseSlots(tattoo("corruption", 8)).empty(),
        "expected runtime-exact Tattoo Identity matching");
}

void filtersPickerToAppliedTattoosAndPreservesToggleAcrossNavigation() {
    TattooCatalogSnapshot snapshot = std::make_shared<const TattooCatalog>(TattooCatalog{
        .repository = stui::repository::TattooRepository({
            tattoo("Applied A", 0),
            tattoo("Unused", 1),
            tattoo("External", 2),
        }),
        .sourceCount = 1,
    });
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    auto bodySlots = slots(TattooArea::body, 4);
    bodySlots.slots[0].occupancy = SlotOccupancy::slaveTats;
    bodySlots.slots[0].tattoo = TattooEntry{
        .section = "Marks",
        .name = "Applied A",
        .area = "BODY",
        .slot = 0,
    };
    bodySlots.slots[1].occupancy = SlotOccupancy::external;
    bodySlots.slots[1].tattoo = TattooEntry{
        .section = "Marks",
        .name = "External",
        .area = "BODY",
        .slot = 1,
    };
    completeInitialQuery(model, std::move(bodySlots));
    expect(model.selectSlot(2), "expected empty slot to open Picker");

    model.setAppliedOnly(true);

    expect(model.appliedOnly(), "expected Applied-only toggle enabled");
    expect(catalog.page().matchedEntries == 1 &&
            catalog.page().entries.front().name == "Applied A",
        "expected Picker to include only SlaveTats-managed applied identities");

    model.backToSlots();
    expect(model.selectSlot(3), "expected another empty slot to reopen Picker");
    expect(model.appliedOnly() && catalog.page().matchedEntries == 1,
        "expected Applied-only state preserved across Picker navigation");
}

void previewDoesNotApplyAndCancelReturnsToSlots() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(13);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    catalog.setSearch("Entry");
    catalog.setSourceId("source.json");
    catalog.setSection("Marks");
    catalog.setArea("BODY");
    catalog.setPageNumber(2);
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slots(TattooArea::body, 3));
    expect(model.selectSlot(2), "expected empty target selected");
    const auto filterBefore = catalog.filter();
    const auto pageBefore = catalog.page().pageIndex;
    const auto selected = catalog.page().entries.front();

    model.selectTattoo(selected);

    expect(model.screen() == SlotWorkflowScreen::preview,
        "expected thumbnail selection to enter Preview");
    expect(model.previewTattoo() && model.previewTattoo()->name == selected.name,
        "expected copied preview tattoo");
    expect(!model.takeApplyRequest(), "expected no mutation before explicit confirmation");

    model.cancelPreview();
    expect(model.screen() == SlotWorkflowScreen::currentSlots &&
            !model.targetSlot() && !model.previewTattoo() && !model.previewAppearance(),
        "expected Cancel to discard Preview state and return to Current Slots");
    expect(!model.takeApplyRequest(),
        "expected Cancel to return to Current Slots without creating an Apply request");
    expect(catalog.filter().search == filterBefore.search &&
            catalog.filter().sourceId == filterBefore.sourceId &&
            catalog.filter().section == filterBefore.section &&
            catalog.filter().area == filterBefore.area &&
            catalog.page().pageIndex == pageBefore,
        "expected Cancel to preserve picker filters and page for the next target");
}

void previewBackReturnsToPickerAndKeepsApplyingState() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(2);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slots(TattooArea::body, 3));
    expect(model.selectSlot(2), "expected empty target selected");
    const auto first = catalog.page().entries[0];
    const auto replacement = catalog.page().entries[1];
    model.selectTattoo(first);

    model.backToPicker();

    expect(model.screen() == SlotWorkflowScreen::picker &&
            model.targetSlot() == std::optional<std::int32_t>{2} &&
            !model.previewTattoo() && model.previewAppearance(),
        "expected Back to preserve the selected slot and appearance for another Picker choice");
    model.selectTattoo(replacement);
    expect(model.confirmApply() && model.takeApplyRequest(),
        "expected selecting another Tattoo after Back to create an Apply request");
}

void explicitConfirmationCreatesOneExactDomainPolicyRequest() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slots(TattooArea::body, 3));
    expect(model.selectSlot(2), "expected apply target selected");
    auto selected = tattoo("Corruption", 7);
    selected.domain = "custom";
    model.selectTattoo(selected);

    expect(model.confirmApply(), "expected first Apply confirmation accepted");
    expect(!model.confirmApply(), "expected duplicate Apply confirmation rejected");
    const auto ticket = model.takeApplyRequest();

    expect(ticket.has_value(), "expected one apply ticket");
    expect(ticket->request.actorFormId == 0x14 && ticket->request.area == TattooArea::body &&
            ticket->request.slot == 2,
        "expected Player BODY slot target");
    expect(ticket->request.domain == "custom" && ticket->request.section == "Marks" &&
            ticket->request.name == "Corruption",
        "expected selected tattoo identity with exact domain");
    expect(ticket->request.color == 0xFFFFFF && ticket->request.alpha == 1.0F,
        "expected fixed white opaque apply policy");
    expect(model.screen() == SlotWorkflowScreen::applying,
        "expected Applying state after confirmation");
    expect(!model.takeApplyRequest(), "expected apply ticket consumed once");
}

void applySuccessReturnsToSlotsAndRefreshesArea() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(13);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    catalog.setSearch("Entry");
    catalog.setPageNumber(2);
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slots(TattooArea::body, 3));
    expect(model.selectSlot(2), "expected success-flow target selected");
    const auto filterBefore = catalog.filter();
    const auto pageBefore = catalog.page().pageIndex;
    model.selectTattoo(catalog.page().entries.front());
    expect(model.confirmApply(), "expected success-flow confirmation accepted");
    const auto apply = model.takeApplyRequest();

    model.completeApply(apply->generation, ApplyTattooSuccess{
        .actorFormId = 0x14,
        .area = TattooArea::body,
        .slot = 2,
        .section = "Marks",
        .name = "Entry 6",
    });

    expect(model.screen() == SlotWorkflowScreen::currentSlots,
        "expected success to return to Current Slots");
    const auto refresh = model.takeSlotQuery();
    expect(refresh && refresh->area == TattooArea::body,
        "expected success to refresh selected area");
    expect(catalog.filter().search == filterBefore.search &&
            catalog.filter().sourceId == filterBefore.sourceId &&
            catalog.filter().section == filterBefore.section &&
            catalog.filter().area == filterBefore.area &&
            catalog.page().pageIndex == pageBefore,
        "expected Apply success to preserve picker state");
}

void applyFailureRetainsPreviewForRetry() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slots(TattooArea::body, 3));
    expect(model.selectSlot(2), "expected failure-flow target selected");
    model.selectTattoo(tattoo("Corruption", 7));
    expect(model.confirmApply(), "expected failure-flow confirmation accepted");
    const auto first = model.takeApplyRequest();

    model.completeApply(first->generation, std::unexpected(ServiceError{
        ServiceErrorCode::applyFailed,
        "apply failed",
    }));

    expect(model.screen() == SlotWorkflowScreen::preview,
        "expected apply failure to return to Preview");
    expect(model.previewTattoo() && model.targetSlot() == 2,
        "expected failed preview and target retained");
    expect(model.error() && model.error()->message == "apply failed",
        "expected apply error exposed");
    expect(model.confirmApply(), "expected retry confirmation accepted");
    const auto retry = model.takeApplyRequest();
    expect(retry && retry->generation > first->generation,
        "expected retry to use a newer generation");
}

void applySynchronizationFailureRetriesWithoutRepeatingMutation() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slots(TattooArea::body, 3));
    expect(model.selectSlot(2), "expected synchronization-failure target selected");
    model.selectTattoo(tattoo("Corruption", 7));
    expect(model.confirmApply(), "expected initial Apply accepted");
    const auto first = model.takeApplyRequest();
    expect(first && first->request.mode == ApplyTattooMode::applyAndSynchronize,
        "expected initial Apply to mutate and synchronize");

    model.completeApply(first->generation, std::unexpected(ServiceError{
        ServiceErrorCode::synchronizeFailed,
        "apply sync failed",
        MutationSideEffect::mayHaveOccurred,
    }));

    expect(model.screen() == SlotWorkflowScreen::preview && model.previewTattoo(),
        "expected synchronization failure to retain Preview for retry");
    expect(model.confirmApply(), "expected synchronization-only Apply retry accepted");
    const auto retry = model.takeApplyRequest();
    expect(retry && retry->generation > first->generation &&
            retry->request.mode == ApplyTattooMode::synchronizeOnly,
        "expected retry to synchronize without repeating Apply mutation");

    model.completeApply(retry->generation, ApplyTattooSuccess{});
    expect(model.screen() == SlotWorkflowScreen::currentSlots,
        "expected successful synchronization retry to finish Apply");
}

void recentHistoryLoadsAndRecordsOnlySuccessfulApply() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);

    model.initializeRecentTattoos();
    const auto load = model.takeRecentTattooRequest();
    expect(load && load->kind == stui::native::RecentTattooRequestKind::load &&
            !load->identity,
        "expected one explicit Recently Used load request");
    const auto stored = stui::repository::recentTattooIdentity(
        tattoo("Stored", 5), TattooArea::body);
    model.completeRecentTattoo(load->requestId, stui::runtime::RecentTattooList{stored});
    model.setRecentlyUsedOnly(true);
    expect(model.recentlyUsedOnly(), "expected Recently Used filter available after load");
    model.setRecentlyUsedOnly(false);

    completeInitialQuery(model, slots(TattooArea::body, 3));
    expect(model.selectSlot(2), "expected Apply target");
    const auto selected = tattoo("Corruption", 7);
    model.selectTattoo(selected);
    expect(model.confirmApply(), "expected Apply request");
    const auto apply = model.takeApplyRequest();
    expect(!model.takeRecentTattooRequest(), "expected no history before Apply success");
    model.completeApply(apply->generation, ApplyTattooSuccess{});
    const auto record = model.takeRecentTattooRequest();
    expect(record && record->kind == stui::native::RecentTattooRequestKind::record &&
            record->identity == stui::repository::recentTattooIdentity(
                selected, TattooArea::body),
        "expected exact copied identity only after Apply success");
}

void failedRecentHistoryWriteRetriesWithoutChangingTattooResult() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slots(TattooArea::body, 3));
    expect(model.selectSlot(2), "expected Apply target");
    model.selectTattoo(tattoo("Corruption", 7));
    expect(model.confirmApply(), "expected Apply request");
    const auto apply = model.takeApplyRequest();
    model.completeApply(apply->generation, ApplyTattooSuccess{});
    const auto record = model.takeRecentTattooRequest();

    model.completeRecentTattoo(record->requestId, std::unexpected(stui::runtime::ConfigError{
        .message = "history write failed"}));

    expect(model.screen() == SlotWorkflowScreen::currentSlots && !model.error() &&
            model.recentlyUsedError() && model.recentlyUsedError()->message == "history write failed",
        "expected Apply success preserved with separate history error");
    expect(model.retryRecentTattoo(), "expected explicit history retry");
    const auto retry = model.takeRecentTattooRequest();
    expect(retry && retry->requestId > record->requestId &&
            retry->kind == stui::native::RecentTattooRequestKind::record &&
            retry->identity == record->identity,
        "expected fresh history-only retry for the same identity");
}

void staleCompletionsAreIgnored() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    model.start();
    const auto body = model.takeSlotQuery();
    model.selectArea(TattooArea::face);
    const auto face = model.takeSlotQuery();

    model.completeSlotQuery(body->generation, slots(TattooArea::body, 12));
    expect(model.slots() == nullptr, "expected stale BODY completion ignored while FACE selected");
    model.completeSlotQuery(face->generation, slots(TattooArea::face, 3));
    expect(model.slots() && model.slots()->area == TattooArea::face,
        "expected current FACE completion accepted");

    expect(model.selectSlot(1), "expected FACE target selected");
    model.selectTattoo(tattoo("Face Mark", 1, "FACE"));
    expect(model.confirmApply(), "expected stale-completion confirmation accepted");
    const auto apply = model.takeApplyRequest();
    model.completeApply(apply->generation + 1, ApplyTattooSuccess{});
    expect(model.screen() == SlotWorkflowScreen::applying,
        "expected stale apply completion ignored");
}

void queryFailureRemainsRetryable() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    model.start();
    const auto query = model.takeSlotQuery();

    model.completeSlotQuery(query->generation, std::unexpected(ServiceError{
        ServiceErrorCode::jContainersUnavailable,
        "JContainers not ready",
    }));

    expect(model.error() && model.error()->code == ServiceErrorCode::jContainersUnavailable,
        "expected query failure exposed");
    model.refreshSelectedArea();
    const auto retry = model.takeSlotQuery();
    expect(retry && retry->generation > query->generation,
        "expected Refresh to create a newer query generation");
}

void editAppearanceRequiresOwnedSlotWithHandleAndCopiesSnapshot() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    auto bodySlots = slotsWithEditableOwnedTattoo();
    bodySlots.slots[2].occupancy = SlotOccupancy::external;
    bodySlots.slots[3].occupancy = SlotOccupancy::slaveTats;
    bodySlots.slots[3].tattoo = *bodySlots.slots[1].tattoo;
    bodySlots.slots[3].tattoo->runtimeHandle = 0;
    bodySlots.slots[3].tattoo->slot = 3;
    completeInitialQuery(model, std::move(bodySlots));

    expect(model.selectSlot(0), "expected empty slot selection accepted for Add flow");
    expect(!model.beginEditAppearance(), "expected empty slot edit rejected");
    model.backToSlots();
    expect(!model.selectSlot(2), "expected external slot selection rejected");
    expect(model.selectSlot(3), "expected zero-handle owned slot action selected");
    expect(!model.beginEditAppearance(), "expected zero-handle slot edit rejected");
    model.backToSlots();
    expect(model.selectSlot(1) && model.beginEditAppearance(),
        "expected owned slot edit accepted");

    const auto* session = model.editAppearance();
    expect(model.screen() == SlotWorkflowScreen::editAppearance && session,
        "expected Edit Appearance screen with a session");
    expect(session->actorFormId == 0x14 && session->area == TattooArea::body &&
            session->slot == 1 && session->runtimeHandle == 73 &&
            session->texturePath == "marks/existing.dds" &&
            session->glowTexture == "marks/existing_g.dds" &&
            session->bump == "marks/existing_n.dds",
        "expected session identity copied from the selected slot snapshot");
    expect(session->original.color == 0x2468AC && session->original.alpha == 0.42F &&
            session->original.glow == 0x102030 && session->original.glossiness == 2.5F &&
            session->original.specularStrength == 1.25F &&
            session->original.emissiveMult == 3.0F &&
            session->edited.color == 0x2468AC && session->edited.alpha == 0.42F &&
            session->edited.glow == 0x102030 && session->edited.glossiness == 2.5F &&
            session->edited.specularStrength == 1.25F && session->edited.emissiveMult == 3.0F &&
            session->mode == UpdateTattooAppearanceMode::updateAndSynchronize,
        "expected original and edited appearance initialized from the snapshot");
    expect(!model.canSaveAppearance(), "expected unchanged appearance Save disabled");
    expect(!model.takeAppearanceRequest(), "expected editor entry not to create a ticket");
}

void localAppearanceEditsNormalizeTrackDirtyAndCancelWithoutTicket() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slotsWithEditableOwnedTattoo());
    expect(model.selectSlot(1) && model.beginEditAppearance(),
        "expected edit session opened");

    model.setEditedAppearance(-1, 1.25F);
    const auto* normalized = model.editAppearance();
    expect(normalized && normalized->edited.color == 0 && normalized->edited.alpha == 1.0F,
        "expected editor values clamped to supported color and alpha ranges");
    expect(model.canSaveAppearance(), "expected normalized changed appearance to enable Save");
    expect(!model.takeAppearanceRequest(), "expected local edits not to create a ticket");

    model.setEditedAppearance(0x2468AC, 0.42F);
    expect(!model.canSaveAppearance(), "expected original appearance to disable Save again");
    model.setEditedAppearance(0x123456, 0.35F, 0xABCDEF, 4.0F, 2.0F, 5.0F);
    model.cancelEditAppearance();
    expect(model.screen() == SlotWorkflowScreen::slotActions && !model.editAppearance(),
        "expected Cancel to discard the session and return to Slot Actions");
    expect(!model.takeAppearanceRequest(), "expected Cancel not to create a ticket");
}

void advancedAppearanceEditsTrackDirtyNormalizeAndForwardAllValues() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slotsWithEditableOwnedTattoo());
    expect(model.selectSlot(1) && model.beginEditAppearance(),
        "expected edit session opened");

    model.setEditedAppearance(0x2468AC, 0.42F, 0x302010, 2.5F, 1.25F, 3.0F);
    expect(model.canSaveAppearance(), "expected changing Glow Color to enable Save");
    model.setEditedAppearance(0x2468AC, 0.42F, 0x102030, 2.5F, 1.25F, 3.0F);
    expect(!model.canSaveAppearance(), "expected restoring Glow Color to disable Save");

    model.setEditedAppearance(0x2468AC, 0.42F, 0x102030, 4.0F, 1.25F, 3.0F);
    expect(model.canSaveAppearance(), "expected changing Glossiness to enable Save");
    model.setEditedAppearance(0x2468AC, 0.42F, 0x102030, 2.5F, 1.25F, 3.0F);
    expect(!model.canSaveAppearance(), "expected restoring Glossiness to disable Save");

    model.setEditedAppearance(0x2468AC, 0.42F, 0x102030, 2.5F, 2.0F, 3.0F);
    expect(model.canSaveAppearance(), "expected changing Specular Strength to enable Save");
    model.setEditedAppearance(0x2468AC, 0.42F, 0x102030, 2.5F, 1.25F, 3.0F);
    expect(!model.canSaveAppearance(), "expected restoring Specular Strength to disable Save");

    model.setEditedAppearance(0x2468AC, 0.42F, 0x102030, 2.5F, 1.25F, 4.0F);
    expect(model.canSaveAppearance(), "expected changing Emission Strength to enable Save");
    model.setEditedAppearance(0x2468AC, 0.42F, 0x102030, 2.5F, 1.25F, 3.0F);
    expect(!model.canSaveAppearance(), "expected restoring Emission Strength to disable Save");

    model.setEditedAppearance(0x123456, 0.35F, 0xABCDEF, 4.0F, 2.0F, 5.0F);
    const auto* edited = model.editAppearance();
    expect(edited && edited->edited.color == 0x123456 && edited->edited.alpha == 0.35F &&
            edited->edited.glow == 0xABCDEF && edited->edited.glossiness == 4.0F &&
            edited->edited.specularStrength == 2.0F && edited->edited.emissiveMult == 5.0F,
        "expected every editable appearance value retained");
    model.setEditedAppearance(
        0x123456,
        0.35F,
        0xABCDEF,
        -1.0F,
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN());
    edited = model.editAppearance();
    expect(edited && edited->edited.glossiness == 4.0F && edited->edited.specularStrength == 2.0F &&
            edited->edited.emissiveMult == 5.0F,
        "expected invalid material values to retain the prior editable values");
    expect(model.confirmAppearanceUpdate(), "expected changed advanced appearance Save accepted");
    const auto ticket = model.takeAppearanceRequest();
    expect(ticket && ticket->request.color == 0x123456 && ticket->request.alpha == 0.35F &&
            ticket->request.glow == 0xABCDEF && ticket->request.glossiness == 4.0F &&
            ticket->request.specularStrength == 2.0F && ticket->request.emissiveMult == 5.0F,
        "expected full update ticket to forward every editable appearance value");
}

void lockTicketRefreshesSnapshotAndGuardsLockedMutations() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slotsWithEditableOwnedTattoo());

    expect(model.selectSlot(1), "expected owned slot selected for lock");
    expect(model.toggleSelectedSlotLock(), "expected Lock accepted");
    const auto lock = model.takeLockRequest();
    expect(lock && lock->request.actorFormId == 0x14 && lock->request.runtimeHandle == 73 &&
            lock->request.locked && model.isLockStateChangeInFlight(),
        "expected Lock ticket scoped to selected owned tattoo");
    expect(!model.replaceSelectedSlot() && !model.requestRemove() && !model.beginEditAppearance(),
        "expected pending Lock to prevent actions against a stale slot snapshot");

    model.completeLockStateChange(lock->generation, SetTattooLockedSuccess{});
    const auto refresh = model.takeSlotQuery();
    expect(refresh && refresh->area == TattooArea::body &&
            model.screen() == SlotWorkflowScreen::currentSlots,
        "expected successful Lock to refresh selected area");
    auto lockedSlots = slotsWithEditableOwnedTattoo();
    lockedSlots.slots[1].tattoo->locked = true;
    model.completeSlotQuery(refresh->generation, std::move(lockedSlots));

    expect(model.selectSlot(1), "expected locked owned slot selected");
    expect(!model.replaceSelectedSlot() && !model.requestRemove(),
        "expected locked slot to reject Replace and Remove");
    expect(model.beginEditAppearance(), "expected locked slot to allow Edit Appearance");
    model.cancelEditAppearance();
    expect(model.toggleSelectedSlotLock(), "expected Unlock accepted");
    const auto unlock = model.takeLockRequest();
    expect(unlock && !unlock->request.locked,
        "expected Unlock ticket to clear the persisted lock state");

    model.completeLockStateChange(unlock->generation, std::unexpected(ServiceError{
        ServiceErrorCode::lockFailed,
        "lock write failed",
    }));
    expect(model.screen() == SlotWorkflowScreen::slotActions && model.error() &&
            model.error()->code == ServiceErrorCode::lockFailed && !model.isLockStateChangeInFlight(),
        "expected failed Unlock to retain Slot Actions for retry");
}

void currentSlotLockToggleDoesNotNavigateToSlotActions() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slotsWithEditableOwnedTattoo());

    expect(model.toggleSlotLock(1), "expected Current Slots lock icon accepted");
    const auto lock = model.takeLockRequest();
    expect(lock && lock->request.runtimeHandle == 73 && lock->request.locked,
        "expected thumbnail Lock action to create the owned-slot request");
    expect(model.screen() == SlotWorkflowScreen::currentSlots,
        "expected thumbnail Lock action to stay on Current Slots");

    model.completeLockStateChange(lock->generation, std::unexpected(ServiceError{
        ServiceErrorCode::lockFailed,
        "lock write failed",
    }));
    expect(model.screen() == SlotWorkflowScreen::currentSlots && model.error() &&
            model.error()->code == ServiceErrorCode::lockFailed,
        "expected thumbnail Lock failure to remain retryable on Current Slots");
    expect(!model.toggleSlotLock(0),
        "expected empty slot to reject direct Lock action");
}

void changingAreaInvalidatesLockCompletion() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slotsWithEditableOwnedTattoo());
    expect(model.selectSlot(1) && model.toggleSelectedSlotLock(),
        "expected lock request before area navigation");
    const auto lock = model.takeLockRequest();
    expect(lock.has_value(), "expected pending lock ticket");

    model.selectArea(TattooArea::face);
    const auto faceQuery = model.takeSlotQuery();
    expect(faceQuery && faceQuery->area == TattooArea::face,
        "expected FACE query after area navigation");
    model.completeLockStateChange(lock->generation, SetTattooLockedSuccess{});

    expect(model.selectedArea() == TattooArea::face &&
            model.screen() == SlotWorkflowScreen::currentSlots && !model.error() &&
            !model.takeSlotQuery(),
        "expected obsolete Lock completion to leave navigated workflow unchanged");
}

void appearanceSaveCreatesOneTicketAndSuccessRefreshesOnlyBody() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slotsWithEditableOwnedTattoo());
    expect(model.selectSlot(1) && model.beginEditAppearance(),
        "expected edit session opened");
    model.setEditedAppearance(0x123456, 0.35F);

    expect(model.confirmAppearanceUpdate(), "expected changed appearance Save accepted");
    expect(!model.confirmAppearanceUpdate(), "expected duplicate appearance Save rejected");
    const auto ticket = model.takeAppearanceRequest();
    expect(ticket && ticket->request.actorFormId == 0x14 && ticket->request.runtimeHandle == 73 &&
            ticket->request.color == 0x123456 && ticket->request.alpha == 0.35F &&
            ticket->request.mode == UpdateTattooAppearanceMode::updateAndSynchronize,
        "expected full update ticket to use the edited session values");
    expect(model.screen() == SlotWorkflowScreen::savingAppearance && !model.canSaveAppearance(),
        "expected Saving Appearance to prevent a duplicate Save");
    expect(!model.takeAppearanceRequest(), "expected appearance ticket consumed once");

    model.completeAppearanceUpdate(ticket->generation + 1, UpdateTattooAppearanceSuccess{});
    expect(model.screen() == SlotWorkflowScreen::savingAppearance && model.editAppearance(),
        "expected stale appearance completion ignored");
    model.completeAppearanceUpdate(ticket->generation, UpdateTattooAppearanceSuccess{
        .actorFormId = 0x14,
        .runtimeHandle = 73,
    });
    expect(model.screen() == SlotWorkflowScreen::currentSlots && !model.editAppearance(),
        "expected successful appearance save to clear the session and return to Current Slots");
    const auto refresh = model.takeSlotQuery();
    expect(refresh && refresh->area == TattooArea::body,
        "expected successful appearance save to refresh only selected BODY slots");
}

void appearanceTransactionBlocksAreaNavigationUntilCompletion() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slotsWithEditableOwnedTattoo());
    expect(model.selectSlot(1) && model.beginEditAppearance(),
        "expected edit session opened");
    model.setEditedAppearance(0x123456, 0.35F);
    expect(model.confirmAppearanceUpdate(), "expected appearance Save accepted");
    const auto ticket = model.takeAppearanceRequest();
    expect(ticket.has_value(), "expected appearance ticket");

    model.selectArea(TattooArea::face);
    expect(model.selectedArea() == TattooArea::body && !model.takeSlotQuery(),
        "expected appearance transaction to block area navigation");

    model.completeAppearanceUpdate(ticket->generation, UpdateTattooAppearanceSuccess{
        .actorFormId = 0x14,
        .runtimeHandle = 73,
    });

    expect(model.selectedArea() == TattooArea::body &&
            model.screen() == SlotWorkflowScreen::currentSlots &&
            !model.editAppearance() && !model.error(),
        "expected appearance completion to finish the original area transaction");
    const auto refresh = model.takeSlotQuery();
    expect(refresh && refresh->area == TattooArea::body,
        "expected successful transaction to refresh its original area");
    model.selectArea(TattooArea::face);
    expect(model.selectedArea() == TattooArea::face && model.takeSlotQuery(),
        "expected navigation allowed after transaction completes");
}

void appearanceWriteFailureRetriesFullUpdateAndSyncFailureRetriesOnlySync() {
    TattooCatalogSnapshot snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slotsWithEditableOwnedTattoo());
    expect(model.selectSlot(1) && model.beginEditAppearance(),
        "expected write retry session opened");
    model.setEditedAppearance(0x123456, 0.35F, 0xABCDEF, 4.0F, 2.0F, 5.0F);
    expect(model.confirmAppearanceUpdate(), "expected first write attempt accepted");
    const auto first = model.takeAppearanceRequest();
    model.completeAppearanceUpdate(first->generation, std::unexpected(ServiceError{
        ServiceErrorCode::updateFailed,
        "appearance write failed",
    }));

    const auto* failedWrite = model.editAppearance();
    expect(model.screen() == SlotWorkflowScreen::editAppearance && failedWrite &&
            failedWrite->mode == UpdateTattooAppearanceMode::updateAndSynchronize &&
            failedWrite->edited.color == 0x123456 && failedWrite->edited.alpha == 0.35F &&
            failedWrite->edited.glow == 0xABCDEF && failedWrite->edited.glossiness == 4.0F &&
            failedWrite->edited.specularStrength == 2.0F && failedWrite->edited.emissiveMult == 5.0F &&
            model.error() && model.error()->code == ServiceErrorCode::updateFailed,
        "expected write error to retain edited values for a full-update retry");
    expect(model.confirmAppearanceUpdate(), "expected write retry accepted");
    const auto writeRetry = model.takeAppearanceRequest();
    expect(writeRetry && writeRetry->generation > first->generation &&
            writeRetry->request.mode == UpdateTattooAppearanceMode::updateAndSynchronize,
        "expected write retry to remain a full update");

    model.completeAppearanceUpdate(writeRetry->generation, std::unexpected(ServiceError{
        ServiceErrorCode::synchronizeFailed,
        "appearance sync failed",
    }));
    const auto* failedSync = model.editAppearance();
    expect(model.screen() == SlotWorkflowScreen::editAppearance && failedSync &&
            failedSync->mode == UpdateTattooAppearanceMode::synchronizeOnly &&
            model.error() && model.error()->code == ServiceErrorCode::synchronizeFailed,
        "expected synchronization error to switch the retained session to Retry Sync");
    model.setEditedAppearance(0x102030, 0.9F, 0x010203, 0.5F, 0.75F, 1.0F);
    failedSync = model.editAppearance();
    expect(failedSync && failedSync->edited.color == 0x123456 &&
            failedSync->edited.alpha == 0.35F && failedSync->edited.glow == 0xABCDEF &&
            failedSync->edited.glossiness == 4.0F && failedSync->edited.specularStrength == 2.0F &&
            failedSync->edited.emissiveMult == 5.0F,
        "expected Retry Sync mode to ignore all appearance edits");
    expect(model.confirmAppearanceUpdate(), "expected synchronization-only retry accepted");
    const auto syncRetry = model.takeAppearanceRequest();
    expect(syncRetry && syncRetry->generation > writeRetry->generation &&
            syncRetry->request.mode == UpdateTattooAppearanceMode::synchronizeOnly &&
            syncRetry->request.runtimeHandle == 73 && syncRetry->request.color == 0x123456 &&
            syncRetry->request.alpha == 0.35F && syncRetry->request.glow == 0xABCDEF &&
            syncRetry->request.glossiness == 4.0F && syncRetry->request.specularStrength == 2.0F &&
            syncRetry->request.emissiveMult == 5.0F,
        "expected Retry Sync to preserve every edited value without another full update");

    model.completeAppearanceUpdate(syncRetry->generation, std::unexpected(ServiceError{
        ServiceErrorCode::updateFailed,
        "retry scheduling failed",
    }));
    const auto* failedRetry = model.editAppearance();
    expect(model.screen() == SlotWorkflowScreen::editAppearance && failedRetry &&
            failedRetry->mode == UpdateTattooAppearanceMode::synchronizeOnly &&
            model.error() && model.error()->code == ServiceErrorCode::updateFailed,
        "expected a failed Retry Sync to retain synchronization-only mode");
    expect(model.confirmAppearanceUpdate(), "expected failed Retry Sync to remain retryable");
    const auto secondSyncRetry = model.takeAppearanceRequest();
    expect(secondSyncRetry && secondSyncRetry->generation > syncRetry->generation &&
            secondSyncRetry->request.mode == UpdateTattooAppearanceMode::synchronizeOnly,
        "expected a failed Retry Sync never to emit a second full appearance update");
}

void resolveCrosshair(NativeSlotWorkflowModel& model) {
    model.start();
    expect(model.selectCrosshairTarget(), "expected Crosshair selection accepted");
    const auto target = model.takeActorTargetRequest();
    expect(target.has_value(), "expected one target-resolution ticket");
    model.completeActorTargetResolution(target->generation,
        ActorTarget{ActorTargetKind::crosshair, 0x1234, "Lydia"});
}

void loadCrosshairSlots(NativeSlotWorkflowModel& model, bool locked = false) {
    resolveCrosshair(model);
    const auto query = model.takeSlotQuery();
    expect(query && query->actorFormId == 0x1234, "expected exact NPC query");
    auto snapshot = slotsWithEditableOwnedTattoo();
    snapshot.actorFormId = 0x1234;
    snapshot.slots[1].tattoo->locked = locked;
    model.completeSlotQuery(query->generation, std::move(snapshot));
}

void expectTargetSwitchBlocked(NativeSlotWorkflowModel& model) {
    expect(model.isMutationInFlight(), "expected pending/active mutation presentation state");
    expect(!model.selectPlayerTarget() && !model.selectCrosshairTarget() &&
            !model.refreshCrosshairTarget(),
        "expected all target intents blocked during pending/active mutation");
    expect(model.actorTarget() && model.actorTarget()->formId == 0x1234 &&
            !model.takeActorTargetRequest(),
        "expected blocked selection to preserve NPC identity without resolution");
}

void explicitPlayerAndCrosshairResolutionNeverFallback() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    NativeSlotWorkflowModel model(catalog);
    expect(model.actorTarget() && model.actorTarget()->formId == 0x14 &&
            model.actorTarget()->displayName == "Player" &&
            model.selectedTargetKind() == ActorTargetKind::player,
        "expected explicit Player identity at startup");
    expect(!model.refreshCrosshairTarget(), "expected no Crosshair refresh in Player mode");
    model.start();
    expect(model.selectCrosshairTarget(), "expected Crosshair intent accepted");
    expect(!model.actorTarget() && model.isActorTargetResolutionInFlight() &&
            model.selectedTargetKind() == ActorTargetKind::crosshair && !model.takeSlotQuery(),
        "expected resolving Crosshair with old pending Player query removed");
    const auto target = model.takeActorTargetRequest();
    expect(target && !model.takeActorTargetRequest(), "expected target ticket consumed once");
    model.refreshSelectedArea();
    model.selectArea(TattooArea::face);
    expect(!model.takeSlotQuery() && !model.selectSlot(0), "expected no work without Actor");
    model.completeActorTargetResolution(target->generation, std::unexpected(ServiceError{
        ServiceErrorCode::actorNotFound, "No valid crosshair Actor"}));
    expect(!model.actorTarget() && !model.isActorTargetResolutionInFlight() &&
            model.error() && model.error()->message == "No valid crosshair Actor" &&
            !model.takeSlotQuery(), "expected provider error without Player fallback");
    expect(model.refreshCrosshairTarget(), "expected deliberate Crosshair retry");
    const auto retry = model.takeActorTargetRequest();
    model.completeActorTargetResolution(retry->generation,
        ActorTarget{ActorTargetKind::crosshair, 0x1234, "Lydia"});
    const auto query = model.takeSlotQuery();
    expect(query && query->actorFormId == 0x1234 && query->area == TattooArea::face &&
            model.actorTarget()->displayName == "Lydia" && !model.error(),
        "expected exact resolved Actor and selected area");
    expect(model.selectPlayerTarget(), "expected explicit Player selection accepted");
    const auto player = model.takeSlotQuery();
    expect(player && player->actorFormId == 0x14 && !model.takeActorTargetRequest(),
        "expected explicit Player query without resolver");

    for (const auto invalid : {ActorTarget{ActorTargetKind::player, 0x14, "Player"},
             ActorTarget{ActorTargetKind::crosshair, 0, "Invalid"}}) {
        expect(model.selectCrosshairTarget(), "expected Crosshair selection for invalid result");
        const auto request = model.takeActorTargetRequest();
        model.completeActorTargetResolution(request->generation, invalid);
        expect(!model.actorTarget() && !model.takeSlotQuery() && model.error(),
            "expected malformed provider result to reject without fallback");
        expect(model.selectPlayerTarget(), "expected explicit Player recovery after failure");
        const auto recovery = model.takeSlotQuery();
        expect(recovery && recovery->actorFormId == 0x14 && !model.error(),
            "expected deliberate Player recovery to clear target error");
    }
    expect(model.selectCrosshairTarget(), "expected unnamed Crosshair selection");
    const auto unnamed = model.takeActorTargetRequest();
    model.completeActorTargetResolution(unnamed->generation,
        ActorTarget{ActorTargetKind::crosshair, 0x1234, ""});
    expect(model.actorTarget() && model.actorTarget()->displayName == "Unnamed Actor",
        "expected nonempty presentation metadata for a valid unnamed Actor");
}

void targetChangeClearsAllCachesPagesAndTransientState() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slots(TattooArea::body, 12));
    for (auto area : {TattooArea::body, TattooArea::face, TattooArea::hands, TattooArea::feet}) {
        model.selectArea(area);
        if (const auto query = model.takeSlotQuery()) {
            model.completeSlotQuery(query->generation, slots(area, 12));
        }
        model.nextSlotPage();
        expect(model.slotPageIndex() == 1, "expected populated old Actor page");
    }
    expect(model.selectSlot(0), "expected old Actor slot selected");
    model.selectTattoo(tattoo("Foot", 0, "FEET"));
    expect(model.previewTattoo(), "expected preview before target change");
    expect(model.selectCrosshairTarget(), "expected target switch");
    expect(!model.targetSlot() && !model.previewTattoo() && !model.previewAppearance() &&
            !model.editAppearance() && !model.error() &&
            model.screen() == SlotWorkflowScreen::currentSlots,
        "expected actor-scoped transient state reset");
    for (auto area : {TattooArea::body, TattooArea::face, TattooArea::hands, TattooArea::feet}) {
        model.selectArea(area);
        expect(!model.slots() && model.slotPageIndex() == 0 && !model.takeSlotQuery(),
            "expected all old Actor caches/pages cleared without Player query");
    }
    model.selectArea(TattooArea::body);
    loadCrosshairSlots(model);
    expect(model.selectSlot(1) && model.beginEditAppearance(), "expected NPC edit session");
    model.setEditedAppearance(0x123456, 0.5F);
    expect(model.confirmAppearanceUpdate(), "expected NPC edit submission");
    const auto edit = model.takeAppearanceRequest();
    model.completeAppearanceUpdate(edit->generation, std::unexpected(ServiceError{
        ServiceErrorCode::synchronizeFailed, "old Actor sync failed"}));
    expect(model.editAppearance() && model.error(), "expected retained NPC retry state");
    expect(!model.selectPlayerTarget(), "expected unfinished synchronization to block target change");
    expect(model.confirmAppearanceUpdate(), "expected synchronization retry before switching Actor");
    const auto sync = model.takeAppearanceRequest();
    model.completeAppearanceUpdate(sync->generation, UpdateTattooAppearanceSuccess{});
    expect(model.selectPlayerTarget(), "expected target switch after appearance transaction completes");
    expect(!model.editAppearance() && !model.error() && !model.targetSlot() && !model.slots(),
        "expected target change to clear actual edit session and actor-scoped error");
}

void obsoleteTargetAndQueryCompletionsCannotAffectNewTarget() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    NativeSlotWorkflowModel model(catalog);
    model.start();
    const auto player = model.takeSlotQuery();
    expect(model.selectCrosshairTarget(), "expected first Crosshair resolution");
    const auto oldTarget = model.takeActorTargetRequest();
    expect(model.refreshCrosshairTarget(), "expected newer Crosshair resolution");
    const auto newTarget = model.takeActorTargetRequest();
    model.completeActorTargetResolution(oldTarget->generation,
        ActorTarget{ActorTargetKind::crosshair, 0x9999, "Old Actor"});
    model.completeSlotQuery(player->generation, slots(TattooArea::body, 3));
    expect(!model.actorTarget() && !model.slots() && !model.error() &&
            model.isActorTargetResolutionInFlight() && !model.takeSlotQuery(),
        "expected obsolete success completions ignored");
    model.completeActorTargetResolution(newTarget->generation,
        ActorTarget{ActorTargetKind::crosshair, 0x1234, "Lydia"});
    model.completeActorTargetResolution(oldTarget->generation, std::unexpected(ServiceError{
        ServiceErrorCode::actorNotFound, "obsolete target failure"}));
    model.completeSlotQuery(player->generation, std::unexpected(ServiceError{
        ServiceErrorCode::slotQueryFailed, "obsolete query failure"}));
    const auto query = model.takeSlotQuery();
    expect(query && query->actorFormId == 0x1234 && !model.error(),
        "expected obsolete errors not to affect resolved NPC");
}

void mismatchedActorSnapshotIsRejected() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    NativeSlotWorkflowModel model(catalog);
    resolveCrosshair(model);
    const auto query = model.takeSlotQuery();
    model.completeSlotQuery(query->generation, slotsWithEditableOwnedTattoo());
    expect(!model.slots() && model.error() && !model.selectSlot(1) &&
            !model.toggleSlotLock(1) && !model.confirmApply() && !model.confirmRemove() &&
            !model.beginEditAppearance() && !model.confirmAppearanceUpdate(),
        "expected foreign snapshot rejection before selection or mutation");
    expect(!model.takeApplyRequest() && !model.takeRemoveRequest() &&
            !model.takeAppearanceRequest() && !model.takeLockRequest(),
        "expected no mutation for a mismatched Actor snapshot");
}

void areaNavigationCannotReleaseOutstandingMutationGuard() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    for (bool appearance : {false, true}) {
        NativeSlotWorkflowModel model(catalog);
        loadCrosshairSlots(model);
        expect(model.selectSlot(1), "expected NPC slot selected");
        std::uint64_t generation{};
        if (appearance) {
            expect(model.beginEditAppearance(), "expected appearance session");
            model.setEditedAppearance(0x123456, 0.5F);
            expect(model.confirmAppearanceUpdate(), "expected appearance queued");
            const auto ticket = model.takeAppearanceRequest();
            expect(ticket.has_value(), "expected appearance ticket");
            generation = ticket->generation;
        } else {
            expect(model.toggleSelectedSlotLock(), "expected lock queued");
            const auto ticket = model.takeLockRequest();
            expect(ticket.has_value(), "expected lock ticket");
            generation = ticket->generation;
        }
        model.selectArea(TattooArea::face);
        expect(model.selectedArea() == (appearance ? TattooArea::body : TattooArea::face),
            "expected appearance transaction to block navigation and preserve exact area");
        expectTargetSwitchBlocked(model);
        const auto failure = std::unexpected(ServiceError{ServiceErrorCode::updateFailed, "late failure"});
        if (appearance) {
            model.completeAppearanceUpdate(generation + 1, failure);
            expectTargetSwitchBlocked(model);
            model.completeAppearanceUpdate(generation, failure);
            expect(model.error() && !model.selectPlayerTarget(),
                "expected matching appearance failure to retain retry transaction");
            model.cancelEditAppearance();
        } else {
            model.completeLockStateChange(generation + 1, failure);
            expectTargetSwitchBlocked(model);
            model.completeLockStateChange(generation, failure);
        }
        expect(!model.error() && !model.isMutationInFlight() && model.selectPlayerTarget(),
            "expected obsolete UI failure to release only the matching mutation guard");
    }
}

void npcMutationPathsPreserveActorAndBlockTargetChanges() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    for (int operation = 0; operation < 6; ++operation) {
        NativeSlotWorkflowModel model(catalog);
        loadCrosshairSlots(model, operation == 5);
        if (operation != 5) {
            expect(model.selectSlot(operation == 0 ? 0 : 1), "expected NPC slot selected");
        }
        std::uint64_t generation{};
        if (operation <= 1) {
            if (operation == 1) {
                expect(model.replaceSelectedSlot(), "expected NPC replacement");
            }
            model.selectTattoo(tattoo("New", 0));
            expect(model.confirmApply(), "expected NPC apply");
            expectTargetSwitchBlocked(model);
            const auto ticket = model.takeApplyRequest();
            expect(ticket && ticket->request.actorFormId == 0x1234, "expected NPC apply identity");
            generation = ticket->generation;
            expectTargetSwitchBlocked(model);
            model.completeApply(generation, ApplyTattooSuccess{});
        } else if (operation == 2) {
            expect(model.requestRemove() && model.confirmRemove(), "expected NPC remove");
            expectTargetSwitchBlocked(model);
            const auto ticket = model.takeRemoveRequest();
            expect(ticket && ticket->request.actorFormId == 0x1234, "expected NPC remove identity");
            expectTargetSwitchBlocked(model);
            model.completeRemove(ticket->generation, std::unexpected(ServiceError{
                ServiceErrorCode::synchronizeFailed, "sync failed"}));
            expect(model.confirmRemove(), "expected NPC remove sync retry");
            const auto retry = model.takeRemoveRequest();
            expect(retry && retry->request.actorFormId == 0x1234 &&
                    retry->request.mode == RemoveTattooMode::synchronizeOnly,
                "expected remove retry to synchronize only the original NPC");
            generation = retry->generation;
            expectTargetSwitchBlocked(model);
            model.completeRemove(generation, RemoveTattooSuccess{});
        } else if (operation == 3) {
            expect(model.beginEditAppearance(), "expected NPC appearance session");
            model.setEditedAppearance(0x123456, 0.5F);
            expect(model.confirmAppearanceUpdate(), "expected NPC appearance save");
            expectTargetSwitchBlocked(model);
            const auto ticket = model.takeAppearanceRequest();
            expect(ticket && ticket->request.actorFormId == 0x1234, "expected NPC edit identity");
            expectTargetSwitchBlocked(model);
            model.completeAppearanceUpdate(ticket->generation, std::unexpected(ServiceError{
                ServiceErrorCode::synchronizeFailed, "sync failed"}));
            expect(model.confirmAppearanceUpdate(), "expected NPC appearance sync retry");
            const auto retry = model.takeAppearanceRequest();
            expect(retry && retry->request.actorFormId == 0x1234 &&
                    retry->request.mode == UpdateTattooAppearanceMode::synchronizeOnly,
                "expected appearance retry to synchronize only the original NPC");
            generation = retry->generation;
            expectTargetSwitchBlocked(model);
            model.completeAppearanceUpdate(generation, UpdateTattooAppearanceSuccess{});
        } else {
            expect(operation == 5 ? model.toggleSlotLock(1) : model.toggleSelectedSlotLock(),
                "expected NPC Lock or direct Unlock");
            expectTargetSwitchBlocked(model);
            const auto ticket = model.takeLockRequest();
            expect(ticket && ticket->request.actorFormId == 0x1234 &&
                    ticket->request.locked == (operation != 5),
                "expected NPC identity and requested Lock/Unlock state");
            generation = ticket->generation;
            expectTargetSwitchBlocked(model);
            model.completeLockStateChange(generation, SetTattooLockedSuccess{});
        }
        const auto refresh = model.takeSlotQuery();
        expect(refresh && refresh->actorFormId == 0x1234, "expected mutation refresh for exact NPC");
        expect(model.selectPlayerTarget(), "expected switching after completed mutation");
        const auto player = model.takeSlotQuery();
        const auto obsolete = std::unexpected(ServiceError{ServiceErrorCode::updateFailed, "obsolete"});
        model.completeApply(generation, obsolete);
        model.completeRemove(generation, obsolete);
        model.completeAppearanceUpdate(generation, obsolete);
        model.completeLockStateChange(generation, obsolete);
        expect(player && player->actorFormId == 0x14 && !model.error() &&
                !model.takeSlotQuery() && model.screen() == SlotWorkflowScreen::currentSlots,
            "expected old mutation completions ignored after target change");
    }
}

void livePreviewDebouncesEveryFieldFromLatestObservedEdit() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slotsWithEditableOwnedTattoo());
    expect(model.selectSlot(1) && model.beginEditAppearance(), "expected edit session");
    const auto start = std::chrono::steady_clock::time_point{};
    model.advanceLivePreview(start);
    model.setEditedAppearance(0x112233, 0.5F, 0x445566, 0.25F, 0.75F, 2.0F);
    model.advanceLivePreview(start + 50ms);
    expect(model.livePreviewStatus() == LivePreviewStatus::pending,
        "expected observed local edit to start pending preview");
    model.advanceLivePreview(start + 1049ms);
    expect(!model.takeAppearanceRequest(), "expected no preview before 1000ms from observation");
    model.advanceLivePreview(start + 1050ms);
    const auto preview = model.takeAppearanceRequest();
    expect(preview && preview->purpose == AppearanceOperationPurpose::preview &&
            preview->request.actorFormId == 0x14 && preview->request.runtimeHandle == 73 &&
            preview->request.color == 0x112233 && preview->request.alpha == 0.5F &&
            preview->request.glow == 0x445566 && preview->request.glossiness == 0.25F &&
            preview->request.specularStrength == 0.75F && preview->request.emissiveMult == 2.0F,
        "expected exact Player identity and every editable field at the debounce deadline");
    expect(model.livePreviewStatus() == LivePreviewStatus::updating &&
            model.screen() == SlotWorkflowScreen::editAppearance,
        "expected preview to keep the editor open");
    model.advanceLivePreview(start + 3000ms);
    expect(!model.takeAppearanceRequest(), "expected one appearance operation at a time");
    model.completeAppearanceUpdate(preview->generation, UpdateTattooAppearanceSuccess{});
    expect(model.editAppearance() && model.livePreviewStatus() == LivePreviewStatus::applied &&
            !model.takeSlotQuery(), "expected successful preview to retain session without refresh");
    model.setEditedAppearance(0x112233, 0.5F, 0x445566, 0.25F, 0.75F, 2.0F);
    model.advanceLivePreview(start + 5000ms);
    model.advanceLivePreview(start + 6000ms);
    expect(!model.takeAppearanceRequest(), "expected identical successful preview not rewritten");
}

void livePreviewObservesLatestEditsWhileAnOperationIsActive() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    for (bool alreadyElapsed : {false, true}) {
        NativeSlotWorkflowModel model(catalog);
        loadCrosshairSlots(model);
        expect(model.selectSlot(1) && model.beginEditAppearance(), "expected NPC edit session");
        const auto start = std::chrono::steady_clock::time_point{};
        model.setEditedAppearance(0x111111, 0.1F);
        model.advanceLivePreview(start);
        model.advanceLivePreview(start + 1000ms);
        const auto first = model.takeAppearanceRequest();
        expect(first.has_value(), "expected first preview");
        model.setEditedAppearance(0x222222, 0.2F);
        model.advanceLivePreview(start + 1100ms);
        model.setEditedAppearance(0x333333, 0.3F, 0, 0, 0, 0);
        model.advanceLivePreview(start + 1600ms);
        expect(!model.takeAppearanceRequest(), "expected edits coalesced during active preview");
        if (alreadyElapsed) {
            model.advanceLivePreview(start + 3000ms);
            expect(!model.takeAppearanceRequest(), "expected elapsed debounce not to overlap work");
        }
        model.completeAppearanceUpdate(first->generation, UpdateTattooAppearanceSuccess{});
        expect(model.livePreviewStatus() == LivePreviewStatus::pending,
            "expected newest edit retained after older preview completes");
        if (!alreadyElapsed) {
            model.advanceLivePreview(start + 2599ms);
            expect(!model.takeAppearanceRequest(), "expected last observed edit to restart debounce");
        }
        auto copied = model;
        copied.advanceLivePreview(start + 3000ms);
        model.advanceLivePreview(start + (alreadyElapsed ? 3000ms : 2600ms));
        const auto latest = model.takeAppearanceRequest();
        const auto copyLatest = copied.takeAppearanceRequest();
        expect(latest && copyLatest && latest->purpose == AppearanceOperationPurpose::preview &&
                latest->request.actorFormId == 0x1234 && latest->request.runtimeHandle == 73 &&
                latest->request.color == 0x333333 && latest->request.alpha == 0.3F &&
                latest->request.glow == 0 && latest->request.emissiveMult == 0 &&
                copyLatest->request.color == 0x333333,
            "expected copyable latest-wins state with exact NPC identity and zero emission");
    }
}

stui::native::SlotAppearanceTicket startLivePreview(NativeSlotWorkflowModel& model) {
    expect(model.selectSlot(1) && model.beginEditAppearance(), "expected live preview edit session");
    model.setEditedAppearance(0x112233, 0.5F, 0, 0.25F, 0.75F, 0);
    const auto start = std::chrono::steady_clock::time_point{};
    model.advanceLivePreview(start);
    model.advanceLivePreview(start + 1000ms);
    auto ticket = model.takeAppearanceRequest();
    expect(ticket.has_value(), "expected debounced appearance ticket");
    return *ticket;
}

void saveFlushesPendingAndCommitsIdenticalPreviewWithoutWriting() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    for (bool previewed : {false, true}) {
        NativeSlotWorkflowModel model(catalog);
        completeInitialQuery(model, slotsWithEditableOwnedTattoo());
        if (previewed) {
            const auto preview = startLivePreview(model);
            model.completeAppearanceUpdate(preview.generation, UpdateTattooAppearanceSuccess{});
        } else {
            expect(model.selectSlot(1) && model.beginEditAppearance(), "expected edit session");
            model.setEditedAppearance(0x112233, 0.5F, 0, 0.25F, 0.75F, 0);
        }
        expect(model.confirmAppearanceUpdate(), "expected Save accepted without debounce delay");
        const auto commit = model.takeAppearanceRequest();
        if (previewed) {
            expect(!commit, "expected Save not to repeat a successfully previewed write");
        } else {
            expect(commit && commit->purpose == AppearanceOperationPurpose::commit &&
                    commit->request.color == 0x112233 && commit->request.glow == 0 &&
                    commit->request.emissiveMult == 0,
                "expected Save to flush every latest pending appearance value");
            model.completeAppearanceUpdate(commit->generation, UpdateTattooAppearanceSuccess{});
        }
        const auto refresh = model.takeSlotQuery();
        expect(refresh && refresh->actorFormId == 0x14 && refresh->area == TattooArea::body &&
                !model.editAppearance() && model.screen() == SlotWorkflowScreen::currentSlots &&
                !model.isMutationInFlight(),
            "expected Save to refresh the exact snapshot and release the transaction");
    }
}

void saveWithoutChangesExitsWithoutWriting() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slotsWithEditableOwnedTattoo());
    expect(model.selectSlot(1) && model.beginEditAppearance(), "expected unchanged edit session");
    expect(model.confirmAppearanceUpdate() && !model.takeAppearanceRequest() &&
            !model.editAppearance() && !model.isMutationInFlight(),
        "expected unchanged Save intent to exit without mutation");
}

void saveDuringPreviewWaitsAndFlushesOnlyTheLatestValue() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    for (bool newerEdit : {false, true}) {
        NativeSlotWorkflowModel model(catalog);
        loadCrosshairSlots(model);
        const auto preview = startLivePreview(model);
        if (newerEdit) {
            model.setEditedAppearance(0xABCDEF, 0.8F, 0x102030, 2, 3, 4);
        }
        expect(model.confirmAppearanceUpdate() && !model.confirmAppearanceUpdate() &&
                !model.takeAppearanceRequest(), "expected one deferred Save intent during preview");
        model.completeAppearanceUpdate(preview.generation, UpdateTattooAppearanceSuccess{});
        const auto commit = model.takeAppearanceRequest();
        if (newerEdit) {
            expect(commit && commit->purpose == AppearanceOperationPurpose::commit &&
                    commit->request.actorFormId == 0x1234 && commit->request.color == 0xABCDEF &&
                    commit->request.alpha == 0.8F && commit->request.emissiveMult == 4,
                "expected latest value flushed immediately after the older preview finishes");
            model.completeAppearanceUpdate(commit->generation, UpdateTattooAppearanceSuccess{});
        } else {
            expect(!commit, "expected identical in-flight preview to fulfill Save without another write");
        }
        const auto refresh = model.takeSlotQuery();
        expect(refresh && refresh->actorFormId == 0x1234 && !model.editAppearance(),
            "expected successful deferred Save to refresh the original NPC");
    }
}

void cancelAndCloseRestoreExactOriginalBeforeLeaving() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    for (bool close : {false, true}) {
        NativeSlotWorkflowModel model(catalog);
        loadCrosshairSlots(model);
        const auto preview = startLivePreview(model);
        model.completeAppearanceUpdate(preview.generation, UpdateTattooAppearanceSuccess{});
        model.setEditedAppearance(0xFFFFFF, 1, 0xFFFFFF, 9, 10, 11);
        if (close) {
            expect(model.requestEditAppearanceClose(), "expected Close rollback intent accepted");
            expect(!model.requestEditAppearanceClose(), "expected duplicate Close intent rejected");
        } else {
            model.cancelEditAppearance();
            model.cancelEditAppearance();
        }
        const auto restore = model.takeAppearanceRequest();
        expect(restore && restore->purpose == AppearanceOperationPurpose::restore &&
                restore->request.actorFormId == 0x1234 && restore->request.runtimeHandle == 73 &&
                restore->request.color == 0x2468AC && restore->request.alpha == 0.42F &&
                restore->request.glow == 0x102030 && restore->request.glossiness == 2.5F &&
                restore->request.specularStrength == 1.25F && restore->request.emissiveMult == 3,
            "expected exact original appearance and NPC identity restored, including original glow");
        expect(model.editAppearance() && model.livePreviewStatus() == LivePreviewStatus::restoring &&
                !model.takeMenuCloseRequest() && !model.takeAppearanceRequest(),
            "expected editor retained until one restoration completes");
        model.completeAppearanceUpdate(preview.generation, std::unexpected(ServiceError{
            ServiceErrorCode::updateFailed, "obsolete preview completion"}));
        expect(!model.error() && !model.takeMenuCloseRequest(), "expected stale preview ignored during restore");
        model.completeAppearanceUpdate(restore->generation, UpdateTattooAppearanceSuccess{});
        expect(!model.editAppearance() && model.screen() == SlotWorkflowScreen::slotActions,
            "expected rollback completion to leave editor");
        expect(model.takeMenuCloseRequest() == close && !model.takeMenuCloseRequest(),
            "expected Close signal exactly once and only after restore success");
        expect(!model.isMutationInFlight() && model.selectPlayerTarget(),
            "expected completed rollback to release actor selection guard");
    }
}

void cancelAndCloseBeforePreviewDiscardOnlyLocalEdits() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    for (bool close : {false, true}) {
        NativeSlotWorkflowModel model(catalog);
        completeInitialQuery(model, slotsWithEditableOwnedTattoo());
        expect(model.selectSlot(1) && model.beginEditAppearance(), "expected local edit session");
        model.setEditedAppearance(0x112233, 0.5F);
        model.advanceLivePreview(std::chrono::steady_clock::time_point{});
        if (close) {
            expect(model.requestEditAppearanceClose(), "expected local-only Close accepted");
        } else {
            model.cancelEditAppearance();
        }
        expect(!model.editAppearance() && !model.takeAppearanceRequest() &&
                model.takeMenuCloseRequest() == close && !model.takeMenuCloseRequest(),
            "expected local edits discarded without restore before any preview write");
    }
}

void rollbackDuringPreviewWaitsForTheExactWriteOutcome() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    for (bool close : {false, true}) {
        for (int outcome = 0; outcome < 4; ++outcome) {
            NativeSlotWorkflowModel model(catalog);
            completeInitialQuery(model, slotsWithEditableOwnedTattoo());
            const auto preview = startLivePreview(model);
            if (close) {
                expect(model.requestEditAppearanceClose(), "expected deferred Close");
            } else {
                model.cancelEditAppearance();
            }
            expect(model.editAppearance() && !model.takeAppearanceRequest() &&
                    !model.takeMenuCloseRequest(), "expected rollback to await active preview outcome");
            model.completeAppearanceUpdate(preview.generation + 1, UpdateTattooAppearanceSuccess{});
            expect(model.editAppearance() && !model.takeMenuCloseRequest(),
                "expected stale completion not to release deferred rollback");
            if (outcome == 0) {
                model.completeAppearanceUpdate(preview.generation, UpdateTattooAppearanceSuccess{});
            } else {
                model.completeAppearanceUpdate(preview.generation, std::unexpected(ServiceError{
                    .code = outcome == 3 ? ServiceErrorCode::synchronizeFailed
                                         : ServiceErrorCode::updateFailed,
                    .message = "preview failed",
                    .mutationSideEffect = outcome == 2
                        ? MutationSideEffect::mayHaveOccurred
                        : MutationSideEffect::none,
                }));
            }
            const auto restore = model.takeAppearanceRequest();
            if (outcome == 1) {
                expect(!restore && !model.editAppearance(),
                    "expected confirmed pre-write failure to exit without a restore");
            } else {
                expect(restore && restore->purpose == AppearanceOperationPurpose::restore &&
                        restore->request.mode == UpdateTattooAppearanceMode::updateAndSynchronize,
                    "expected success, possible partial write, or sync failure to restore original values");
                model.completeAppearanceUpdate(restore->generation, UpdateTattooAppearanceSuccess{});
            }
            expect(model.takeMenuCloseRequest() == close && !model.takeMenuCloseRequest() &&
                    !model.isMutationInFlight(), "expected safe rollback completion and one deferred close");
        }
    }
}

void retryPreservesPurposeAndSyncOnlyModeAfterActorFailure() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    for (const auto purpose : {AppearanceOperationPurpose::preview,
             AppearanceOperationPurpose::commit, AppearanceOperationPurpose::restore}) {
        NativeSlotWorkflowModel model(catalog);
        loadCrosshairSlots(model);
        auto operation = startLivePreview(model);
        if (purpose != AppearanceOperationPurpose::preview) {
            model.completeAppearanceUpdate(operation.generation, UpdateTattooAppearanceSuccess{});
            if (purpose == AppearanceOperationPurpose::commit) {
                model.setEditedAppearance(0xABCDEF, 0.7F);
                expect(model.confirmAppearanceUpdate(), "expected commit operation");
            } else {
                expect(model.requestEditAppearanceClose(), "expected restore operation");
            }
            const auto next = model.takeAppearanceRequest();
            expect(next.has_value(), "expected purpose-specific operation ticket");
            operation = *next;
        }
        model.completeAppearanceUpdate(operation.generation, std::unexpected(ServiceError{
            ServiceErrorCode::updateFailed, "write failed"}));
        expect(model.livePreviewStatus() == (purpose == AppearanceOperationPurpose::restore
                ? LivePreviewStatus::restoreError : LivePreviewStatus::previewError),
            "expected operation-specific retry status");
        expect(model.retryLivePreviewOperation() && !model.retryLivePreviewOperation(),
            "expected one explicit operation retry");
        auto retry = model.takeAppearanceRequest();
        expect(retry && retry->purpose == purpose &&
                retry->request.mode == UpdateTattooAppearanceMode::updateAndSynchronize,
            "expected failed write to retry the same logical purpose with full update");
        model.completeAppearanceUpdate(retry->generation, std::unexpected(ServiceError{
            ServiceErrorCode::synchronizeFailed, "sync failed"}));
        for (int attempt = 0; attempt < 2; ++attempt) {
            expect(model.retryLivePreviewOperation(), "expected synchronization retry");
            retry = model.takeAppearanceRequest();
            expect(retry && retry->purpose == purpose && retry->request.actorFormId == 0x1234 &&
                    retry->request.runtimeHandle == 73 &&
                    retry->request.mode == UpdateTattooAppearanceMode::synchronizeOnly,
                "expected exact NPC and unchanged purpose without repeating a completed write");
            if (attempt == 0) {
                model.completeAppearanceUpdate(retry->generation, std::unexpected(ServiceError{
                    ServiceErrorCode::actorNotFound, "NPC unloaded"}));
                expect(model.actorTarget()->formId == 0x1234 && model.editAppearance() &&
                        !model.takeMenuCloseRequest() && !model.selectPlayerTarget(),
                    "expected unavailable NPC to retain exact transaction without closing or fallback");
            }
        }
        model.completeAppearanceUpdate(retry->generation, UpdateTattooAppearanceSuccess{});
        if (purpose == AppearanceOperationPurpose::preview) {
            expect(model.livePreviewStatus() == LivePreviewStatus::applied && model.editAppearance(),
                "expected successful preview retry to remain editable");
        } else {
            expect(!model.editAppearance(), "expected successful commit or restore retry to finish");
        }
        expect(model.takeMenuCloseRequest() == (purpose == AppearanceOperationPurpose::restore),
            "expected only completed Close restoration to emit closure");
    }
}

void previewTransactionBlocksConflictingNavigationAndMutations() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    NativeSlotWorkflowModel model(catalog);
    auto body = slotsWithEditableOwnedTattoo();
    auto extraSlots = slots(TattooArea::body, 13);
    extraSlots.slots[1] = body.slots[1];
    completeInitialQuery(model, std::move(extraSlots));
    model.setSlotPageNumber(2);
    expect(model.selectSlot(1) && model.beginEditAppearance(), "expected edit session");
    model.setEditedAppearance(0x112233, 0.5F);
    const auto assertBlocked = [&] {
        expect(model.isMutationInFlight() && !model.selectPlayerTarget() &&
                !model.selectCrosshairTarget() && !model.selectSlot(0) &&
                !model.toggleSlotLock(1) && !model.toggleSelectedSlotLock() &&
                !model.replaceSelectedSlot() && !model.requestRemove() &&
                !model.confirmRemove() && !model.confirmApply() && !model.beginEditAppearance(),
            "expected transaction to exclude target/slot changes and competing mutations");
        model.previousSlotPage();
        expect(model.slotPageIndex() == 1, "expected Previous blocked");
        model.nextSlotPage();
        expect(model.slotPageIndex() == 1, "expected Next blocked");
        model.setSlotPageNumber(1);
        model.selectArea(TattooArea::face);
        model.refreshSelectedArea();
        model.backToSlots();
        model.resetSession();
        expect(model.slotPageIndex() == 1 && model.selectedArea() == TattooArea::body &&
                model.editAppearance() && !model.takeSlotQuery(),
            "expected navigation, refresh, and reset not to discard active transaction");
    };
    assertBlocked();
    const auto start = std::chrono::steady_clock::time_point{};
    model.advanceLivePreview(start);
    model.advanceLivePreview(start + 1000ms);
    assertBlocked();
    const auto preview = model.takeAppearanceRequest();
    expect(preview.has_value(), "expected preview ticket");
    assertBlocked();
    model.completeAppearanceUpdate(preview->generation, UpdateTattooAppearanceSuccess{});
    assertBlocked();
    expect(model.requestEditAppearanceClose(), "expected Close to initiate restore");
    const auto restore = model.takeAppearanceRequest();
    expect(restore.has_value(), "expected restoration ticket");
    assertBlocked();
    model.completeAppearanceUpdate(restore->generation, UpdateTattooAppearanceSuccess{});
    expect(!model.selectPlayerTarget(), "expected deferred Close consumption before target navigation");
    expect(model.takeMenuCloseRequest() && model.selectPlayerTarget(),
        "expected completed and consumed Close to release transaction guard");
}

void obsoleteAppearanceCompletionCannotAffectANewSession() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slotsWithEditableOwnedTattoo());
    const auto old = startLivePreview(model);
    model.completeAppearanceUpdate(old.generation, std::unexpected(ServiceError{
        ServiceErrorCode::updateFailed, "first write failed"}));
    model.cancelEditAppearance();
    expect(model.selectCrosshairTarget(), "expected target change after safe local cancel");
    loadCrosshairSlots(model);
    const auto current = startLivePreview(model);
    model.completeAppearanceUpdate(old.generation, UpdateTattooAppearanceSuccess{});
    expect(model.editAppearance() && model.editAppearance()->actorFormId == 0x1234 &&
            model.livePreviewStatus() == LivePreviewStatus::updating && !model.error() &&
            !model.takeMenuCloseRequest() && !model.takeSlotQuery(),
        "expected obsolete session success not to alter active NPC session");
    model.completeAppearanceUpdate(current.generation, UpdateTattooAppearanceSuccess{});
    expect(model.livePreviewStatus() == LivePreviewStatus::applied,
        "expected only the current operation to complete the preview");
}

void editSessionRejectsOlderSnapshotRefreshAndKeepsTextureIdentity() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    NativeSlotWorkflowModel model(catalog);
    loadCrosshairSlots(model);
    model.refreshSelectedArea();
    const auto oldQuery = model.takeSlotQuery();
    expect(oldQuery.has_value(), "expected refresh already outstanding before editor entry");
    const auto preview = startLivePreview(model);
    auto changed = slotsWithEditableOwnedTattoo();
    changed.actorFormId = 0x1234;
    changed.slots[1].tattoo->runtimeHandle = 999;
    changed.slots[1].tattoo->texturePath = "marks/replaced.dds";
    model.completeSlotQuery(oldQuery->generation, std::move(changed));
    model.completeAppearanceUpdate(preview.generation, UpdateTattooAppearanceSuccess{});
    const auto* session = model.editAppearance();
    expect(session && session->actorFormId == 0x1234 && session->targetGeneration > 0 &&
            session->area == TattooArea::body && session->slot == 1 &&
            session->runtimeHandle == 73 && session->texturePath == "marks/existing.dds" &&
            session->glowTexture == "marks/existing_g.dds" && session->bump == "marks/existing_n.dds" &&
            model.slots()->slots[1].tattoo->runtimeHandle == 73,
        "expected obsolete refresh not to change captured actor/slot/handle/texture identity");
    model.cancelEditAppearance();
    const auto restore = model.takeAppearanceRequest();
    expect(restore && restore->request.actorFormId == 0x1234 && restore->request.runtimeHandle == 73,
        "expected restoration to retain the exact original session identity");
}

void cleanEditorRejectsRefreshBeforePreviewStarts() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    NativeSlotWorkflowModel model(catalog);
    loadCrosshairSlots(model);
    model.refreshSelectedArea();
    const auto oldQuery = model.takeSlotQuery();
    expect(oldQuery.has_value(), "expected outstanding refresh before editor entry");
    expect(model.selectSlot(1) && model.beginEditAppearance(), "expected clean edit session");
    expect(model.livePreviewStatus() == LivePreviewStatus::clean,
        "expected editor to remain clean before requesting refresh");
    model.refreshSelectedArea();
    expect(!model.takeSlotQuery(), "expected clean editor to reject a new snapshot refresh");

    model.setEditedAppearance(0x112233, 0.5F);
    const auto start = std::chrono::steady_clock::time_point{};
    model.advanceLivePreview(start);
    model.advanceLivePreview(start + 1000ms);
    auto changed = slotsWithEditableOwnedTattoo();
    changed.actorFormId = 0x1234;
    changed.slots[1].tattoo->runtimeHandle = 999;
    changed.slots[1].tattoo->texturePath = "marks/replaced.dds";
    model.completeSlotQuery(oldQuery->generation, std::move(changed));
    const auto preview = model.takeAppearanceRequest();
    expect(preview && preview->request.actorFormId == 0x1234 &&
            preview->request.runtimeHandle == 73 && model.editAppearance() &&
            model.editAppearance()->runtimeHandle == 73 &&
            model.editAppearance()->texturePath == "marks/existing.dds" &&
            model.slots()->slots[1].tattoo->runtimeHandle == 73 &&
            model.slots()->slots[1].tattoo->texturePath == "marks/existing.dds",
        "expected late refresh completion not to replace the captured snapshot during preview");
    model.completeAppearanceUpdate(preview->generation, UpdateTattooAppearanceSuccess{});
    expect(model.confirmAppearanceUpdate(), "expected previewed Save to finish the session");
    const auto refresh = model.takeSlotQuery();
    expect(refresh && refresh->actorFormId == 0x1234,
        "expected snapshot refresh available again after the session finishes");
}

void restoringNonGlowingOriginalPreservesZeroEmission() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    NativeSlotWorkflowModel model(catalog);
    auto body = slotsWithEditableOwnedTattoo();
    body.slots[1].tattoo->glow = 0;
    body.slots[1].tattoo->emissiveMult = 0;
    completeInitialQuery(model, std::move(body));
    expect(!model.retryLivePreviewOperation() && !model.requestEditAppearanceClose() &&
            !model.takeMenuCloseRequest() && model.livePreviewStatus() == LivePreviewStatus::clean,
        "expected no appearance intents without an editor session");
    expect(model.selectSlot(1) && model.beginEditAppearance(), "expected non-glowing edit session");
    model.setEditedAppearance(0x112233, 0.5F, 0x123456, 2, 3, 7);
    const auto start = std::chrono::steady_clock::time_point{};
    model.advanceLivePreview(start);
    model.advanceLivePreview(start + 1000ms);
    const auto preview = model.takeAppearanceRequest();
    expect(preview && preview->request.glow == 0x123456 && preview->request.emissiveMult == 7,
        "expected glowing preview with above-one emission");
    model.completeAppearanceUpdate(preview->generation, UpdateTattooAppearanceSuccess{});
    model.cancelEditAppearance();
    const auto restore = model.takeAppearanceRequest();
    expect(restore && restore->purpose == AppearanceOperationPurpose::restore &&
            restore->request.glow == 0 && restore->request.emissiveMult == 0,
        "expected restore to remove glow and preserve exact zero emission");
    model.completeAppearanceUpdate(restore->generation, UpdateTattooAppearanceSuccess{});
    expect(!model.editAppearance() && !model.isMutationInFlight(),
        "expected non-glowing restoration to finish the transaction");
}

void failedLatestPreviewStillRestoresPreviouslyWrittenAppearance() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    NativeSlotWorkflowModel model(catalog);
    completeInitialQuery(model, slotsWithEditableOwnedTattoo());
    const auto first = startLivePreview(model);
    model.completeAppearanceUpdate(first.generation, UpdateTattooAppearanceSuccess{});
    model.setEditedAppearance(0xFFFFFF, 1);
    const auto start = std::chrono::steady_clock::time_point{};
    model.advanceLivePreview(start + 1100ms);
    model.advanceLivePreview(start + 2100ms);
    const auto latest = model.takeAppearanceRequest();
    expect(latest.has_value(), "expected later preview");
    expect(model.requestEditAppearanceClose(), "expected Close while later preview active");
    model.completeAppearanceUpdate(latest->generation, std::unexpected(ServiceError{
        ServiceErrorCode::updateFailed, "later write failed"}));
    const auto restore = model.takeAppearanceRequest();
    expect(restore && restore->purpose == AppearanceOperationPurpose::restore &&
            restore->request.color == 0x2468AC && !model.takeMenuCloseRequest(),
        "expected prior successful preview to require rollback despite latest write failure");
}

void favoriteRequestsDoNotMutateCatalogUntilSuccessfulCompletion() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    const auto selected = catalog.page().entries.front();

    expect(model.requestFavorite(selected, true), "expected favorite request to queue");
    const auto ticket = model.takeFavoriteRequest();
    expect(ticket && model.favoritePending() && !catalog.isFavorite(selected),
        "expected favorite to remain unchanged while storage is pending");
    expect(!model.takeApplyRequest() && !model.takeRemoveRequest(),
        "expected favorite request not to create tattoo mutation requests");
    model.completeFavorite(ticket->requestId,
        stui::runtime::FavoriteList{stui::repository::favoriteIdentity(selected)});

    expect(!model.favoritePending() && !model.favoriteError() && catalog.isFavorite(selected),
        "expected successful favorite completion to update only catalog membership");
}

void failedFavoriteCanRetryWithFreshRequestId() {
    auto snapshot = catalogWithEntries(1);
    NativeCatalogBrowserModel catalog([&snapshot] { return snapshot; });
    catalog.refresh();
    NativeSlotWorkflowModel model(catalog);
    const auto selected = catalog.page().entries.front();

    expect(model.requestFavorite(selected, true), "expected initial favorite request");
    const auto first = model.takeFavoriteRequest();
    model.completeFavorite(first->requestId,
        std::unexpected(stui::runtime::ConfigError{.message = "disk failure"}));
    expect(model.favoriteError() && !catalog.isFavorite(selected),
        "expected failed favorite to preserve prior catalog membership");
    expect(model.retryFavorite(), "expected failed favorite request to be retryable");
    const auto retry = model.takeFavoriteRequest();

    expect(retry && retry->requestId > first->requestId && retry->enabled,
        "expected retry to use a fresh explicit desired-state request");
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
    failures += run("live preview debounces every field from latest observed edit", livePreviewDebouncesEveryFieldFromLatestObservedEdit);
    failures += run("live preview observes latest edits while an operation is active", livePreviewObservesLatestEditsWhileAnOperationIsActive);
    failures += run("Save flushes pending and commits identical preview without writing", saveFlushesPendingAndCommitsIdenticalPreviewWithoutWriting);
    failures += run("Save without changes exits without writing", saveWithoutChangesExitsWithoutWriting);
    failures += run("Save during preview waits and flushes only the latest value", saveDuringPreviewWaitsAndFlushesOnlyTheLatestValue);
    failures += run("Cancel and Close restore exact original before leaving", cancelAndCloseRestoreExactOriginalBeforeLeaving);
    failures += run("Cancel and Close before preview discard only local edits", cancelAndCloseBeforePreviewDiscardOnlyLocalEdits);
    failures += run("rollback during preview waits for the exact write outcome", rollbackDuringPreviewWaitsForTheExactWriteOutcome);
    failures += run("retry preserves purpose and sync-only mode after Actor failure", retryPreservesPurposeAndSyncOnlyModeAfterActorFailure);
    failures += run("preview transaction blocks conflicting navigation and mutations", previewTransactionBlocksConflictingNavigationAndMutations);
    failures += run("obsolete appearance completion cannot affect a new session", obsoleteAppearanceCompletionCannotAffectANewSession);
    failures += run("edit session rejects older snapshot refresh and keeps texture identity", editSessionRejectsOlderSnapshotRefreshAndKeepsTextureIdentity);
    failures += run("clean editor rejects refresh before preview starts", cleanEditorRejectsRefreshBeforePreviewStarts);
    failures += run("restoring non-glowing original preserves zero emission", restoringNonGlowingOriginalPreservesZeroEmission);
    failures += run("failed latest preview still restores previously written appearance", failedLatestPreviewStillRestoresPreviouslyWrittenAppearance);
    failures += run("favorite requests wait for successful completion", favoriteRequestsDoNotMutateCatalogUntilSuccessfulCompletion);
    failures += run("failed favorite can retry with fresh request", failedFavoriteCanRetryWithFreshRequestId);
    failures += run("explicit Player and Crosshair resolution never fallback", explicitPlayerAndCrosshairResolutionNeverFallback);
    failures += run("target change clears all caches pages and transient state", targetChangeClearsAllCachesPagesAndTransientState);
    failures += run("obsolete target and query completions cannot affect new target", obsoleteTargetAndQueryCompletionsCannotAffectNewTarget);
    failures += run("mismatched Actor snapshot is rejected", mismatchedActorSnapshotIsRejected);
    failures += run("NPC mutation paths preserve Actor and block target changes", npcMutationPathsPreserveActorAndBlockTargetChanges);
    failures += run("area navigation cannot release outstanding mutation guard", areaNavigationCannotReleaseOutstandingMutationGuard);
    failures += run("start schedules one Player BODY query", startSchedulesOnePlayerBodyQuery);
    failures += run("caches area results and preserves per-area pages", cachesAreaResultsAndPreservesPerAreaPages);
    failures += run("clamps slot pagination", clampsSlotPagination);
    failures += run("external slots are rejected and owned slots open Actions", externalSlotsAreRejectedAndOwnedSlotsOpenActions);
    failures += run("remove requires confirmation and creates one request", removeRequiresConfirmationAndCreatesOneRequest);
    failures += run("remove completion refreshes or retains confirmation", removeCompletionRefreshesOrRetainsConfirmationForRetry);
    failures += run("empty and owned targets initialize expected appearance", emptyAndOwnedTargetsInitializeExpectedAppearance);
    failures += run("edited appearance flows into Apply request", editedAppearanceFlowsIntoApplyRequest);
    failures += run("rejects tattoo outside selected Area", rejectsTattooOutsideTheSelectedArea);
    failures += run("finds In Use slots by Tattoo Identity", findsInUseSlotsBySlaveTatsTattooIdentity);
    failures += run(
        "filters Picker to applied tattoos and preserves toggle across navigation",
        filtersPickerToAppliedTattoosAndPreservesToggleAcrossNavigation);
    failures += run("preview does not apply and Cancel returns to Slots", previewDoesNotApplyAndCancelReturnsToSlots);
    failures += run("preview Back returns to Picker and keeps applying state", previewBackReturnsToPickerAndKeepsApplyingState);
    failures += run("explicit confirmation creates one exact-domain policy request", explicitConfirmationCreatesOneExactDomainPolicyRequest);
    failures += run("apply success returns to slots and refreshes area", applySuccessReturnsToSlotsAndRefreshesArea);
    failures += run("apply failure retains Preview for retry", applyFailureRetainsPreviewForRetry);
    failures += run("apply synchronization failure retries without repeating mutation",
        applySynchronizationFailureRetriesWithoutRepeatingMutation);
    failures += run("recent history loads and records only successful Apply",
        recentHistoryLoadsAndRecordsOnlySuccessfulApply);
    failures += run("failed recent history retries without changing tattoo result",
        failedRecentHistoryWriteRetriesWithoutChangingTattooResult);
    failures += run("stale completions are ignored", staleCompletionsAreIgnored);
    failures += run("query failure remains retryable", queryFailureRemainsRetryable);
    failures += run("edit appearance requires owned slot with handle and copies snapshot", editAppearanceRequiresOwnedSlotWithHandleAndCopiesSnapshot);
    failures += run("local appearance edits normalize track dirty and Cancel without ticket", localAppearanceEditsNormalizeTrackDirtyAndCancelWithoutTicket);
    failures += run("advanced appearance edits track dirty normalize and forward all values", advancedAppearanceEditsTrackDirtyNormalizeAndForwardAllValues);
    failures += run("lock ticket refreshes snapshot and guards locked mutations", lockTicketRefreshesSnapshotAndGuardsLockedMutations);
    failures += run("Current Slots lock toggle does not navigate to Slot Actions", currentSlotLockToggleDoesNotNavigateToSlotActions);
    failures += run("changing area invalidates Lock completion", changingAreaInvalidatesLockCompletion);
    failures += run("appearance transaction blocks area navigation until completion", appearanceTransactionBlocksAreaNavigationUntilCompletion);
    failures += run("appearance Save creates one ticket and success refreshes only BODY", appearanceSaveCreatesOneTicketAndSuccessRefreshesOnlyBody);
    failures += run("appearance write failure retries full update and sync failure retries only sync", appearanceWriteFailureRetriesFullUpdateAndSyncFailureRetriesOnlySync);
    return failures == 0 ? 0 : 1;
}
