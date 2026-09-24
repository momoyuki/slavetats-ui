#pragma once

#include "repository/TattooSourceParser.h"

#include <string>

namespace stui::repository {

struct FavoriteIdentity {
    std::string domain;
    std::string sourceId;
    std::string section;
    std::string name;

    bool operator==(const FavoriteIdentity&) const = default;
};

[[nodiscard]] inline FavoriteIdentity favoriteIdentity(const TattooDefinition& tattoo) {
    return FavoriteIdentity{
        .domain = tattoo.domain,
        .sourceId = tattoo.sourceId,
        .section = tattoo.section,
        .name = tattoo.name,
    };
}

}  // namespace stui::repository
