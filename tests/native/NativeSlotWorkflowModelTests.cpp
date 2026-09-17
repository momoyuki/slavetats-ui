#include "native/NativeSlotWorkflowModel.h"

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
using stui::core::RemoveTattooSuccess;
using stui::core::RemoveTattooMode;
using stui::core::ServiceError;
using stui::core::ServiceErrorCode;
using stui::core::SetTattooLockedSuccess;
using stui::core::SlotOccupancy;
using stui::core::TattooArea;
using stui::core::TattooEntry;
using stui::core::TattooSlot;
using stui::core::TattooSlots;
using stui::core::UpdateTattooAppearanceMode;
using stui::core::UpdateTattooAppearanceSuccess;
using stui::native::NativeCatalogBrowserModel;
using stui::native::NativeSlotWorkflowModel;
using stui::native::SlotWorkflowScreen;
using stui::repository::TattooCatalog;
using stui::repository::TattooCatalogSnapshot;
using stui::repository::TattooDefinition;

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
            !model.targetSlot() && !model.previewAppearance(),
        "expected Cancel to discard target appearance and return to Current Slots");
    expect(catalog.filter().search == filterBefore.search &&
            catalog.filter().sourceId == filterBefore.sourceId &&
            catalog.filter().section == filterBefore.section &&
            catalog.filter().area == filterBefore.area &&
            catalog.page().pageIndex == pageBefore,
        "expected Cancel to preserve picker filters and page for the next target");
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

void selectingAnotherAreaInvalidatesMatchingAppearanceCompletion() {
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
    const auto faceQuery = model.takeSlotQuery();
    expect(faceQuery && faceQuery->area == TattooArea::face,
        "expected navigation to schedule only the selected FACE query");

    model.completeAppearanceUpdate(ticket->generation, UpdateTattooAppearanceSuccess{
        .actorFormId = 0x14,
        .runtimeHandle = 73,
    });

    expect(model.selectedArea() == TattooArea::face &&
            model.screen() == SlotWorkflowScreen::currentSlots &&
            !model.editAppearance() && !model.error(),
        "expected formerly matching completion not to mutate navigated workflow state");
    expect(!model.takeSlotQuery(),
        "expected obsolete appearance completion not to schedule a refresh");
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
    failures += run("preview does not apply and Cancel returns to Slots", previewDoesNotApplyAndCancelReturnsToSlots);
    failures += run("explicit confirmation creates one exact-domain policy request", explicitConfirmationCreatesOneExactDomainPolicyRequest);
    failures += run("apply success returns to slots and refreshes area", applySuccessReturnsToSlotsAndRefreshesArea);
    failures += run("apply failure retains Preview for retry", applyFailureRetainsPreviewForRetry);
    failures += run("stale completions are ignored", staleCompletionsAreIgnored);
    failures += run("query failure remains retryable", queryFailureRemainsRetryable);
    failures += run("edit appearance requires owned slot with handle and copies snapshot", editAppearanceRequiresOwnedSlotWithHandleAndCopiesSnapshot);
    failures += run("local appearance edits normalize track dirty and Cancel without ticket", localAppearanceEditsNormalizeTrackDirtyAndCancelWithoutTicket);
    failures += run("advanced appearance edits track dirty normalize and forward all values", advancedAppearanceEditsTrackDirtyNormalizeAndForwardAllValues);
    failures += run("lock ticket refreshes snapshot and guards locked mutations", lockTicketRefreshesSnapshotAndGuardsLockedMutations);
    failures += run("Current Slots lock toggle does not navigate to Slot Actions", currentSlotLockToggleDoesNotNavigateToSlotActions);
    failures += run("changing area invalidates Lock completion", changingAreaInvalidatesLockCompletion);
    failures += run("selecting another area invalidates matching appearance completion", selectingAnotherAreaInvalidatesMatchingAppearanceCompletion);
    failures += run("appearance Save creates one ticket and success refreshes only BODY", appearanceSaveCreatesOneTicketAndSuccessRefreshesOnlyBody);
    failures += run("appearance write failure retries full update and sync failure retries only sync", appearanceWriteFailureRetriesFullUpdateAndSyncFailureRetriesOnlySync);
    return failures == 0 ? 0 : 1;
}
