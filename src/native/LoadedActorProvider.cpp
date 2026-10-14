#include "native/LoadedActorProvider.h"

#include <RE/Skyrim.h>

#include <utility>

namespace stui::native {
namespace {

LoadedActorScanResult unavailable() {
    return std::unexpected(core::ServiceError{
        .code = core::ServiceErrorCode::actorNotFound,
        .message = "Explicit NPC scan is unavailable.",
    });
}

}  // namespace

LoadedActorScanResult scanLoadedActors(const LoadedActorProviderBindings& bindings) {
    if (!bindings.enumerate) return unavailable();
    return normalizeLoadedActorCandidates(bindings.enumerate());
}

LoadedActorScanResult scanLoadedActors() {
    auto* const player = RE::PlayerCharacter::GetSingleton();
    auto* const processLists = RE::ProcessLists::GetSingleton();
    if (!player || !processLists) return unavailable();

    const auto playerPosition = player->GetPosition();
    LoadedActorList candidates;
    processLists->ForAllActors([&](RE::Actor* actor) {
        if (!actor || actor == player || actor->GetFormID() == 0 || !actor->Is3DLoaded()) {
            return RE::BSContainer::ForEachResult::kContinue;
        }
        const auto distance = playerPosition.GetDistance(actor->GetPosition());
        const char* const displayName = actor->GetDisplayFullName();
        candidates.push_back({
            .target = ActorTarget{
                .kind = ActorTargetKind::explicitNpc,
                .formId = actor->GetFormID(),
                .displayName = displayName && *displayName ? displayName : "Unnamed Actor",
            },
            .distance = distance,
            .isPlayerTeammate = actor->IsPlayerTeammate(),
        });
        return RE::BSContainer::ForEachResult::kContinue;
    });
    return normalizeLoadedActorCandidates(std::move(candidates));
}

}  // namespace stui::native
