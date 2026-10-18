#include "runtime/CatalogPreviewRuntimeSession.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace stui;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

struct Fixture {
    core::TattooSlots slots{
        .actorFormId = 0x14,
        .area = core::TattooArea::body,
        .configuredCount = 1,
        .slots = {{.index = 0, .occupancy = core::SlotOccupancy::empty}},
    };
    runtime::CatalogPreviewVisualTarget target{.actor = 1, .root = 2, .geometry = 3,
        .material = 4, .nodeName = "Body [Ovl0]"};
    runtime::CatalogPreviewVisualSnapshot visual{
        .diffuse = "blank.dds", .normal = "blank_n.dds", .glowTexture = "blank_g.dds"};
    int writes{};
    int applies{};
    bool failSynchronization{};
    bool failBeforeMutation{};

    runtime::CatalogPreviewRuntimeSession makeSession() {
        return runtime::CatalogPreviewRuntimeSession(
            [this](std::uint32_t, core::TattooArea) -> core::TattooSlotsResult {
                return slots;
            },
            {
                .resolve = [this](const runtime::CatalogPreviewVisualContext&) {
                    return std::optional(target);
                },
                .capture = [this](const runtime::CatalogPreviewVisualTarget&) {
                    return std::optional(visual);
                },
                .write = [this](const runtime::CatalogPreviewVisualTarget&,
                             const runtime::CatalogPreviewVisualSnapshot& value) {
                    ++writes;
                    visual = value;
                    return true;
                },
            },
            [this](const core::ApplyTattooRequest& request) -> core::ApplyTattooResult {
                ++applies;
                if (failBeforeMutation) return std::unexpected(core::ServiceError{
                    core::ServiceErrorCode::applyFailed, "Apply rejected"});
                if (failSynchronization) return std::unexpected(core::ServiceError{
                    core::ServiceErrorCode::synchronizeFailed, "Synchronization failed",
                    request.mode == core::ApplyTattooMode::applyAndSynchronize
                        ? core::MutationSideEffect::mayHaveOccurred
                        : core::MutationSideEffect::none});
                return core::ApplyTattooSuccess{request.actorFormId, request.area,
                    request.slot, request.section, request.name};
            });
    }
};

core::TattooEntry candidate() {
    return core::TattooEntry{.section = "Pack", .name = "Mark",
        .texturePath = "mark.dds", .area = "Body"};
}

void validatesAuthorityOnEveryOperation() {
    Fixture fixture;
    auto session = fixture.makeSession();
    const core::CatalogPreviewBeginRequest context{0x14, 4, core::TattooArea::body, 0};
    const auto begun = session.begin(context);
    expect(begun.has_value(), "empty managed slot should start preview");
    expect(session.paint({begun->sessionId, context, candidate()}).has_value(),
        "paint should succeed");
    fixture.slots.slots[0].occupancy = core::SlotOccupancy::external;

    const auto restore = session.restore({begun->sessionId, context});
    expect(!restore && restore.error().code == core::ServiceErrorCode::previewConflict,
        "foreign slot change should block restore write");
    expect(fixture.writes == 1, "foreign slot must not receive a restore write");
}

void untouchedSessionCanCloseAfterSlotChanges() {
    Fixture fixture;
    auto session = fixture.makeSession();
    const core::CatalogPreviewBeginRequest context{0x14, 4, core::TattooArea::body, 0};
    const auto begun = session.begin(context);
    expect(begun.has_value(), "empty slot should start an untouched preview");
    fixture.slots.slots[0].occupancy = core::SlotOccupancy::external;
    fixture.target.material = 99;
    expect(session.restore({begun->sessionId, context}).has_value(),
        "untouched preview should close after foreign slot and material changes");
    expect(fixture.writes == 0,
        "closing after foreign changes must not write to that material");
}

void rejectsLockedAndExternalSlotsBeforeCapture() {
    Fixture fixture;
    fixture.slots.slots[0] = {.index = 0, .occupancy = core::SlotOccupancy::external};
    auto session = fixture.makeSession();
    const core::CatalogPreviewBeginRequest context{0x14, 4, core::TattooArea::body, 0};
    expect(!session.begin(context), "external slot should be rejected");

    fixture.slots.slots[0] = {.index = 0, .occupancy = core::SlotOccupancy::slaveTats,
        .tattoo = core::TattooEntry{.runtimeHandle = 9, .section = "Pack", .name = "Old",
            .locked = true}};
    expect(!session.begin(context), "locked slot should be rejected");
    fixture.slots.slots[0] = {.index = 0, .occupancy = core::SlotOccupancy::slaveTats};
    expect(!session.begin(context), "managed slot without a tattoo must be rejected");
    fixture.slots.slots[0].tattoo = core::TattooEntry{
        .runtimeHandle = 0, .section = "Pack", .name = "Old"};
    expect(!session.begin(context), "managed slot without a live handle must be rejected");
    fixture.slots.slots[0].occupancy = core::SlotOccupancy::empty;
    expect(!session.begin(context), "empty slot carrying a tattoo must be rejected");
    fixture.slots.slots[0] = {.index = 0,
        .occupancy = static_cast<core::SlotOccupancy>(99)};
    expect(!session.begin(context), "unknown slot occupancy must be rejected");
    expect(fixture.writes == 0, "rejected begin must not write visual state");
}

void rejectsStaleSessionWithoutPlayerFallback() {
    Fixture fixture;
    auto session = fixture.makeSession();
    const core::CatalogPreviewBeginRequest context{0x123, 4, core::TattooArea::body, 0};
    fixture.slots.actorFormId = 0x123;
    const auto begun = session.begin(context);
    expect(begun.has_value(), "explicit NPC should start preview");

    auto wrong = context;
    wrong.actorFormId = 0x14;
    expect(!session.paint({begun->sessionId, wrong, candidate()}),
        "different Actor must be rejected");
    expect(fixture.writes == 0, "stale actor request must not write or fall back");
}

void rejectsSaveAfterVisualTargetIsRebuilt() {
    Fixture fixture;
    auto session = fixture.makeSession();
    const core::CatalogPreviewBeginRequest context{0x14, 7, core::TattooArea::body, 0};
    const auto begun = session.begin(context);
    expect(begun.has_value(), "preview should begin before material rebuild");
    expect(session.paint({begun->sessionId, context, candidate()}).has_value(),
        "candidate should paint before material rebuild");
    fixture.target.material = 99;
    const core::CatalogPreviewCommitRequest commit{begun->sessionId, context,
        core::ApplyTattooRequest{.actorFormId = 0x14, .area = core::TattooArea::body,
            .slot = 0, .section = "Pack", .name = "Mark"}};
    const auto result = session.commit(commit);
    expect(!result && result.error().code == core::ServiceErrorCode::previewConflict,
        "Save must reject a rebuilt visual target");
    expect(fixture.applies == 0, "rejected Save must not mutate SlaveTats records");
}

void rejectsAuthoritativeTattooChangesWithSameHandleAndName() {
    Fixture fixture;
    fixture.slots.slots[0] = {
        .index = 0,
        .occupancy = core::SlotOccupancy::slaveTats,
        .tattoo = core::TattooEntry{
            .runtimeHandle = 9,
            .domain = "default",
            .section = "Pack",
            .name = "Old",
            .texturePath = "old.dds",
            .area = "Body",
            .slot = 0,
            .color = 0x112233,
        },
    };
    auto session = fixture.makeSession();
    const core::CatalogPreviewBeginRequest context{0x14, 9, core::TattooArea::body, 0};
    const auto begun = session.begin(context);
    expect(begun.has_value(), "managed tattoo should start preview");
    fixture.slots.slots[0].tattoo->texturePath = "replaced.dds";
    expect(!session.paint({begun->sessionId, context, candidate()}),
        "changed authoritative texture must block preview paint");
    const core::CatalogPreviewCommitRequest commit{begun->sessionId, context,
        core::ApplyTattooRequest{.actorFormId = 0x14, .area = core::TattooArea::body,
            .slot = 0, .section = "Pack", .name = "Mark"}};
    expect(!session.commit(commit) && fixture.writes == 0 && fixture.applies == 0,
        "changed authoritative texture must block visual and storage writes");
    fixture.slots.slots[0].tattoo->texturePath = "old.dds";
    fixture.slots.slots[0].tattoo->color = 0x445566;
    expect(!session.paint({begun->sessionId, context, candidate()}) &&
            !session.commit(commit) && fixture.writes == 0 && fixture.applies == 0,
        "changed authoritative appearance must also block preview and Save");
    fixture.slots.slots[0].tattoo->color = 0x112233;
    expect(session.restore({begun->sessionId, context}).has_value(),
        "unchanged authoritative state permits ending the session");
}

void hoverExitKeepsSessionForNextTattoo() {
    Fixture fixture;
    const auto original = fixture.visual;
    auto session = fixture.makeSession();
    const core::CatalogPreviewBeginRequest context{0x14, 5, core::TattooArea::body, 0};
    const auto begun = session.begin(context);
    expect(begun.has_value(), "preview should begin");
    expect(session.paint({begun->sessionId, context, candidate()}).has_value(),
        "first hover should paint");
    expect(session.paint({begun->sessionId, context, std::nullopt}).has_value(),
        "hover exit should clear visual");
    expect(fixture.visual == original, "hover exit should restore exact original");
    expect(session.paint({begun->sessionId, context, candidate()}).has_value(),
        "second hover should paint within same session");
    expect(session.restore({begun->sessionId, context}).has_value(),
        "close should restore and end session");
    expect(fixture.visual == original, "close should restore exact original");
    expect(!session.paint({begun->sessionId, context, candidate()}),
        "paint after close must reject stale session");
}

void worldInvalidationRejectsOldPreviewOperations() {
    Fixture fixture;
    auto session = fixture.makeSession();
    const core::CatalogPreviewBeginRequest context{0x14, 8, core::TattooArea::body, 0};
    const auto begun = session.begin(context);
    expect(begun.has_value(), "preview should begin before world invalidation");
    expect(session.paint({begun->sessionId, context, candidate()}).has_value(),
        "preview should paint before world invalidation");
    const int writesBeforeInvalidation = fixture.writes;
    session.invalidate();
    const core::CatalogPreviewCommitRequest commit{begun->sessionId, context,
        core::ApplyTattooRequest{.actorFormId = 0x14, .area = core::TattooArea::body,
            .slot = 0, .section = "Pack", .name = "Mark"}};
    expect(!session.paint({begun->sessionId, context, candidate()}) &&
        !session.restore({begun->sessionId, context}) && !session.commit(commit),
        "old world preview operations must reject after invalidation");
    expect(fixture.writes == writesBeforeInvalidation && fixture.applies == 0,
        "old world completion must not write a visual or authoritative tattoo");
    const auto next = session.begin(context);
    expect(next && next->sessionId != begun->sessionId,
        "new world preview must receive a different session token");
}

void saveChecksSlotAndRetriesSynchronizationOnly() {
    Fixture fixture;
    auto session = fixture.makeSession();
    const core::CatalogPreviewBeginRequest context{0x14, 6, core::TattooArea::body, 0};
    const auto begun = session.begin(context);
    expect(begun.has_value(), "Save test should begin preview");
    core::CatalogPreviewCommitRequest commit{begun->sessionId, context,
        core::ApplyTattooRequest{.actorFormId = 0x14, .area = core::TattooArea::body,
            .slot = 0, .section = "Pack", .name = "Mark"}};
    auto wrong = commit;
    wrong.apply.actorFormId = 0x123;
    expect(!session.commit(wrong) && fixture.applies == 0,
        "Save must never fall back to a different Actor");
    fixture.failBeforeMutation = true;
    expect(!session.commit(commit), "pre-write failure should be reported");
    expect(session.restore({begun->sessionId, context}).has_value(),
        "pre-write failure should remain cancellable");

    auto next = session.begin(context);
    expect(next.has_value(), "a fresh session should start after cancellation");
    commit.sessionId = next->sessionId;
    fixture.failBeforeMutation = false;
    fixture.failSynchronization = true;
    const auto first = session.commit(commit);
    expect(!first && first.error().mutationSideEffect ==
        core::MutationSideEffect::mayHaveOccurred,
        "failed synchronization must report the completed mutation");
    expect(!session.restore({next->sessionId, context}),
        "Close must not restore the old visual after a completed mutation");
    expect(!session.begin(context), "another preview must wait for synchronization retry");
    auto retry = commit;
    retry.apply.mode = core::ApplyTattooMode::synchronizeOnly;
    wrong = retry;
    wrong.apply.name = "Other";
    expect(!session.commit(wrong), "retry must keep the frozen Save identity");
    fixture.failSynchronization = false;
    const auto saved = session.commit(retry);
    expect(saved.has_value() && fixture.applies == 3,
        "retry should perform only one synchronization call after one mutation");
    expect(!session.commit(retry), "completed retry token must be single use");
    expect(session.begin(context).has_value(), "new preview may start after successful retry");
}

}  // namespace

int main() {
    validatesAuthorityOnEveryOperation();
    untouchedSessionCanCloseAfterSlotChanges();
    rejectsLockedAndExternalSlotsBeforeCapture();
    rejectsStaleSessionWithoutPlayerFallback();
    rejectsSaveAfterVisualTargetIsRebuilt();
    rejectsAuthoritativeTattooChangesWithSameHandleAndName();
    hoverExitKeepsSessionForNextTattoo();
    worldInvalidationRejectsOldPreviewOperations();
    saveChecksSlotAndRetriesSynchronizationOnly();
    std::cout << "CatalogPreviewRuntimeSessionTests passed\n";
}
