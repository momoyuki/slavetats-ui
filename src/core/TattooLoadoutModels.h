#pragma once

#include "core/TattooModels.h"

#include <cstddef>
#include <cstdint>
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
    std::int32_t color{0xFFFFFF};
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

}  // namespace stui::core
