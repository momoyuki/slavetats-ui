#include "native/NativeCatalogBrowserModel.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace stui::native {
namespace {

bool equalsFoldedASCII(std::string_view left, std::string_view right) {
    return left.size() == right.size() && std::ranges::equal(
        left,
        right,
        [](char leftCharacter, char rightCharacter) {
            const auto fold = [](char character) {
                return character >= 'A' && character <= 'Z'
                    ? static_cast<char>(character + ('a' - 'A'))
                    : character;
            };
            return fold(leftCharacter) == fold(rightCharacter);
        });
}

template <class Range, class Projection>
bool containsFolded(const Range& values, std::string_view value, Projection projection) {
    return std::ranges::any_of(values, [&](const auto& candidate) {
        return equalsFoldedASCII(std::invoke(projection, candidate), value);
    });
}

}  // namespace

NativeCatalogBrowserModel::NativeCatalogBrowserModel(CatalogSnapshotProvider provider) :
    m_provider(std::move(provider)) {}

void NativeCatalogBrowserModel::refresh() {
    repository::TattooCatalogSnapshot next = m_provider ? m_provider() : nullptr;
    if (next == m_snapshot) {
        return;
    }

    m_snapshot = std::move(next);
    m_contextualFacetsDirty = true;
    resetFilter();
    query();
}

void NativeCatalogBrowserModel::setSearch(std::string value) {
    if (m_filter.search == value) {
        return;
    }
    m_filter.search = std::move(value);
    m_filter.pageIndex = 0;
    query();
}

void NativeCatalogBrowserModel::setDomain(std::string value) {
    if (m_filter.domain == value) {
        return;
    }
    m_filter.domain = std::move(value);
    m_filter.pageIndex = 0;
    m_contextualFacetsDirty = true;
    reconcileContextualFilters();
    query();
}

void NativeCatalogBrowserModel::setSourceId(std::string value) {
    if (m_filter.sourceId == value) {
        return;
    }
    m_filter.sourceId = std::move(value);
    m_filter.pageIndex = 0;
    m_contextualFacetsDirty = true;
    reconcileContextualFilters();
    query();
}

void NativeCatalogBrowserModel::setSection(std::string value) {
    if (m_filter.section == value) {
        return;
    }
    m_filter.section = std::move(value);
    m_filter.pageIndex = 0;
    reconcileContextualFilters();
    query();
}

void NativeCatalogBrowserModel::setArea(std::string value) {
    if (m_filter.area == value) {
        return;
    }
    m_filter.area = std::move(value);
    m_filter.pageIndex = 0;
    m_contextualFacetsDirty = true;
    reconcileContextualFilters();
    query();
}

void NativeCatalogBrowserModel::setAppliedOnly(bool value) {
    if (m_appliedOnly == value) {
        return;
    }
    m_appliedOnly = value;
    m_filter.appliedIdentities = value
        ? std::optional(m_appliedTattooIdentities)
        : std::nullopt;
    m_filter.pageIndex = 0;
    m_contextualFacetsDirty = true;
    reconcileContextualFilters();
    query();
}

void NativeCatalogBrowserModel::setAppliedTattooIdentities(
    std::vector<repository::TattooIdentity> identities) {
    if (m_appliedTattooIdentities == identities) {
        return;
    }
    m_appliedTattooIdentities = std::move(identities);
    if (!m_appliedOnly) {
        return;
    }
    m_filter.appliedIdentities = m_appliedTattooIdentities;
    m_filter.pageIndex = 0;
    m_contextualFacetsDirty = true;
    reconcileContextualFilters();
    query();
}

void NativeCatalogBrowserModel::setFavoritesOnly(const bool value) {
    if (m_favoritesOnly == value) {
        return;
    }
    m_favoritesOnly = value;
    m_filter.favoriteIdentities = value
        ? std::optional(m_favoriteIdentities)
        : std::nullopt;
    m_filter.pageIndex = 0;
    m_contextualFacetsDirty = true;
    reconcileContextualFilters();
    query();
}

void NativeCatalogBrowserModel::setFavoriteIdentities(
    std::vector<repository::FavoriteIdentity> identities) {
    if (m_favoriteIdentities == identities) {
        return;
    }
    m_favoriteIdentities = std::move(identities);
    if (m_favoritesOnly) {
        m_filter.favoriteIdentities = m_favoriteIdentities;
        m_contextualFacetsDirty = true;
        reconcileContextualFilters();
        query();
    }
}

void NativeCatalogBrowserModel::setRecentlyUsedOnly(const bool value) {
    if (m_recentlyUsedOnly == value) {
        return;
    }
    m_recentlyUsedOnly = value;
    m_filter.recentIdentities = value
        ? std::optional(m_recentTattooIdentities)
        : std::nullopt;
    m_filter.pageIndex = 0;
    m_contextualFacetsDirty = true;
    reconcileContextualFilters();
    query();
}

void NativeCatalogBrowserModel::setRecentTattooIdentities(
    std::vector<repository::RecentTattooIdentity> identities) {
    if (m_recentTattooIdentities == identities) {
        return;
    }
    m_recentTattooIdentities = std::move(identities);
    if (m_recentlyUsedOnly) {
        m_filter.recentIdentities = m_recentTattooIdentities;
        m_contextualFacetsDirty = true;
        reconcileContextualFilters();
        query();
    }
}

void NativeCatalogBrowserModel::setGlowOnly(const bool value) {
    if (m_filter.glowOnly == value) {
        return;
    }
    m_filter.glowOnly = value;
    m_filter.pageIndex = 0;
    m_contextualFacetsDirty = true;
    reconcileContextualFilters();
    query();
}

void NativeCatalogBrowserModel::setBumpOnly(const bool value) {
    if (m_filter.bumpOnly == value) {
        return;
    }
    m_filter.bumpOnly = value;
    m_filter.pageIndex = 0;
    m_contextualFacetsDirty = true;
    reconcileContextualFilters();
    query();
}

void NativeCatalogBrowserModel::setGlossOnly(const bool value) {
    if (m_filter.glossOnly == value) {
        return;
    }
    m_filter.glossOnly = value;
    m_filter.pageIndex = 0;
    m_contextualFacetsDirty = true;
    reconcileContextualFilters();
    query();
}

void NativeCatalogBrowserModel::previousPage() {
    if (m_filter.pageIndex == 0) {
        return;
    }

    --m_filter.pageIndex;
    updatePage();
}

void NativeCatalogBrowserModel::nextPage() {
    if (m_page.pageCount == 0 || m_filter.pageIndex + 1 >= m_page.pageCount) {
        return;
    }

    ++m_filter.pageIndex;
    updatePage();
}

void NativeCatalogBrowserModel::setPageNumber(std::size_t oneBasedPage) {
    const auto requested = oneBasedPage == 0 ? 0 : oneBasedPage - 1;
    const auto clamped = m_page.pageCount == 0 ? 0
        : std::min(requested, m_page.pageCount - 1);
    if (m_filter.pageIndex == clamped) {
        return;
    }
    m_filter.pageIndex = clamped;
    updatePage();
}

const repository::TattooFilter& NativeCatalogBrowserModel::filter() const noexcept {
    return m_filter;
}

const repository::TattooPage& NativeCatalogBrowserModel::page() const noexcept {
    return m_page;
}

const repository::TattooPage& NativeCatalogBrowserModel::scrollPage() const noexcept {
    return m_scrollPage;
}

std::size_t NativeCatalogBrowserModel::scrollRevision() const noexcept {
    return m_scrollRevision;
}

const repository::TattooFacets& NativeCatalogBrowserModel::contextualFacets() const {
    if (m_contextualFacetsDirty) {
        m_contextualFacets = m_snapshot
            ? m_snapshot->repository.contextualFacets(m_filter)
            : repository::TattooFacets{};
        m_contextualFacetsDirty = false;
    }
    return m_contextualFacets;
}

repository::TattooCatalogSnapshot NativeCatalogBrowserModel::snapshot() const noexcept {
    return m_snapshot;
}

std::vector<std::string> NativeCatalogBrowserModel::diskCacheBuildPaths() {
    refresh();
    if (!m_snapshot) return {};
    repository::TattooFilter filter;
    filter.pageSize = std::numeric_limits<std::size_t>::max();
    const auto page = m_snapshot->repository.query(filter);
    std::vector<std::string> paths;
    paths.reserve(page.entries.size());
    for (const auto& entry : page.entries)
        paths.push_back(entry.texturePath);
    return paths;
}

bool NativeCatalogBrowserModel::appliedOnly() const noexcept {
    return m_appliedOnly;
}

bool NativeCatalogBrowserModel::favoritesOnly() const noexcept {
    return m_favoritesOnly;
}

bool NativeCatalogBrowserModel::recentlyUsedOnly() const noexcept {
    return m_recentlyUsedOnly;
}

bool NativeCatalogBrowserModel::glowOnly() const noexcept {
    return m_filter.glowOnly;
}

bool NativeCatalogBrowserModel::bumpOnly() const noexcept {
    return m_filter.bumpOnly;
}

bool NativeCatalogBrowserModel::glossOnly() const noexcept {
    return m_filter.glossOnly;
}

bool NativeCatalogBrowserModel::isFavorite(const repository::TattooDefinition& tattoo) const {
    const auto identity = repository::favoriteIdentity(tattoo);
    return std::ranges::any_of(m_favoriteIdentities, [&identity](const auto& favorite) {
        return favorite == identity;
    });
}

void NativeCatalogBrowserModel::resetFilter() {
    const bool glowOnly = m_filter.glowOnly;
    const bool bumpOnly = m_filter.bumpOnly;
    const bool glossOnly = m_filter.glossOnly;
    m_filter = repository::TattooFilter{
        .glowOnly = glowOnly,
        .bumpOnly = bumpOnly,
        .glossOnly = glossOnly,
        .pageSize = kPageSize,
    };
    if (m_appliedOnly) {
        m_filter.appliedIdentities = m_appliedTattooIdentities;
    }
    if (m_favoritesOnly) {
        m_filter.favoriteIdentities = m_favoriteIdentities;
    }
    if (m_recentlyUsedOnly) {
        m_filter.recentIdentities = m_recentTattooIdentities;
    }
}

void NativeCatalogBrowserModel::reconcileContextualFilters() {
    if (!m_snapshot) {
        m_filter.domain.clear();
        m_filter.sourceId.clear();
        m_filter.section.clear();
        return;
    }

    const auto& facets = contextualFacets();
    if (!m_filter.domain.empty() &&
        !containsFolded(facets.domains, m_filter.domain,
            [](const std::string& domain) -> std::string_view { return domain; })) {
        m_filter.domain.clear();
        m_contextualFacetsDirty = true;
        (void)contextualFacets();
    }
    if (!m_filter.sourceId.empty() &&
        !containsFolded(facets.sources, m_filter.sourceId,
            [](const repository::TattooSourceOption& source) -> std::string_view {
                return source.sourceId;
            })) {
        m_filter.sourceId.clear();
        m_contextualFacetsDirty = true;
        (void)contextualFacets();
    }
    if (!m_filter.section.empty() &&
        !containsFolded(facets.sections, m_filter.section,
            [](const std::string& section) -> std::string_view { return section; })) {
        m_filter.section.clear();
    }
}

void NativeCatalogBrowserModel::query() {
    ++m_scrollRevision;
    if (!m_snapshot) {
        m_page = repository::TattooPage{.pageSize = kPageSize};
        m_scrollPage = repository::TattooPage{};
        return;
    }

    auto scrollFilter = m_filter;
    scrollFilter.pageIndex = 0;
    scrollFilter.pageSize = std::numeric_limits<std::size_t>::max();
    m_scrollPage = m_snapshot->repository.query(scrollFilter);
    updatePage();
}

void NativeCatalogBrowserModel::updatePage() {
    const auto matched = m_scrollPage.matchedEntries;
    const auto count = matched / kPageSize + (matched % kPageSize != 0 ? 1 : 0);
    const auto index = count == 0 ? 0 : std::min(m_filter.pageIndex, count - 1);
    const auto begin = index * kPageSize;
    const auto end = begin + std::min(kPageSize, matched - begin);
    m_page = repository::TattooPage{
        .entries = {m_scrollPage.entries.begin() + begin, m_scrollPage.entries.begin() + end},
        .totalEntries = m_scrollPage.totalEntries,
        .matchedEntries = matched,
        .pageIndex = index,
        .pageSize = kPageSize,
        .pageCount = count,
    };
    m_filter.pageIndex = index;
    m_filter.pageSize = kPageSize;
}

}  // namespace stui::native
