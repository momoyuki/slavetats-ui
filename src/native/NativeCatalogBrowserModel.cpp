#include "native/NativeCatalogBrowserModel.h"

#include <algorithm>
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
    resetFilter();
    query();
}

void NativeCatalogBrowserModel::setSearch(std::string value) {
    m_filter.search = std::move(value);
    m_filter.pageIndex = 0;
    query();
}

void NativeCatalogBrowserModel::setDomain(std::string value) {
    m_filter.domain = std::move(value);
    m_filter.pageIndex = 0;
    reconcileContextualFilters();
    query();
}

void NativeCatalogBrowserModel::setSourceId(std::string value) {
    m_filter.sourceId = std::move(value);
    m_filter.pageIndex = 0;
    reconcileContextualFilters();
    query();
}

void NativeCatalogBrowserModel::setSection(std::string value) {
    m_filter.section = std::move(value);
    m_filter.pageIndex = 0;
    reconcileContextualFilters();
    query();
}

void NativeCatalogBrowserModel::setArea(std::string value) {
    m_filter.area = std::move(value);
    m_filter.pageIndex = 0;
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
        reconcileContextualFilters();
        query();
    }
}

void NativeCatalogBrowserModel::previousPage() {
    if (m_filter.pageIndex == 0) {
        return;
    }

    --m_filter.pageIndex;
    query();
}

void NativeCatalogBrowserModel::nextPage() {
    if (m_page.pageCount == 0 || m_filter.pageIndex + 1 >= m_page.pageCount) {
        return;
    }

    ++m_filter.pageIndex;
    query();
}

void NativeCatalogBrowserModel::setPageNumber(std::size_t oneBasedPage) {
    m_filter.pageIndex = oneBasedPage == 0 ? 0 : oneBasedPage - 1;
    query();
}

const repository::TattooFilter& NativeCatalogBrowserModel::filter() const noexcept {
    return m_filter;
}

const repository::TattooPage& NativeCatalogBrowserModel::page() const noexcept {
    return m_page;
}

repository::TattooFacets NativeCatalogBrowserModel::contextualFacets() const {
    return m_snapshot
        ? m_snapshot->repository.contextualFacets(m_filter)
        : repository::TattooFacets{};
}

repository::TattooCatalogSnapshot NativeCatalogBrowserModel::snapshot() const noexcept {
    return m_snapshot;
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

bool NativeCatalogBrowserModel::isFavorite(const repository::TattooDefinition& tattoo) const {
    const auto identity = repository::favoriteIdentity(tattoo);
    return std::ranges::any_of(m_favoriteIdentities, [&identity](const auto& favorite) {
        return favorite == identity;
    });
}

void NativeCatalogBrowserModel::resetFilter() {
    m_filter = repository::TattooFilter{.pageSize = kPageSize};
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

    auto facets = m_snapshot->repository.contextualFacets(m_filter);
    if (!m_filter.domain.empty() &&
        !containsFolded(facets.domains, m_filter.domain,
            [](const std::string& domain) -> std::string_view { return domain; })) {
        m_filter.domain.clear();
        facets = m_snapshot->repository.contextualFacets(m_filter);
    }
    if (!m_filter.sourceId.empty() &&
        !containsFolded(facets.sources, m_filter.sourceId,
            [](const repository::TattooSourceOption& source) -> std::string_view {
                return source.sourceId;
            })) {
        m_filter.sourceId.clear();
        facets = m_snapshot->repository.contextualFacets(m_filter);
    }
    if (!m_filter.section.empty() &&
        !containsFolded(facets.sections, m_filter.section,
            [](const std::string& section) -> std::string_view { return section; })) {
        m_filter.section.clear();
    }
}

void NativeCatalogBrowserModel::query() {
    if (!m_snapshot) {
        m_page = repository::TattooPage{.pageSize = kPageSize};
        return;
    }

    m_page = m_snapshot->repository.query(m_filter);
    m_filter.pageIndex = m_page.pageIndex;
    m_filter.pageSize = kPageSize;
}

}  // namespace stui::native
