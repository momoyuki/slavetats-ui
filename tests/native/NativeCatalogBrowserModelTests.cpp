#include "native/NativeCatalogBrowserModel.h"
#include "repository/FavoriteIdentity.h"

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using stui::native::NativeCatalogBrowserModel;
using stui::repository::TattooCatalog;
using stui::repository::TattooCatalogSnapshot;
using stui::repository::TattooDefinition;

TattooDefinition tattoo(
    std::string sourceId,
    std::string section,
    std::string area,
    std::string name,
    std::size_t sourceIndex,
    std::string domain = "default") {
    return TattooDefinition{
        .sourceId = std::move(sourceId),
        .sourceFile = "fixture.json",
        .packName = "Fixture Pack",
        .sourceIndex = sourceIndex,
        .domain = std::move(domain),
        .name = std::move(name),
        .section = std::move(section),
        .texturePath = "fixture.dds",
        .area = std::move(area),
    };
}

TattooCatalogSnapshot snapshot(std::vector<TattooDefinition> definitions) {
    return std::make_shared<const TattooCatalog>(TattooCatalog{
        .repository = stui::repository::TattooRepository(std::move(definitions)),
        .sourceCount = 1,
    });
}

TattooCatalogSnapshot catalogWithEntries(std::size_t count) {
    std::vector<TattooDefinition> definitions;
    definitions.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        definitions.push_back(tattoo(
            "source-a.json",
            "Marks",
            "Body",
            "Entry " + std::to_string(index),
            index));
    }
    return snapshot(std::move(definitions));
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void displaysSixEntriesPerPage() {
    TattooCatalogSnapshot current = catalogWithEntries(13);
    NativeCatalogBrowserModel model([&current] { return current; });
    model.refresh();

    expect(model.page().entries.size() == 6, "expected fixed six-entry first page");
    expect(model.page().pageSize == 6 && model.page().pageCount == 3,
        "expected fixed six-entry page geometry");
    model.nextPage();
    expect(model.page().entries.size() == 6 && model.page().pageIndex == 1,
        "expected fixed six-entry second page");
    model.nextPage();
    expect(model.page().entries.size() == 1 && model.page().pageIndex == 2,
        "expected remaining entry on final page");
}

void combinesFiltersAndResetsToFirstPage() {
    TattooCatalogSnapshot current = snapshot({
        tattoo("source-a.json", "Marks", "Body", "Alpha", 0),
        tattoo("source-a.json", "Marks", "Face", "Bravo", 1),
        tattoo("source-b.json", "Marks", "Body", "Charlie", 2),
        tattoo("source-a.json", "Runes", "Body", "Delta", 3),
        tattoo("source-a.json", "Marks", "Body", "Echo", 4),
        tattoo("source-a.json", "Marks", "Body", "Foxtrot", 5),
        tattoo("source-a.json", "Marks", "Body", "Golf", 6),
    });
    NativeCatalogBrowserModel model([&current] { return current; });
    model.refresh();
    model.nextPage();

    model.setSourceId("source-a.json");
    model.setSection("Marks");
    model.setArea("Body");
    model.setSearch("echo");

    expect(model.filter().pageIndex == 0, "expected every filter change to reset page zero");
    expect(model.page().matchedEntries == 1 && model.page().entries.front().name == "Echo",
        "expected combined filters to retain only Echo");
}

void clampsPreviousNextAndOneBasedPageInput() {
    TattooCatalogSnapshot current = catalogWithEntries(13);
    NativeCatalogBrowserModel model([&current] { return current; });
    model.refresh();

    model.previousPage();
    expect(model.page().pageIndex == 0, "expected previous to stop at first page");
    model.setPageNumber(99);
    expect(model.page().pageIndex == 2, "expected one-based input clamped to final page");
    model.nextPage();
    expect(model.page().pageIndex == 2, "expected next to stop at final page");
    model.setPageNumber(0);
    expect(model.page().pageIndex == 0, "expected zero one-based input clamped to first page");
}

void handlesNullAndNoMatchSnapshots() {
    TattooCatalogSnapshot current;
    NativeCatalogBrowserModel model([&current] { return current; });
    model.refresh();

    expect(model.page().entries.empty() && model.page().totalEntries == 0,
        "expected null snapshot to yield empty entries");
    expect(model.page().pageIndex == 0 && model.page().pageSize == 6 && model.page().pageCount == 0,
        "expected null snapshot to yield normalized empty page");

    current = catalogWithEntries(1);
    model.refresh();
    model.setSearch("not-present");
    expect(model.page().entries.empty() && model.page().matchedEntries == 0,
        "expected no-match filter to yield empty page");
    expect(model.page().pageIndex == 0 && model.page().pageSize == 6,
        "expected no-match page to retain native page size");
}

void preservesStateForSameSnapshotAndResetsForReplacement() {
    TattooCatalogSnapshot current = catalogWithEntries(13);
    NativeCatalogBrowserModel model([&current] { return current; });
    model.refresh();
    model.setSearch("Entry");
    model.setPageNumber(2);

    model.refresh();
    expect(model.filter().search == "Entry" && model.page().pageIndex == 1,
        "expected same snapshot identity to retain browser state");

    current = snapshot({tattoo("replacement.json", "Runes", "Face", "Replacement", 0)});
    model.refresh();
    expect(model.filter().search.empty() && model.filter().sourceId.empty() &&
            model.filter().section.empty() && model.filter().area.empty() &&
            model.filter().pageIndex == 0,
        "expected replacement snapshot to clear filters and reset page");
    expect(model.page().entries.size() == 1 && model.page().entries.front().name == "Replacement",
        "expected replacement snapshot content");
}

void clearsFiltersThatAreInvalidInTheNewContext() {
    TattooCatalogSnapshot current = snapshot({
        tattoo("body-a.json", "Body Marks", "Body", "Body A", 0),
        tattoo("body-b.json", "Body Runes", "Body", "Body B", 1),
        tattoo("face.json", "Face Marks", "Face", "Face", 2),
    });
    NativeCatalogBrowserModel model([&current] { return current; });
    model.refresh();
    model.setArea("Face");
    model.setSourceId("face.json");
    model.setSection("Face Marks");

    model.setArea("Body");
    expect(model.filter().sourceId.empty() && model.filter().section.empty(),
        "expected Area change to clear Source and Section unavailable in Body");

    model.setSourceId("body-a.json");
    model.setSection("Body Marks");
    model.setSourceId("body-b.json");
    expect(model.filter().section.empty(),
        "expected Source change to clear a Section unavailable in that Source");
    expect(model.page().matchedEntries == 1 && model.page().entries.front().name == "Body B",
        "expected query to use the reconciled Body Source context");
}

void domainSelectionReconcilesSourceAndSection() {
    TattooCatalogSnapshot current = snapshot({
        tattoo("default-a.json", "Default Marks", "Body", "Default A", 0),
        tattoo("default-b.json", "Default Runes", "Body", "Default B", 1),
        tattoo("custom.json", "Custom Marks", "Body", "Custom", 2, "custom"),
    });
    NativeCatalogBrowserModel model([&current] { return current; });
    model.refresh();
    model.setDomain("custom");
    model.setSourceId("custom.json");
    model.setSection("Custom Marks");

    model.setDomain("default");
    expect(model.filter().domain == "default" && model.filter().pageIndex == 0,
        "expected Domain change to reset pagination");
    expect(model.filter().sourceId.empty() && model.filter().section.empty(),
        "expected incompatible Source and Section cleared after Domain change");
    expect(model.page().matchedEntries == 2,
        "expected default Domain entries after reconciliation");

    model.setDomain("");
    expect(model.filter().domain.empty() && model.page().matchedEntries == 3,
        "expected empty Domain to restore All Domains");

    model.setDomain("missing");
    expect(model.filter().domain.empty() && model.page().matchedEntries == 3,
        "expected unavailable Domain to reconcile to All Domains");
}

void filtersExactFavoriteIdentitiesBeforePagination() {
    std::vector<TattooDefinition> definitions;
    for (std::size_t index = 0; index < 13; ++index) {
        definitions.push_back(tattoo(
            index % 2 == 0 ? "favorites.json" : "other.json",
            "Marks",
            "Body",
            "Entry " + std::to_string(index),
            index,
            index == 12 ? "custom" : "default"));
    }
    TattooCatalogSnapshot current = snapshot(std::move(definitions));
    NativeCatalogBrowserModel model([&current] { return current; });
    model.refresh();

    std::vector<stui::repository::FavoriteIdentity> favorites;
    for (std::size_t index = 0; index < 13; index += 2) {
        favorites.push_back(stui::repository::favoriteIdentity(tattoo(
            "favorites.json", "Marks", "Body", "Entry " + std::to_string(index), index,
            index == 12 ? "custom" : "default")));
    }
    model.setFavoriteIdentities(favorites);
    model.setFavoritesOnly(true);

    expect(model.favoritesOnly() && model.page().matchedEntries == favorites.size(),
        "expected Favorites-only to filter the exact stored identities");
    expect(std::ranges::all_of(model.page().entries, [&model](const TattooDefinition& entry) {
        return model.isFavorite(entry);
    }), "expected every visible Favorites-only entry to have a stored favorite identity");
}

void favoriteUpdateClampsPageWithoutResettingOtherFilters() {
    TattooCatalogSnapshot current = catalogWithEntries(13);
    NativeCatalogBrowserModel model([&current] { return current; });
    model.refresh();
    std::vector<stui::repository::FavoriteIdentity> favorites;
    for (std::size_t index = 0; index < 7; ++index) {
        favorites.push_back(stui::repository::favoriteIdentity(tattoo(
            "source-a.json", "Marks", "Body", "Entry " + std::to_string(index), index)));
    }
    model.setFavoriteIdentities(favorites);
    model.setFavoritesOnly(true);
    model.setPageNumber(2);
    model.setSearch("Entry");
    model.setPageNumber(2);

    favorites.pop_back();
    model.setFavoriteIdentities(favorites);

    expect(model.filter().search == "Entry" && model.page().pageIndex == 0 &&
            model.page().matchedEntries == 6,
        "expected membership update to clamp the page without clearing other filters");
}

void filtersRecentlyUsedInNewestFirstOrder() {
    TattooCatalogSnapshot current = snapshot({
        tattoo("source-a.json", "Marks", "Body", "Alpha", 0),
        tattoo("source-a.json", "Marks", "Body", "Beta", 1),
        tattoo("source-a.json", "Marks", "Face", "Face Mark", 2),
    });
    NativeCatalogBrowserModel model([&current] { return current; });
    model.refresh();
    model.setArea("Body");
    model.setRecentTattooIdentities({
        stui::repository::recentTattooIdentity(
            tattoo("source-a.json", "Marks", "Body", "Beta", 1),
            stui::core::TattooArea::body),
        stui::repository::recentTattooIdentity(
            tattoo("source-a.json", "Marks", "Body", "Alpha", 0),
            stui::core::TattooArea::body),
    });
    model.setRecentlyUsedOnly(true);

    expect(model.recentlyUsedOnly() && model.page().matchedEntries == 2 &&
            model.page().entries[0].name == "Beta" &&
            model.page().entries[1].name == "Alpha",
        "expected Recently Used filter to preserve newest-first history order");
}

void recentUpdateClampsPageWithoutClearingOtherFilters() {
    TattooCatalogSnapshot current = catalogWithEntries(13);
    NativeCatalogBrowserModel model([&current] { return current; });
    model.refresh();
    std::vector<stui::repository::RecentTattooIdentity> recent;
    for (std::size_t index = 0; index < 7; ++index) {
        recent.push_back(stui::repository::recentTattooIdentity(tattoo(
            "source-a.json", "Marks", "Body", "Entry " + std::to_string(index), index),
            stui::core::TattooArea::body));
    }
    model.setRecentTattooIdentities(recent);
    model.setRecentlyUsedOnly(true);
    model.setSearch("Entry");
    model.setPageNumber(2);

    recent.pop_back();
    model.setRecentTattooIdentities(recent);

    expect(model.filter().search == "Entry" && model.page().pageIndex == 0 &&
            model.page().matchedEntries == 6,
        "expected recent update to clamp page without clearing filters");
}

void materialFiltersComposeAndPersistAcrossCatalogRefresh() {
    auto glowOnly = tattoo("source-a.json", "Marks", "Body", "Glow", 0);
    glowOnly.glow = 1;
    auto bumpOnly = tattoo("source-a.json", "Marks", "Body", "Bump", 1);
    bumpOnly.bump = "marks/bump_n.dds";
    auto glossOnly = tattoo("source-a.json", "Marks", "Body", "Gloss", 2);
    glossOnly.glossiness = 1.0F;
    auto allThree = tattoo("source-a.json", "Marks", "Body", "All Three", 3);
    allThree.glowTexture = "marks/all_g.dds";
    allThree.bump = "marks/all_n.dds";
    allThree.specularStrength = 1.0F;
    const auto legacy = tattoo("source-a.json", "Marks", "Body", "Legacy", 4);
    TattooCatalogSnapshot current = snapshot({glowOnly, bumpOnly, glossOnly, allThree, legacy});
    NativeCatalogBrowserModel model([&current] { return current; });
    model.refresh();

    model.setGlowOnly(true);
    model.setBumpOnly(true);
    model.setGlossOnly(true);
    expect(model.glowOnly() && model.bumpOnly() && model.glossOnly(),
        "expected independent material toggles enabled");
    expect(model.filter().pageIndex == 0 && model.page().matchedEntries == 1 &&
            model.page().entries.front().name == "All Three",
        "expected material toggles to reset pagination and use AND semantics");

    current = snapshot({allThree, legacy});
    model.refresh();
    expect(model.glowOnly() && model.bumpOnly() && model.glossOnly() &&
            model.page().matchedEntries == 1 &&
            model.page().entries.front().name == "All Three",
        "expected replacement snapshot to preserve transient material toggles");

    model.setGlossOnly(false);
    model.setBumpOnly(false);
    model.setGlowOnly(false);
    expect(!model.glowOnly() && !model.bumpOnly() && !model.glossOnly() &&
            model.page().matchedEntries == 2,
        "expected disabling material filters to restore legacy entries");
}

template <class Test>
int run(std::string_view name, Test&& test) {
    try {
        std::forward<Test>(test)();
        std::cout << "PASS " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        return 1;
    }
}

}  // namespace

int main() {
    int failures = 0;
    failures += run("displays six entries per page", displaysSixEntriesPerPage);
    failures += run("combines filters and resets to first page", combinesFiltersAndResetsToFirstPage);
    failures += run("clamps previous, next, and one-based input", clampsPreviousNextAndOneBasedPageInput);
    failures += run("handles null and no-match snapshots", handlesNullAndNoMatchSnapshots);
    failures += run("preserves state for same snapshot and resets for replacement",
        preservesStateForSameSnapshotAndResetsForReplacement);
    failures += run(
        "clears filters invalid in the new context",
        clearsFiltersThatAreInvalidInTheNewContext);
    failures += run(
        "domain selection reconciles Source and Section",
        domainSelectionReconcilesSourceAndSection);
    failures += run("filters exact favorite identities before pagination",
        filtersExactFavoriteIdentitiesBeforePagination);
    failures += run("favorite update clamps page without resetting filters",
        favoriteUpdateClampsPageWithoutResettingOtherFilters);
    failures += run("filters Recently Used in newest-first order",
        filtersRecentlyUsedInNewestFirstOrder);
    failures += run("recent update clamps page without clearing filters",
        recentUpdateClampsPageWithoutClearingOtherFilters);
    failures += run("material filters compose and persist across refresh",
        materialFiltersComposeAndPersistAcrossCatalogRefresh);
    return failures == 0 ? 0 : 1;
}
