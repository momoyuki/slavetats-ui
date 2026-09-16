#pragma once

#include "repository/TattooSourceParser.h"

#include <cstddef>
#include <string>
#include <vector>

namespace stui::repository {

inline constexpr std::size_t kDefaultTattooPageSize = 24;

struct TattooFilter {
    std::string search;
    std::string domain;
    std::string sourceId;
    std::string section;
    std::string area;
    std::size_t pageIndex{};
    std::size_t pageSize{kDefaultTattooPageSize};
};

struct TattooPage {
    std::vector<TattooDefinition> entries;
    std::size_t totalEntries{};
    std::size_t matchedEntries{};
    std::size_t pageIndex{};
    std::size_t pageSize{kDefaultTattooPageSize};
    std::size_t pageCount{};
};

struct TattooSourceOption {
    std::string sourceId;
    std::string packName;

    bool operator==(const TattooSourceOption&) const = default;
};

struct TattooFacets {
    std::vector<std::string> domains;
    std::vector<TattooSourceOption> sources;
    std::vector<std::string> sections;
    std::vector<std::string> areas;
};

class TattooRepository {
public:
    explicit TattooRepository(std::vector<TattooDefinition> definitions);

    [[nodiscard]] TattooPage query(const TattooFilter& filter = {}) const;
    [[nodiscard]] const TattooFacets& facets() const noexcept;
    [[nodiscard]] TattooFacets contextualFacets(const TattooFilter& filter) const;

private:
    struct IndexedDefinition {
        TattooDefinition definition;
        std::string foldedSearch;
        std::string foldedDomain;
        std::string foldedSourceId;
        std::string foldedSection;
        std::string foldedArea;
        std::string foldedPackName;
        std::string foldedName;
        std::string foldedTexturePath;
    };

    std::vector<IndexedDefinition> m_entries;
    TattooFacets m_facets;
};

}  // namespace stui::repository
