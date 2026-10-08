#include "core/TattooLoadoutPlanner.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace stui::core;

void expect(const bool condition, const std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

TattooEntry tattoo(
    std::string name,
    const TattooArea area = TattooArea::body,
    const std::int32_t slot = -1) {
    std::string areaName = "BODY";
    if (area == TattooArea::face) areaName = "FACE";
    if (area == TattooArea::hands) areaName = "HANDS";
    if (area == TattooArea::feet) areaName = "FEET";
    return TattooEntry{
        .domain = "default",
        .section = "Marks",
        .name = std::move(name),
        .area = std::move(areaName),
        .slot = slot,
        .color = 0xFFFFFF,
        .alpha = 1.0F,
        .emissiveMult = 1.0F,
    };
}

TattooLoadoutEntry desired(
    std::string name,
    const TattooArea area,
    const std::int32_t slot) {
    return TattooLoadoutEntry{
        .area = area,
        .slot = slot,
        .domain = "default",
        .section = "Marks",
        .name = std::move(name),
    };
}

TattooSlots slots(const TattooArea area, const std::int32_t count = 3) {
    TattooSlots result{.actorFormId = 0x14, .area = area, .configuredCount = count};
    for (std::int32_t index = 0; index < count; ++index) {
        result.slots.push_back(TattooSlot{.index = index});
    }
    return result;
}

std::array<TattooSlots, 4> emptyAreas() {
    return {slots(TattooArea::body), slots(TattooArea::face),
        slots(TattooArea::hands), slots(TattooArea::feet)};
}

TattooLoadoutPlanningInput input(TattooLoadout loadout) {
    return TattooLoadoutPlanningInput{
        .actorFormId = 0x14,
        .loadout = std::move(loadout),
        .slots = emptyAreas(),
    };
}

void classifiesExactRestoreActionsAcrossAreas() {
    auto request = input(TattooLoadout{
        .name = "Set",
        .entries = {
            desired("Keep", TattooArea::body, 0),
            desired("Recolor", TattooArea::body, 1),
            desired("Replace", TattooArea::face, 0),
            desired("Add", TattooArea::hands, 0),
        },
    });
    request.available = {
        tattoo("Keep"), tattoo("Recolor"), tattoo("Replace", TattooArea::face),
        tattoo("Add", TattooArea::hands)};
    auto keep = tattoo("Keep", TattooArea::body, 0);
    auto recolor = tattoo("Recolor", TattooArea::body, 1);
    recolor.color = 0;
    auto oldFace = tattoo("Old", TattooArea::face, 0);
    auto removedFeet = tattoo("Remove", TattooArea::feet, 1);
    request.slots[0].slots[0] = {.index = 0, .occupancy = SlotOccupancy::slaveTats, .tattoo = keep};
    request.slots[0].slots[1] = {.index = 1, .occupancy = SlotOccupancy::slaveTats, .tattoo = recolor};
    request.slots[1].slots[0] = {.index = 0, .occupancy = SlotOccupancy::slaveTats, .tattoo = oldFace};
    request.slots[3].slots[1] = {.index = 1, .occupancy = SlotOccupancy::slaveTats, .tattoo = removedFeet};

    const auto result = planTattooLoadout(request);

    expect(result.has_value(), "valid exact restore should plan");
    expect(result->actions.size() == 5, "expected five deterministic actions");
    expect(result->actions[0].kind == TattooLoadoutActionKind::unchanged,
        "matching Body slot should be unchanged");
    expect(result->actions[1].kind == TattooLoadoutActionKind::appearance,
        "appearance difference should not replace identity");
    expect(result->actions[2].kind == TattooLoadoutActionKind::replace,
        "different identity in target slot should replace");
    expect(result->actions[3].kind == TattooLoadoutActionKind::add,
        "empty target slot should add");
    expect(result->actions[4].kind == TattooLoadoutActionKind::remove,
        "managed tattoo absent from loadout should remove");
}

void emptyLoadoutRemovesManagedAndPreservesExternal() {
    auto request = input(TattooLoadout{.name = "Empty"});
    request.slots[0].slots[0] = {
        .index = 0, .occupancy = SlotOccupancy::slaveTats,
        .tattoo = tattoo("Remove", TattooArea::body, 0)};
    request.slots[0].slots[2] = {.index = 2, .occupancy = SlotOccupancy::external};

    const auto result = planTattooLoadout(request);

    expect(result && result->actions.size() == 1 &&
            result->actions[0].kind == TattooLoadoutActionKind::remove,
        "empty loadout should remove only managed tattoo");
    expect(result->preservedExternal.size() == 1 &&
            result->preservedExternal[0].area == TattooArea::body &&
            result->preservedExternal[0].slot == 2,
        "unrelated external slot should be preserved");
}

void rejectsMissingTattooInvalidSlotAndExternalConflictTogether() {
    auto request = input(TattooLoadout{
        .name = "Blocked",
        .entries = {
            desired("Missing", TattooArea::body, 0),
            desired("Too Far", TattooArea::face, 9),
            desired("External", TattooArea::hands, 1),
        },
    });
    request.available = {tattoo("Too Far", TattooArea::face), tattoo("External", TattooArea::hands)};
    request.slots[2].slots[1] = {.index = 1, .occupancy = SlotOccupancy::external};

    const auto result = planTattooLoadout(request);

    expect(!result && result.error().size() == 3, "all independent blockers should be reported");
    expect(result.error()[0].code == TattooLoadoutBlockerCode::missingTattoo,
        "Body missing tattoo should sort first");
    expect(result.error()[1].code == TattooLoadoutBlockerCode::invalidSlot,
        "Face invalid slot should sort second");
    expect(result.error()[2].code == TattooLoadoutBlockerCode::externalConflict,
        "Hands external conflict should sort third");
}

void lockedMutationRejectsButUnchangedLockedSlotPasses() {
    auto changed = input(TattooLoadout{
        .name = "Changed",
        .entries = {desired("Locked", TattooArea::body, 0)},
    });
    changed.available = {tattoo("Locked")};
    auto current = tattoo("Locked", TattooArea::body, 0);
    current.locked = true;
    current.color = 0;
    changed.slots[0].slots[0] = {
        .index = 0, .occupancy = SlotOccupancy::slaveTats, .tattoo = current};

    const auto rejected = planTattooLoadout(changed);
    expect(!rejected && rejected.error()[0].code == TattooLoadoutBlockerCode::lockedConflict,
        "locked appearance write should be rejected");

    changed.loadout.entries[0].color = 0;
    const auto unchanged = planTattooLoadout(changed);
    expect(unchanged && unchanged->actions[0].kind == TattooLoadoutActionKind::unchanged,
        "unchanged locked tattoo should pass without mutation");
}

void rejectsActorAndAreaSnapshotMismatch() {
    auto request = input(TattooLoadout{.name = "Set"});
    request.slots[1].actorFormId = 0x1234;
    request.slots[2].area = TattooArea::feet;

    const auto result = planTattooLoadout(request);

    expect(!result && result.error().size() == 2,
        "actor and area snapshot mismatches should both block");
    expect(result.error()[0].code == TattooLoadoutBlockerCode::actorMismatch &&
            result.error()[1].code == TattooLoadoutBlockerCode::areaMismatch,
        "snapshot blockers should be deterministic");
}

void rejectsDuplicateDesiredPosition() {
    auto request = input(TattooLoadout{
        .name = "Duplicate",
        .entries = {
            desired("First", TattooArea::body, 1),
            desired("Second", TattooArea::body, 1),
        },
    });
    request.available = {tattoo("First"), tattoo("Second")};

    const auto result = planTattooLoadout(request);

    expect(!result, "duplicate desired area and slot must reject the whole plan");
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
    int failures = 0;
    failures += run("classifies exact restore actions", classifiesExactRestoreActionsAcrossAreas);
    failures += run("empty loadout preserves external", emptyLoadoutRemovesManagedAndPreservesExternal);
    failures += run("reports independent blockers",
        rejectsMissingTattooInvalidSlotAndExternalConflictTogether);
    failures += run("locked mutation rejects but unchanged passes",
        lockedMutationRejectsButUnchangedLockedSlotPasses);
    failures += run("rejects snapshot mismatches", rejectsActorAndAreaSnapshotMismatch);
    failures += run("rejects duplicate desired position", rejectsDuplicateDesiredPosition);
    return failures == 0 ? 0 : 1;
}
