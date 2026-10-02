#pragma once

#include "repository/TattooSourceParser.h"

#include <cmath>

namespace stui::repository {

inline constexpr float kTattooMaterialFloatTolerance = 0.0001F;

struct TattooMaterialClassification {
    bool glow{};
    bool bump{};
    bool gloss{};

    bool operator==(const TattooMaterialClassification&) const = default;
};

[[nodiscard]] inline TattooMaterialClassification classifyTattooMaterial(
    const TattooDefinition& tattoo) noexcept {
    const bool hasGlowTexture = tattoo.glowTexture && !tattoo.glowTexture->empty();
    const bool hasBumpTexture = tattoo.bump && !tattoo.bump->empty();
    const bool hasNonDefaultEmission = tattoo.emissiveMult &&
        std::fabs(*tattoo.emissiveMult - 1.0F) > kTattooMaterialFloatTolerance;

    return {
        .glow = (tattoo.glow && *tattoo.glow != 0) || hasGlowTexture ||
            hasNonDefaultEmission,
        .bump = hasBumpTexture,
        .gloss = (tattoo.glossiness && *tattoo.glossiness > 0.0F) ||
            (tattoo.specularStrength && *tattoo.specularStrength > 0.0F),
    };
}

}  // namespace stui::repository
