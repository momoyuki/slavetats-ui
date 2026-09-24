#pragma once

#include "repository/TattooCatalogStore.h"
#include "repository/TattooRepository.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace stui::native {

using CatalogSnapshotProvider = std::function<repository::TattooCatalogSnapshot()>;

class NativeCatalogBrowserModel {
public:
    static constexpr std::size_t kPageSize = 6;

    explicit NativeCatalogBrowserModel(CatalogSnapshotProvider provider);

    void refresh();
    void setSearch(std::string value);
    void setDomain(std::string value);
    void setSourceId(std::string value);
    void setSection(std::string value);
    void setArea(std::string value);
    void setAppliedOnly(bool value);
    void setAppliedTattooIdentities(std::vector<repository::TattooIdentity> identities);
    void previousPage();
    void nextPage();
    void setPageNumber(std::size_t oneBasedPage);

    [[nodiscard]] const repository::TattooFilter& filter() const noexcept;
    [[nodiscard]] const repository::TattooPage& page() const noexcept;
    [[nodiscard]] repository::TattooFacets contextualFacets() const;
    [[nodiscard]] repository::TattooCatalogSnapshot snapshot() const noexcept;
    [[nodiscard]] bool appliedOnly() const noexcept;

private:
    void resetFilter();
    void reconcileContextualFilters();
    void query();

    CatalogSnapshotProvider m_provider;
    repository::TattooCatalogSnapshot m_snapshot;
    repository::TattooFilter m_filter{.pageSize = kPageSize};
    repository::TattooPage m_page{.pageSize = kPageSize};
    std::vector<repository::TattooIdentity> m_appliedTattooIdentities;
    bool m_appliedOnly{};
};

}  // namespace stui::native
