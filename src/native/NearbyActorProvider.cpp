#include "native/NearbyActorProvider.h"

#include <RE/Skyrim.h>

#include <cmath>
#include <utility>

namespace stui::native {
namespace {

NearbyActorScanResult unavailable() {
    return std::unexpected(core::ServiceError{
        .code = core::ServiceErrorCode::actorNotFound,
        .message = "Nearby Actor scan is unavailable.",
    });
}

}  // namespace

NearbyActorScanResult scanNearbyActors(const NearbyActorProviderBindings& bindings) {
    if (!bindings.enumerate) return unavailable();
    return normalizeNearbyActorCandidates(bindings.enumerate());
}

NearbyActorScanResult scanNearbyActors() {
    auto* const player = RE::PlayerCharacter::GetSingleton();
    auto* const processLists = RE::ProcessLists::GetSingleton();
    if (!player || !processLists) return unavailable();

    const auto playerPosition = player->GetPosition();
    NearbyActorList candidates;
    processLists->ForAllActors([&](RE::Actor* actor) {
        if (!actor || actor == player || actor->GetFormID() == 0 || !actor->Is3DLoaded()) {
            return RE::BSContainer::ForEachResult::kContinue;
        }
        const auto distance = playerPosition.GetDistance(actor->GetPosition());
        if (!std::isfinite(distance) || distance > kNearbyActorScanRadius) {
            return RE::BSContainer::ForEachResult::kContinue;
        }
        const char* const displayName = actor->GetDisplayFullName();
        candidates.push_back({
            .target = ActorTarget{
                .kind = ActorTargetKind::nearby,
                .formId = actor->GetFormID(),
                .displayName = displayName && *displayName ? displayName : "Unnamed Actor",
            },
            .distance = distance,
        });
        return RE::BSContainer::ForEachResult::kContinue;
    });
    return normalizeNearbyActorCandidates(std::move(candidates));
}

}  // namespace stui::native
