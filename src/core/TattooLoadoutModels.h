#pragma once

#include "core/TattooModels.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace stui::core {

inline constexpr std::size_t kTattooLoadoutLimit = 20;

struct TattooLoadoutEntry {
    TattooArea area{TattooArea::body};
    std::int32_t slot{-1};
    std::string domain{"default"};
    std::string section;
    std::string name;
    std::int32_t color{0x000000};
    float alpha{1.0F};
    std::int32_t glow{};
    float emissiveMult{1.0F};
    float glossiness{};
    float specularStrength{};

    bool operator==(const TattooLoadoutEntry&) const = default;
};

struct TattooLoadout {
    std::string name;
    std::vector<TattooLoadoutEntry> entries;

    bool operator==(const TattooLoadout&) const = default;
};

using TattooLoadoutList = std::vector<TattooLoadout>;

enum class TattooLoadoutValidationError {
    missingName,
    invalidArea,
    invalidSlot,
    missingIdentity,
    invalidColor,
    invalidAppearance,
    duplicatePosition,
};

inline std::expected<void, TattooLoadoutValidationError> validateTattooLoadoutEntry(
    const TattooLoadoutEntry& entry) {
    switch (entry.area) {
    case TattooArea::body:
    case TattooArea::face:
    case TattooArea::hands:
    case TattooArea::feet:
        break;
    default:
        return std::unexpected(TattooLoadoutValidationError::invalidArea);
    }
    if (entry.slot < 0) {
        return std::unexpected(TattooLoadoutValidationError::invalidSlot);
    }
    if (entry.domain.empty() || entry.section.empty() || entry.name.empty()) {
        return std::unexpected(TattooLoadoutValidationError::missingIdentity);
    }
    if (entry.color < 0 || entry.color > 0xFFFFFF ||
        entry.glow < 0 || entry.glow > 0xFFFFFF) {
        return std::unexpected(TattooLoadoutValidationError::invalidColor);
    }
    if (!std::isfinite(entry.alpha) || entry.alpha < 0.0F || entry.alpha > 1.0F ||
        !std::isfinite(entry.emissiveMult) || entry.emissiveMult < 0.0F ||
        entry.emissiveMult > 10.0F || !std::isfinite(entry.glossiness) ||
        entry.glossiness < 0.0F || entry.glossiness > 1000.0F ||
        !std::isfinite(entry.specularStrength) || entry.specularStrength < 0.0F ||
        entry.specularStrength > 100.0F) {
        return std::unexpected(TattooLoadoutValidationError::invalidAppearance);
    }
    return {};
}

inline std::expected<void, TattooLoadoutValidationError> validateTattooLoadout(
    const TattooLoadout& loadout) {
    if (loadout.name.empty()) {
        return std::unexpected(TattooLoadoutValidationError::missingName);
    }
    for (std::size_t index = 0; index < loadout.entries.size(); ++index) {
        const auto& entry = loadout.entries[index];
        if (auto validation = validateTattooLoadoutEntry(entry); !validation) {
            return validation;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (loadout.entries[previous].area == entry.area &&
                loadout.entries[previous].slot == entry.slot) {
                return std::unexpected(TattooLoadoutValidationError::duplicatePosition);
            }
        }
    }
    return {};
}

}  // namespace stui::core
