#pragma once

#include "native/ActorTarget.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace stui::native {

inline constexpr std::size_t kMaxExplicitNpcCandidates = 64;

struct LoadedActorCandidate {
    ActorTarget target;
    float distance{};
    bool isPlayerTeammate{};

    bool operator==(const LoadedActorCandidate&) const = default;
};

using LoadedActorList = std::vector<LoadedActorCandidate>;
using LoadedActorScanResult = std::expected<LoadedActorList, core::ServiceError>;

struct LoadedActorProviderBindings {
    std::function<LoadedActorList()> enumerate;
};

[[nodiscard]] inline bool isValidLoadedActorCandidate(
    const LoadedActorCandidate& candidate) noexcept {
    return candidate.target.kind == ActorTargetKind::explicitNpc &&
        candidate.target.formId != 0 && std::isfinite(candidate.distance) &&
        candidate.distance >= 0.0F;
}

[[nodiscard]] inline bool loadedActorNameLess(
    std::string_view left, std::string_view right) noexcept {
    const auto length = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < length; ++index) {
        const auto leftChar = static_cast<unsigned char>(left[index]);
        const auto rightChar = static_cast<unsigned char>(right[index]);
        const auto leftLower = static_cast<unsigned char>(std::tolower(leftChar));
        const auto rightLower = static_cast<unsigned char>(std::tolower(rightChar));
        if (leftLower != rightLower) return leftLower < rightLower;
    }
    return left.size() < right.size();
}

[[nodiscard]] inline LoadedActorList normalizeLoadedActorCandidates(
    LoadedActorList candidates) {
    for (auto& candidate : candidates) {
        if (candidate.target.displayName.empty()) {
            candidate.target.displayName = "Unnamed Actor";
        }
    }
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [](const auto& candidate) {
        return !isValidLoadedActorCandidate(candidate);
    }), candidates.end());
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        if (loadedActorNameLess(left.target.displayName, right.target.displayName)) return true;
        if (loadedActorNameLess(right.target.displayName, left.target.displayName)) return false;
        return left.target.formId < right.target.formId;
    });

    LoadedActorList result;
    result.reserve(std::min(candidates.size(), kMaxExplicitNpcCandidates));
    for (auto& candidate : candidates) {
        const auto duplicate = std::find_if(result.begin(), result.end(), [&](const auto& existing) {
            return existing.target.formId == candidate.target.formId;
        });
        if (duplicate != result.end()) continue;
        result.push_back(std::move(candidate));
        if (result.size() == kMaxExplicitNpcCandidates) break;
    }
    return result;
}

[[nodiscard]] LoadedActorScanResult scanLoadedActors(
    const LoadedActorProviderBindings& bindings);
[[nodiscard]] LoadedActorScanResult scanLoadedActors();

}  // namespace stui::native
