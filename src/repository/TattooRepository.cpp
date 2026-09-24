#include "repository/TattooRepository.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace stui::repository {
namespace {

std::string foldASCII(std::string_view value) {
    std::string folded(value);
    for (char& character : folded) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    return folded;
}

std::string buildFoldedSearch(const TattooDefinition& definition) {
    std::string search;
    for (const std::string_view field : {
            std::string_view(definition.name),
            std::string_view(definition.packName),
            std::string_view(definition.sourceFile),
            std::string_view(definition.section),
            std::string_view(definition.area),
            std::string_view(definition.texturePath)}) {
        if (!search.empty()) {
            search.push_back('\n');
        }
        search += foldASCII(field);
    }
    return search;
}

std::vector<std::string> buildFacetValues(
    std::vector<std::pair<std::string, std::string>> candidates) {
    std::ranges::sort(candidates);

    std::vector<std::string> values;
    values.reserve(candidates.size());
    std::string previousFolded;
    bool hasPrevious = false;
    for (auto& [folded, display] : candidates) {
        if (!hasPrevious || folded != previousFolded) {
            values.push_back(std::move(display));
            previousFolded = std::move(folded);
            hasPrevious = true;
        }
    }
    return values;
}

bool matchesAppliedIdentity(
    const TattooDefinition& definition,
    const std::optional<std::vector<TattooIdentity>>& appliedIdentities) {
    return !appliedIdentities || std::ranges::any_of(*appliedIdentities,
        [&definition](const TattooIdentity& identity) {
            return identity.section == definition.section && identity.name == definition.name;
        });
}

bool matchesFavoriteIdentity(
    const TattooDefinition& definition,
    const std::optional<std::vector<FavoriteIdentity>>& favoriteIdentities) {
    return !favoriteIdentities || std::ranges::any_of(*favoriteIdentities,
        [&definition](const FavoriteIdentity& identity) {
            return identity.domain == definition.domain &&
                identity.sourceId == definition.sourceId &&
                identity.section == definition.section && identity.name == definition.name;
        });
}

}  // namespace

TattooRepository::TattooRepository(std::vector<TattooDefinition> definitions) {
    m_entries.reserve(definitions.size());
    for (auto& definition : definitions) {
        m_entries.push_back(IndexedDefinition{
            .definition = std::move(definition),
        });
        auto& entry = m_entries.back();
        entry.foldedSearch = buildFoldedSearch(entry.definition);
        entry.foldedDomain = foldASCII(entry.definition.domain);
        entry.foldedSourceId = foldASCII(entry.definition.sourceId);
        entry.foldedSection = foldASCII(entry.definition.section);
        entry.foldedArea = foldASCII(entry.definition.area);
        entry.foldedPackName = foldASCII(entry.definition.packName);
        entry.foldedName = foldASCII(entry.definition.name);
        entry.foldedTexturePath = foldASCII(entry.definition.texturePath);
    }

    std::ranges::sort(m_entries, [](const IndexedDefinition& left, const IndexedDefinition& right) {
        return std::tie(
                   left.foldedPackName,
                   left.foldedDomain,
                   left.foldedSection,
                   left.foldedArea,
                   left.foldedName,
                   left.foldedTexturePath,
                   left.definition.sourceId,
                   left.definition.sourceIndex) <
               std::tie(
                   right.foldedPackName,
                   right.foldedDomain,
                   right.foldedSection,
                   right.foldedArea,
                   right.foldedName,
                   right.foldedTexturePath,
                   right.definition.sourceId,
                   right.definition.sourceIndex);
    });

    std::unordered_set<std::string> seenSources;
    std::vector<std::pair<std::string, std::string>> domains;
    std::vector<std::pair<std::string, std::string>> sections;
    std::vector<std::pair<std::string, std::string>> areas;
    domains.reserve(m_entries.size());
    sections.reserve(m_entries.size());
    areas.reserve(m_entries.size());
    for (const auto& entry : m_entries) {
        if (seenSources.insert(entry.foldedSourceId).second) {
            m_facets.sources.push_back(TattooSourceOption{
                .sourceId = entry.definition.sourceId,
                .packName = entry.definition.packName,
            });
        }
        domains.emplace_back(entry.foldedDomain, entry.definition.domain);
        sections.emplace_back(entry.foldedSection, entry.definition.section);
        areas.emplace_back(entry.foldedArea, entry.definition.area);
    }

    std::ranges::sort(m_facets.sources, [](const TattooSourceOption& left,
                                               const TattooSourceOption& right) {
        return std::tuple(foldASCII(left.packName), foldASCII(left.sourceId), left.sourceId) <
               std::tuple(foldASCII(right.packName), foldASCII(right.sourceId), right.sourceId);
    });
    m_facets.domains = buildFacetValues(std::move(domains));
    m_facets.sections = buildFacetValues(std::move(sections));
    m_facets.areas = buildFacetValues(std::move(areas));
}

TattooPage TattooRepository::query(const TattooFilter& filter) const {
    const std::string foldedSearch = foldASCII(filter.search);
    const std::string foldedDomain = foldASCII(filter.domain);
    const std::string foldedSourceId = foldASCII(filter.sourceId);
    const std::string foldedSection = foldASCII(filter.section);
    const std::string foldedArea = foldASCII(filter.area);

    std::vector<const IndexedDefinition*> matches;
    matches.reserve(m_entries.size());
    for (const auto& entry : m_entries) {
        if ((!foldedSearch.empty() &&
                entry.foldedSearch.find(foldedSearch) == std::string::npos) ||
            (!foldedSourceId.empty() && entry.foldedSourceId != foldedSourceId) ||
            (!foldedDomain.empty() && entry.foldedDomain != foldedDomain) ||
            (!foldedSection.empty() && entry.foldedSection != foldedSection) ||
            (!foldedArea.empty() && entry.foldedArea != foldedArea) ||
            !matchesAppliedIdentity(entry.definition, filter.appliedIdentities) ||
            !matchesFavoriteIdentity(entry.definition, filter.favoriteIdentities)) {
            continue;
        }
        matches.push_back(&entry);
    }

    const std::size_t pageSize =
        filter.pageSize == 0 ? kDefaultTattooPageSize : filter.pageSize;
    const std::size_t matchedEntries = matches.size();
    const std::size_t pageCount =
        matchedEntries / pageSize + (matchedEntries % pageSize != 0 ? 1 : 0);
    const std::size_t pageIndex =
        pageCount == 0 ? 0 : std::min(filter.pageIndex, pageCount - 1);
    const std::size_t begin = pageIndex * pageSize;
    const std::size_t end = begin + std::min(pageSize, matchedEntries - begin);

    TattooPage page{
        .totalEntries = m_entries.size(),
        .matchedEntries = matchedEntries,
        .pageIndex = pageIndex,
        .pageSize = pageSize,
        .pageCount = pageCount,
    };
    page.entries.reserve(end - begin);
    for (std::size_t index = begin; index < end; ++index) {
        page.entries.push_back(matches[index]->definition);
    }
    return page;
}

const TattooFacets& TattooRepository::facets() const noexcept {
    return m_facets;
}

TattooFacets TattooRepository::contextualFacets(const TattooFilter& filter) const {
    const std::string foldedArea = foldASCII(filter.area);
    const std::string foldedDomain = foldASCII(filter.domain);
    const std::string foldedSourceId = foldASCII(filter.sourceId);
    TattooFacets result;
    std::unordered_set<std::string> seenSources;
    std::vector<std::pair<std::string, std::string>> domains;
    std::vector<std::pair<std::string, std::string>> sections;
    std::vector<std::pair<std::string, std::string>> areas;

    for (const auto& entry : m_entries) {
        if (!matchesAppliedIdentity(entry.definition, filter.appliedIdentities) ||
            !matchesFavoriteIdentity(entry.definition, filter.favoriteIdentities)) {
            continue;
        }
        areas.emplace_back(entry.foldedArea, entry.definition.area);
        if (!foldedArea.empty() && entry.foldedArea != foldedArea) {
            continue;
        }
        domains.emplace_back(entry.foldedDomain, entry.definition.domain);
        if (!foldedDomain.empty() && entry.foldedDomain != foldedDomain) {
            continue;
        }
        if (seenSources.insert(entry.foldedSourceId).second) {
            result.sources.push_back(TattooSourceOption{
                .sourceId = entry.definition.sourceId,
                .packName = entry.definition.packName,
            });
        }
        if (foldedSourceId.empty() || entry.foldedSourceId == foldedSourceId) {
            sections.emplace_back(entry.foldedSection, entry.definition.section);
        }
    }

    std::ranges::sort(result.sources, [](const TattooSourceOption& left,
                                            const TattooSourceOption& right) {
        return std::tuple(foldASCII(left.packName), foldASCII(left.sourceId), left.sourceId) <
               std::tuple(foldASCII(right.packName), foldASCII(right.sourceId), right.sourceId);
    });
    result.domains = buildFacetValues(std::move(domains));
    result.sections = buildFacetValues(std::move(sections));
    result.areas = buildFacetValues(std::move(areas));
    return result;
}

}  // namespace stui::repository
