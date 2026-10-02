#pragma once

#include "repository/TattooSourceScanner.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace stui::repository {

struct TattooDefinition {
    std::string sourceId;
    std::string sourceFile;
    std::string packName;
    std::size_t sourceIndex{};
    std::string domain{"default"};
    std::string name;
    std::string section;
    std::string texturePath;
    std::string area;
    std::optional<std::int32_t> glow;
    std::optional<bool> inBsa;
    std::optional<std::string> credit;
    std::optional<std::string> glowTexture;
    std::optional<float> emissiveMult;
    std::optional<float> glossiness;
    std::optional<float> specularStrength;
    std::optional<std::string> bump;
};

struct TattooParseIssue {
    std::optional<std::size_t> entryIndex;
    std::string message;
};

struct TattooSourceParseReport {
    std::vector<TattooDefinition> definitions;
    std::vector<TattooParseIssue> issues;
};

[[nodiscard]] TattooSourceParseReport parseTattooSource(const TattooSourceFile& source);

}  // namespace stui::repository
