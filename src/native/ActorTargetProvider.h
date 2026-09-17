#pragma once

#include "native/ActorTarget.h"

#include <cstdint>
#include <functional>
#include <string>

namespace stui::native {

struct ActorTargetProviderBindings {
    std::function<void*()> resolveCrosshairActor;
    std::function<std::uint32_t(void*)> formId;
    std::function<bool(void*)> is3DLoaded;
    std::function<std::string(void*)> displayName;
};

[[nodiscard]] ActorTargetResult resolveCrosshairActorTarget(
    const ActorTargetProviderBindings& bindings);
[[nodiscard]] ActorTargetResult resolveCrosshairActorTarget();

}  // namespace stui::native
