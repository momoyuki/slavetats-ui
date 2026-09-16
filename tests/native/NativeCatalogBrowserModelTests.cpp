#include "native/NativeCatalogBrowserModel.h"

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
    return failures == 0 ? 0 : 1;
}
