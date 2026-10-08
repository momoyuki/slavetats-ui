#include "core/TattooLoadoutPlanner.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <tuple>
#include <utility>

namespace stui::core {
namespace {

constexpr std::array<TattooArea, 4> kAreaOrder{
    TattooArea::body, TattooArea::face, TattooArea::hands, TattooArea::feet};

int areaIndex(const TattooArea area) {
    switch (area) {
    case TattooArea::body: return 0;
    case TattooArea::face: return 1;
    case TattooArea::hands: return 2;
    case TattooArea::feet: return 3;
    }
    return 4;
}

std::string folded(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        result.push_back(static_cast<char>(std::tolower(character)));
    }
    return result;
}

bool areaMatches(const TattooEntry& tattoo, const TattooArea area) {
    static constexpr std::array<std::string_view, 4> names{"body", "face", "hands", "feet"};
    return folded(tattoo.area) == names[areaIndex(area)];
}

bool identityMatches(const TattooEntry& current, const TattooLoadoutEntry& desired) {
    return current.domain == desired.domain && current.section == desired.section &&
        current.name == desired.name;
}

bool appearanceMatches(const TattooEntry& current, const TattooLoadoutEntry& desired) {
    return current.color == desired.color && current.alpha == desired.alpha &&
        current.glow == desired.glow && current.emissiveMult == desired.emissiveMult &&
        current.glossiness == desired.glossiness &&
        current.specularStrength == desired.specularStrength;
}

bool availableContains(
    const std::vector<TattooEntry>& available,
    const TattooLoadoutEntry& desired) {
    return std::ranges::any_of(available, [&](const TattooEntry& candidate) {
        return identityMatches(candidate, desired) && areaMatches(candidate, desired.area);
    });
}

const TattooSlot* findSlot(const TattooSlots& snapshot, const std::int32_t index) {
    const auto found = std::ranges::find_if(snapshot.slots, [&](const TattooSlot& slot) {
        return slot.index == index;
    });
    return found == snapshot.slots.end() ? nullptr : &*found;
}

TattooLoadoutBlocker blocker(
    const TattooLoadoutBlockerCode code,
    const TattooArea area,
    const std::int32_t slot,
    std::string message) {
    return {.code = code, .area = area, .slot = slot, .message = std::move(message)};
}

}  // namespace

TattooLoadoutPlanResult planTattooLoadout(const TattooLoadoutPlanningInput& input) {
    TattooLoadoutPlan plan{.actorFormId = input.actorFormId};
    std::vector<TattooLoadoutBlocker> blockers;
    std::map<std::pair<int, std::int32_t>, const TattooLoadoutEntry*> desiredBySlot;

    for (const auto& desired : input.loadout.entries) {
        const bool inserted = desiredBySlot.emplace(
            std::pair{areaIndex(desired.area), desired.slot}, &desired).second;
        if (!inserted) {
            blockers.push_back(blocker(TattooLoadoutBlockerCode::duplicatePosition,
                desired.area, desired.slot, "Loadout contains duplicate area and slot entries."));
        }
    }

    for (std::size_t index = 0; index < input.slots.size(); ++index) {
        const auto expectedArea = kAreaOrder[index];
        const auto& snapshot = input.slots[index];
        if (snapshot.actorFormId != input.actorFormId) {
            blockers.push_back(blocker(TattooLoadoutBlockerCode::actorMismatch,
                expectedArea, -1, "Slot snapshot belongs to a different Actor."));
        }
        if (snapshot.area != expectedArea) {
            blockers.push_back(blocker(TattooLoadoutBlockerCode::areaMismatch,
                expectedArea, -1, "Slot snapshot belongs to a different area."));
        }
    }

    for (const auto& desired : input.loadout.entries) {
        const auto& snapshot = input.slots[areaIndex(desired.area)];
        if (desired.slot < 0 || desired.slot >= snapshot.configuredCount ||
            !findSlot(snapshot, desired.slot)) {
            blockers.push_back(blocker(TattooLoadoutBlockerCode::invalidSlot,
                desired.area, desired.slot, "Loadout target slot is not configured."));
            continue;
        }
        if (!availableContains(input.available, desired)) {
            blockers.push_back(blocker(TattooLoadoutBlockerCode::missingTattoo,
                desired.area, desired.slot, "Loadout tattoo is not installed."));
        }
    }

    for (std::size_t area = 0; area < input.slots.size(); ++area) {
        const auto expectedArea = kAreaOrder[area];
        const auto& snapshot = input.slots[area];
        auto orderedSlots = snapshot.slots;
        std::ranges::sort(orderedSlots, {}, &TattooSlot::index);
        for (const auto& slot : orderedSlots) {
            const auto desired = desiredBySlot.find(std::pair{static_cast<int>(area), slot.index});
            const auto* after = desired == desiredBySlot.end() ? nullptr : desired->second;
            if (slot.occupancy == SlotOccupancy::external) {
                if (after) {
                    blockers.push_back(blocker(TattooLoadoutBlockerCode::externalConflict,
                        expectedArea, slot.index, "Loadout target slot is externally managed."));
                } else {
                    plan.preservedExternal.push_back({.area = expectedArea, .slot = slot.index});
                }
                continue;
            }

            const TattooEntry* before = slot.occupancy == SlotOccupancy::slaveTats && slot.tattoo ?
                &*slot.tattoo : nullptr;
            std::optional<TattooLoadoutActionKind> kind;
            if (!before && after) {
                kind = TattooLoadoutActionKind::add;
            } else if (before && !after) {
                kind = TattooLoadoutActionKind::remove;
            } else if (before && after && !identityMatches(*before, *after)) {
                kind = TattooLoadoutActionKind::replace;
            } else if (before && after && !appearanceMatches(*before, *after)) {
                kind = TattooLoadoutActionKind::appearance;
            } else if (before && after) {
                kind = TattooLoadoutActionKind::unchanged;
            }
            if (!kind) continue;
            if (before && before->locked && *kind != TattooLoadoutActionKind::unchanged) {
                blockers.push_back(blocker(TattooLoadoutBlockerCode::lockedConflict,
                    expectedArea, slot.index, "Locked tattoo would require mutation."));
                continue;
            }
            plan.actions.push_back({
                .kind = *kind,
                .area = expectedArea,
                .slot = slot.index,
                .before = before ? std::optional<TattooEntry>(*before) : std::nullopt,
                .after = after ? std::optional<TattooLoadoutEntry>(*after) : std::nullopt,
            });
        }
    }

    if (!blockers.empty()) {
        std::ranges::sort(blockers, [](const auto& left, const auto& right) {
            return std::tuple{areaIndex(left.area), left.slot, left.code} <
                std::tuple{areaIndex(right.area), right.slot, right.code};
        });
        return std::unexpected(std::move(blockers));
    }
    return plan;
}

}  // namespace stui::core
