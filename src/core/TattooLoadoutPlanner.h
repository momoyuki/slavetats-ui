#pragma once

#include "core/TattooLoadoutModels.h"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace stui::core {

enum class TattooLoadoutActionKind { add, replace, remove, appearance, unchanged };

enum class TattooLoadoutBlockerCode {
    actorMismatch,
    areaMismatch,
    duplicatePosition,
    invalidSlot,
    missingTattoo,
    externalConflict,
    lockedConflict,
};

struct TattooLoadoutBlocker {
    TattooLoadoutBlockerCode code{TattooLoadoutBlockerCode::actorMismatch};
    TattooArea area{TattooArea::body};
    std::int32_t slot{-1};
    std::string message;

    bool operator==(const TattooLoadoutBlocker&) const = default;
};

struct TattooLoadoutPlanAction {
    TattooLoadoutActionKind kind{TattooLoadoutActionKind::unchanged};
    TattooArea area{TattooArea::body};
    std::int32_t slot{-1};
    std::optional<TattooEntry> before;
    std::optional<TattooLoadoutEntry> after;

    bool operator==(const TattooLoadoutPlanAction&) const = default;
};

struct TattooLoadoutSlotRef {
    TattooArea area{TattooArea::body};
    std::int32_t slot{-1};

    bool operator==(const TattooLoadoutSlotRef&) const = default;
};

struct TattooLoadoutPlan {
    std::uint32_t actorFormId{};
    std::vector<TattooLoadoutPlanAction> actions;
    std::vector<TattooLoadoutSlotRef> preservedExternal;

    bool operator==(const TattooLoadoutPlan&) const = default;
};

struct TattooLoadoutPlanningInput {
    std::uint32_t actorFormId{};
    TattooLoadout loadout;
    std::array<TattooSlots, 4> slots;
    std::vector<TattooEntry> available;
};

using TattooLoadoutPlanResult =
    std::expected<TattooLoadoutPlan, std::vector<TattooLoadoutBlocker>>;

[[nodiscard]] TattooLoadoutPlanResult planTattooLoadout(
    const TattooLoadoutPlanningInput& input);

}  // namespace stui::core
