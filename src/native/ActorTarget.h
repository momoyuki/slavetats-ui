#pragma once

#include "core/TattooModels.h"

#include <cstdint>
#include <expected>
#include <string>

namespace stui::native {

enum class ActorTargetKind {
    player,
    crosshair,
};

struct ActorTarget {
    ActorTargetKind kind{ActorTargetKind::player};
    std::uint32_t formId{0x14};
    std::string displayName{"Player"};
};

using ActorTargetResult = std::expected<ActorTarget, core::ServiceError>;

}  // namespace stui::native
