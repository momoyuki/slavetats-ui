#pragma once

#include "native/ActorTarget.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <expected>
#include <functional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace stui::native {

inline constexpr float kNearbyActorScanRadius = 2048.0F;
inline constexpr std::size_t kMaxNearbyActors = 10;

struct NearbyActorCandidate {
    ActorTarget target;
    float distance{};

    bool operator==(const NearbyActorCandidate&) const = default;
};

using NearbyActorList = std::vector<NearbyActorCandidate>;
using NearbyActorScanResult = std::expected<NearbyActorList, core::ServiceError>;

struct NearbyActorProviderBindings {
    std::function<NearbyActorList()> enumerate;
};

[[nodiscard]] inline bool isValidNearbyActorCandidate(
    const NearbyActorCandidate& candidate) noexcept {
    return candidate.target.kind == ActorTargetKind::nearby &&
        candidate.target.formId != 0 && std::isfinite(candidate.distance) &&
        candidate.distance >= 0.0F && candidate.distance <= kNearbyActorScanRadius;
}

[[nodiscard]] inline NearbyActorList normalizeNearbyActorCandidates(
    NearbyActorList candidates) {
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [](const auto& candidate) {
        return !isValidNearbyActorCandidate(candidate);
    }), candidates.end());
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        if (left.distance != right.distance) return left.distance < right.distance;
        return left.target.formId < right.target.formId;
    });

    NearbyActorList result;
    std::unordered_set<std::uint32_t> seen;
    result.reserve(std::min(candidates.size(), kMaxNearbyActors));
    for (auto& candidate : candidates) {
        if (!seen.insert(candidate.target.formId).second) continue;
        result.push_back(std::move(candidate));
        if (result.size() == kMaxNearbyActors) break;
    }
    return result;
}
[[nodiscard]] NearbyActorScanResult scanNearbyActors(
    const NearbyActorProviderBindings& bindings);
[[nodiscard]] NearbyActorScanResult scanNearbyActors();

}  // namespace stui::native
