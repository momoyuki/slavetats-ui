#include "native/ActorTargetProvider.h"

#include <RE/Skyrim.h>

#include <utility>

namespace stui::native {
namespace {

ActorTargetResult actorNotFound() {
    return std::unexpected(core::ServiceError{
        .code = core::ServiceErrorCode::actorNotFound,
        .message = "No valid crosshair Actor.",
    });
}

std::string normalizeDisplayName(std::string displayName) {
    return displayName.empty() ? "Unnamed Actor" : std::move(displayName);
}

}  // namespace

ActorTargetResult resolveCrosshairActorTarget(const ActorTargetProviderBindings& bindings) {
    if (!bindings.resolveCrosshairActor || !bindings.formId || !bindings.is3DLoaded ||
        !bindings.displayName) {
        return actorNotFound();
    }

    void* const actor = bindings.resolveCrosshairActor();
    if (!actor) {
        return actorNotFound();
    }

    const auto formId = bindings.formId(actor);
    if (formId == 0 || !bindings.is3DLoaded(actor)) {
        return actorNotFound();
    }

    return ActorTarget{
        .kind = ActorTargetKind::crosshair,
        .formId = formId,
        .displayName = normalizeDisplayName(bindings.displayName(actor)),
    };
}

ActorTargetResult resolveCrosshairActorTarget() {
    auto* const crosshairPickData = RE::CrosshairPickData::GetSingleton();
    if (!crosshairPickData) {
        return actorNotFound();
    }

    const auto actorReference = [&]() {
        if (REL::Module::IsVR()) {
            for (const auto device : {
                     RE::VR_DEVICE::kLeftController,
                     RE::VR_DEVICE::kRightController,
                     RE::VR_DEVICE::kHeadset,
                 }) {
                if (auto reference = crosshairPickData->targetActor[device].get()) {
                    return reference;
                }
            }
            return decltype(crosshairPickData->targetActor[0].get()){};
        }

        return crosshairPickData->targetActor[0].get();
    }();
    auto* const actor = actorReference ? actorReference->As<RE::Actor>() : nullptr;
    if (!actor) {
        return actorNotFound();
    }

    const auto formId = actor->GetFormID();
    if (formId == 0 || !actor->Is3DLoaded()) {
        return actorNotFound();
    }

    const char* const displayName = actor->GetDisplayFullName();
    return ActorTarget{
        .kind = ActorTargetKind::crosshair,
        .formId = formId,
        .displayName = normalizeDisplayName(displayName ? displayName : ""),
    };
}

}  // namespace stui::native
