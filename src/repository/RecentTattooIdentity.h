#pragma once

#include "core/TattooModels.h"
#include "repository/TattooSourceParser.h"

#include <string>

namespace stui::repository {

struct RecentTattooIdentity {
    std::string domain;
    std::string sourceId;
    std::string section;
    std::string name;
    core::TattooArea area{core::TattooArea::body};

    bool operator==(const RecentTattooIdentity&) const = default;
};

[[nodiscard]] inline RecentTattooIdentity recentTattooIdentity(
    const TattooDefinition& tattoo,
    const core::TattooArea area) {
    return RecentTattooIdentity{
        .domain = tattoo.domain,
        .sourceId = tattoo.sourceId,
        .section = tattoo.section,
        .name = tattoo.name,
        .area = area,
    };
}

}  // namespace stui::repository
