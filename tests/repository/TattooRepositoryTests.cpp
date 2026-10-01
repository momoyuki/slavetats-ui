#include "repository/TattooRepository.h"
#include "repository/TattooMaterialClassification.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using stui::repository::TattooDefinition;
using stui::repository::RecentTattooIdentity;
using stui::repository::TattooFilter;
using stui::repository::TattooIdentity;
using stui::repository::TattooMaterialClassification;
using stui::repository::TattooRepository;
using stui::repository::classifyTattooMaterial;
using stui::repository::kTattooMaterialFloatTolerance;

TattooDefinition definition(
    std::string sourceId,
    std::string pack,
    std::string section,
    std::string name,
    std::string texture,
    std::string area,
    std::size_t sourceIndex = 0) {
    return TattooDefinition{
        .sourceId = std::move(sourceId),
        .sourceFile = pack + ".json",
        .packName = std::move(pack),
        .sourceIndex = sourceIndex,
        .name = std::move(name),
        .section = std::move(section),
        .texturePath = std::move(texture),
        .area = std::move(area),
    };
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void ordersDefinitionsDeterministicallyAndPreservesMetadata() {
    auto alpha = definition("a.json", "pack a", "Marks", "Alpha", "a.dds", "Body", 4);
    alpha.glow = 123;
    alpha.inBsa = true;
    alpha.credit = "Artist";
    alpha.glowTexture = "Pack\\Alpha\\alpha_g.dds";
    alpha.emissiveMult = 3.75F;
    alpha.glossiness = 2.5F;
    alpha.specularStrength = 1.25F;
    alpha.bump = "Pack\\Alpha\\alpha_n.dds";
    std::vector<TattooDefinition> definitions{
        definition("z.json", "Pack B", "Marks", "Zulu", "z.dds", "Body"),
        definition("a.json", "pack a", "Marks", "Beta", "b.dds", "Body", 5),
        std::move(alpha),
    };
    TattooRepository repository(definitions);
    std::ranges::reverse(definitions);
    TattooRepository reversedRepository(std::move(definitions));

    const auto page = repository.query();
    const auto reversedPage = reversedRepository.query();

    expect(page.entries.size() == 3, "expected all definitions");
    expect(page.entries[0].name == "Alpha", "expected folded pack/name ordering");
    expect(page.entries[1].name == "Beta", "expected stable order within pack");
    expect(page.entries[2].name == "Zulu", "expected later pack last");
    expect(page.entries[0].sourceId == "a.json" && page.entries[0].sourceIndex == 4,
        "expected source provenance preserved");
    expect(page.entries[0].glow == 123 && page.entries[0].inBsa == true &&
            page.entries[0].credit == "Artist",
        "expected optional metadata preserved");
    expect(page.entries[0].glowTexture == "Pack\\Alpha\\alpha_g.dds" &&
            page.entries[0].emissiveMult == 3.75F &&
            page.entries[0].glossiness == 2.5F &&
            page.entries[0].specularStrength == 1.25F &&
            page.entries[0].bump == "Pack\\Alpha\\alpha_n.dds",
        "expected advanced optional metadata preserved exactly");
    expect(page.totalEntries == 3 && page.matchedEntries == 3,
        "expected total and match counts");
    expect(page.pageIndex == 0 && page.pageSize == 24 && page.pageCount == 1,
        "expected default paging");
    expect(reversedPage.entries.size() == page.entries.size(),
        "expected input permutation to preserve result count");
    for (std::size_t index = 0; index < page.entries.size(); ++index) {
        expect(reversedPage.entries[index].sourceId == page.entries[index].sourceId &&
                reversedPage.entries[index].sourceIndex == page.entries[index].sourceIndex,
            "expected input permutation to preserve deterministic order");
    }
}

void paginatesAndClampsOutOfRangePage() {
    std::vector<TattooDefinition> definitions;
    for (std::size_t index = 0; index < 5; ++index) {
        definitions.push_back(definition(
            "pack.json",
            "Pack",
            "Marks",
            "Mark " + std::to_string(index),
            std::to_string(index) + ".dds",
            "Body",
            index));
    }
    TattooRepository repository(std::move(definitions));

    const auto page = repository.query(TattooFilter{.pageIndex = 9, .pageSize = 2});

    expect(page.pageIndex == 2 && page.pageCount == 3,
        "expected page index clamped to last page");
    expect(page.entries.size() == 1, "expected one entry on final page");
    expect(page.entries.front().name == "Mark 4", "expected deterministic final entry");
}

void zeroPageSizeUsesDefault() {
    TattooRepository repository({
        definition("pack.json", "Pack", "Marks", "Mark", "mark.dds", "Body"),
    });

    const auto page = repository.query(TattooFilter{.pageSize = 0});

    expect(page.pageSize == 24, "expected zero page size to use default");
    expect(page.entries.size() == 1, "expected entry returned with default page size");
}

void searchesAcrossMetadataCaseInsensitively() {
    TattooRepository repository({
        definition("rose.json", "Rose Pack", "Flowers", "Red Rose", "rose/red.dds", "Body"),
        definition("rune.json", "Rune Pack", "Magic", "Blue Rune", "rune/blue.dds", "Face"),
    });

    for (const std::string_view search : {
             "RED ROSE", "ROSE PACK", "ROSE PACK.JSON", "FLOWERS", "BODY", "ROSE/RED"}) {
        const auto page = repository.query(TattooFilter{.search = std::string(search)});

        expect(page.matchedEntries == 1, "expected folded metadata search match");
        expect(page.entries.front().name == "Red Rose", "expected rose result");
        expect(page.totalEntries == 2, "expected unfiltered total preserved");
    }
}

void combinesSourceSectionAndAreaFilters() {
    TattooRepository repository({
        definition("one.json", "One", "Marks", "Body One", "one.dds", "Body"),
        definition("one.json", "One", "Marks", "Face One", "face.dds", "Face"),
        definition("two.json", "Two", "Marks", "Body Two", "two.dds", "Body"),
    });

    const auto page = repository.query(TattooFilter{
        .sourceId = "ONE.JSON",
        .section = "marks",
        .area = "BODY",
    });

    expect(page.matchedEntries == 1, "expected all exact filters combined");
    expect(page.entries.front().name == "Body One",
        "expected only matching source, section, and area");
}

void emptyMatchResetsPaging() {
    TattooRepository repository({
        definition("one.json", "One", "Marks", "Mark", "one.dds", "Body"),
    });

    const auto page = repository.query(TattooFilter{
        .search = "missing",
        .pageIndex = 8,
        .pageSize = 12,
    });

    expect(page.entries.empty(), "expected empty page");
    expect(page.matchedEntries == 0 && page.pageCount == 0,
        "expected zero match and page counts");
    expect(page.pageIndex == 0 && page.pageSize == 12,
        "expected empty result paging normalized");
}

void buildsStableSourceAwareFacets() {
    TattooRepository repository({
        definition("b.json", "Same Pack", "Marks", "B", "b.dds", "BODY"),
        definition("a.json", "Same Pack", "marks", "A", "a.dds", "Body"),
        definition("c.json", "Other", "Runes", "C", "c.dds", "Face"),
    });

    const auto& facets = repository.facets();

    expect(facets.sources.size() == 3,
        "expected identical pack labels to retain distinct sources");
    expect(facets.sources[0] == stui::repository::TattooSourceOption{
            .sourceId = "c.json",
            .packName = "Other",
        },
        "expected folded source display ordering");
    expect(facets.sections == std::vector<std::string>{"Marks", "Runes"},
        "expected folded duplicate-free sections");
    expect(facets.areas == std::vector<std::string>{"BODY", "Face"},
        "expected folded duplicate-free areas");
}

void filtersDomainsAndBuildsDeterministicDomainFacets() {
    auto custom = definition("custom.json", "Custom", "Marks", "Custom One", "custom.dds", "Body");
    custom.domain = "Custom";
    auto customVariant = definition("variant.json", "Variant", "Marks", "Custom Two", "variant.dds", "Body");
    customVariant.domain = "custom";
    TattooRepository repository({
        definition("default.json", "Default", "Marks", "Default", "default.dds", "Body"),
        std::move(custom),
        std::move(customVariant),
    });

    expect(repository.facets().domains == std::vector<std::string>{"Custom", "default"},
        "expected case-insensitive deterministic domain options");
    expect(repository.query(TattooFilter{.domain = "CUSTOM"}).matchedEntries == 2,
        "expected case-insensitive domain filter");
    expect(repository.query(TattooFilter{}).matchedEntries == 3,
        "expected empty domain filter to retain all entries");
}

void classifiesMaterialMetadataWithoutTreatingDefaultsAsCapabilities() {
    const auto legacy = definition(
        "legacy.json", "Pack", "Marks", "Legacy", "legacy.dds", "Body");
    expect(classifyTattooMaterial(legacy) == TattooMaterialClassification{},
        "expected missing legacy metadata to produce no material capabilities");

    auto defaults = definition(
        "defaults.json", "Pack", "Marks", "Defaults", "defaults.dds", "Body");
    defaults.glow = 0;
    defaults.glowTexture = "";
    defaults.emissiveMult = 1.0F;
    defaults.glossiness = 0.0F;
    defaults.specularStrength = 0.0F;
    defaults.bump = "";
    expect(classifyTattooMaterial(defaults) == TattooMaterialClassification{},
        "expected explicit defaults and empty paths to produce no capabilities");

    auto nearDefaultEmission = definition(
        "near.json", "Pack", "Marks", "Near", "near.dds", "Body");
    nearDefaultEmission.emissiveMult =
        1.0F + kTattooMaterialFloatTolerance / 2.0F;
    expect(!classifyTattooMaterial(nearDefaultEmission).glow,
        "expected emission within tolerance to remain default");
}

void classifiesIndependentMaterialSignals() {
    auto glowColor = definition(
        "color.json", "Pack", "Marks", "Glow Color", "color.dds", "Body");
    glowColor.glow = 0x010203;
    expect(classifyTattooMaterial(glowColor) == TattooMaterialClassification{.glow = true},
        "expected nonzero glow color to classify as Glow");

    auto glowTexture = definition(
        "texture.json", "Pack", "Marks", "Glow Texture", "texture.dds", "Body");
    glowTexture.glowTexture = "Pack\\texture_g.dds";
    expect(classifyTattooMaterial(glowTexture).glow,
        "expected non-empty glow texture to classify as Glow");

    auto emission = definition(
        "emission.json", "Pack", "Marks", "Emission", "emission.dds", "Body");
    emission.emissiveMult = 1.0F + kTattooMaterialFloatTolerance * 2.0F;
    expect(classifyTattooMaterial(emission).glow,
        "expected materially non-default emission to classify as Glow");

    auto bump = definition(
        "bump.json", "Pack", "Marks", "Bump", "bump.dds", "Body");
    bump.bump = "Pack\\bump_n.dds";
    expect(classifyTattooMaterial(bump).bump,
        "expected non-empty bump path to classify as Bump");

    auto glossiness = definition(
        "gloss.json", "Pack", "Marks", "Glossiness", "gloss.dds", "Body");
    glossiness.glossiness = 0.5F;
    expect(classifyTattooMaterial(glossiness).gloss,
        "expected positive glossiness to classify as Gloss");

    auto specular = definition(
        "specular.json", "Pack", "Marks", "Specular", "specular.dds", "Body");
    specular.specularStrength = 0.25F;
    expect(classifyTattooMaterial(specular).gloss,
        "expected positive specular strength to classify as Gloss");
}

void materialFiltersComposeWithAndSemanticsAndContextualFacets() {
    auto glowOnly = definition(
        "glow.json", "Glow Pack", "Marks", "Glow", "glow.dds", "Body");
    glowOnly.glow = 1;
    auto bumpOnly = definition(
        "bump.json", "Bump Pack", "Marks", "Bump", "bump.dds", "Body");
    bumpOnly.bump = "Pack\\bump_n.dds";
    auto glossOnly = definition(
        "gloss.json", "Gloss Pack", "Marks", "Gloss", "gloss.dds", "Body");
    glossOnly.glossiness = 1.0F;
    auto allThree = definition(
        "all.json", "All Pack", "Runes", "All Three", "all.dds", "Body");
    allThree.glowTexture = "Pack\\all_g.dds";
    allThree.bump = "Pack\\all_n.dds";
    allThree.specularStrength = 1.0F;
    const auto legacy = definition(
        "legacy.json", "Legacy Pack", "Marks", "Legacy", "legacy.dds", "Body");
    TattooRepository repository({glowOnly, bumpOnly, glossOnly, allThree, legacy});

    expect(repository.query(TattooFilter{.glowOnly = true}).matchedEntries == 2,
        "expected Glow filter to retain every Glow definition");
    expect(repository.query(TattooFilter{.bumpOnly = true}).matchedEntries == 2,
        "expected Bump filter to retain every Bump definition");
    expect(repository.query(TattooFilter{.glossOnly = true}).matchedEntries == 2,
        "expected Gloss filter to retain every Gloss definition");

    const TattooFilter combined{
        .area = "Body",
        .glowOnly = true,
        .bumpOnly = true,
        .glossOnly = true,
    };
    const auto page = repository.query(combined);
    const auto facets = repository.contextualFacets(combined);
    expect(page.matchedEntries == 1 && page.entries.front().name == "All Three",
        "expected enabled material filters to compose with AND semantics");
    expect(facets.sources == std::vector<stui::repository::TattooSourceOption>{
               {.sourceId = "all.json", .packName = "All Pack"},
           } && facets.sections == std::vector<std::string>{"Runes"},
        "expected material filters to narrow contextual facets");

    const auto unfiltered = repository.query();
    expect(unfiltered.matchedEntries == 5 && unfiltered.entries.front().name == "All Three",
        "expected disabled material filters to preserve catalog results and ordering");
}

void contextualFacetsFollowAreaThenSource() {
    auto customBody = definition("body-b.json", "Body B", "Runes", "B", "b.dds", "BODY");
    customBody.domain = "custom";
    TattooRepository repository({
        definition("body-a.json", "Body A", "Marks", "A", "a.dds", "Body"),
        std::move(customBody),
        definition("face.json", "Face", "Face Marks", "C", "c.dds", "Face"),
    });

    const auto bodyFacets = repository.contextualFacets(TattooFilter{.area = "body"});
    expect(bodyFacets.sources == std::vector<stui::repository::TattooSourceOption>{
               {.sourceId = "body-a.json", .packName = "Body A"},
               {.sourceId = "body-b.json", .packName = "Body B"},
           },
        "expected Body Source options to exclude Face-only sources");
    expect(bodyFacets.sections == std::vector<std::string>{"Marks", "Runes"},
        "expected Body Section options from every Body source");

    const auto sourceFacets = repository.contextualFacets(TattooFilter{
        .search = "not present",
        .sourceId = "BODY-A.JSON",
        .section = "not present",
        .area = "BODY",
    });
    expect(sourceFacets.sources == bodyFacets.sources,
        "expected Source options to depend only on Area");
    expect(sourceFacets.sections == std::vector<std::string>{"Marks"},
        "expected Section options narrowed only by Area and Source");

    const auto customFacets = repository.contextualFacets(TattooFilter{
        .domain = "CUSTOM",
        .area = "body",
    });
    expect(customFacets.sources == std::vector<stui::repository::TattooSourceOption>{
               {.sourceId = "body-b.json", .packName = "Body B"},
           } && customFacets.sections == std::vector<std::string>{"Runes"},
        "expected Domain to narrow Source and Section facets after Area");
}

void appliedIdentityFilterPrecedesPaginationAndNarrowsFacets() {
    TattooRepository repository({
        definition("one.json", "One", "Marks", "Applied A", "a.dds", "Body"),
        definition("one.json", "One", "Marks", "Unused", "unused.dds", "Body"),
        definition("two.json", "Two", "Runes", "Applied B", "b.dds", "Body"),
        definition("face.json", "Face", "Marks", "Applied A", "face.dds", "Face"),
    });
    const TattooFilter filter{
        .area = "Body",
        .appliedIdentities = std::vector<TattooIdentity>{
            {.section = "Marks", .name = "Applied A"},
            {.section = "Runes", .name = "Applied B"},
        },
        .pageSize = 1,
    };

    const auto firstPage = repository.query(filter);
    auto secondPageFilter = filter;
    secondPageFilter.pageIndex = 1;
    const auto secondPage = repository.query(secondPageFilter);
    const auto facets = repository.contextualFacets(filter);

    expect(firstPage.matchedEntries == 2 && firstPage.pageCount == 2 &&
            firstPage.entries.size() == 1 && firstPage.entries.front().name == "Applied A",
        "expected applied identities filtered before pagination");
    expect(secondPage.entries.size() == 1 && secondPage.entries.front().name == "Applied B",
        "expected second applied tattoo on the second filtered page");
    expect(facets.sources == std::vector<stui::repository::TattooSourceOption>{
               {.sourceId = "one.json", .packName = "One"},
               {.sourceId = "two.json", .packName = "Two"},
           } && facets.sections == std::vector<std::string>{"Marks", "Runes"},
        "expected contextual facets limited to applied Body tattoos");
}

void appliedIdentityFilterUsesRuntimeExactIdentity() {
    TattooRepository repository({
        definition("one.json", "One", "Marks", "Corruption", "a.dds", "Body"),
    });

    const auto exact = repository.query(TattooFilter{
        .appliedIdentities = std::vector<TattooIdentity>{
            {.section = "Marks", .name = "Corruption"},
        },
    });
    const auto differentCase = repository.query(TattooFilter{
        .appliedIdentities = std::vector<TattooIdentity>{
            {.section = "Marks", .name = "corruption"},
        },
    });
    const auto noneApplied = repository.query(TattooFilter{
        .appliedIdentities = std::vector<TattooIdentity>{},
    });

    expect(exact.matchedEntries == 1, "expected exact runtime Tattoo Identity match");
    expect(differentCase.matchedEntries == 0,
        "expected Tattoo Identity matching to remain case-sensitive");
    expect(noneApplied.matchedEntries == 0,
        "expected active empty applied filter to return no tattoos");
}

void recentIdentityFilterUsesExactAreaAndNewestFirstOrder() {
    auto alpha = definition("a.json", "Pack", "Marks", "Alpha", "a.dds", "Body");
    auto beta = definition("b.json", "Pack", "Marks", "Beta", "b.dds", "Body");
    auto face = definition("a.json", "Pack", "Marks", "Alpha", "f.dds", "Face");
    TattooRepository repository({alpha, beta, face});
    const std::vector<RecentTattooIdentity> recent{
        {.domain = "default", .sourceId = "b.json", .section = "Marks",
            .name = "Beta", .area = stui::core::TattooArea::body},
        {.domain = "default", .sourceId = "a.json", .section = "Marks",
            .name = "Alpha", .area = stui::core::TattooArea::body},
    };

    const auto ordered = repository.query(TattooFilter{
        .area = "Body",
        .recentIdentities = recent,
        .pageSize = 1,
    });
    const auto second = repository.query(TattooFilter{
        .area = "Body",
        .recentIdentities = recent,
        .pageIndex = 1,
        .pageSize = 1,
    });
    const auto empty = repository.query(TattooFilter{
        .area = "Face",
        .recentIdentities = recent,
    });

    expect(ordered.matchedEntries == 2 && ordered.entries.front().name == "Beta",
        "expected newest exact recent identity before pagination");
    expect(second.entries.front().name == "Alpha",
        "expected older recent identity on the next page");
    expect(empty.matchedEntries == 0,
        "expected area-scoped history not to match another area");
}

void emptyRepositoryReturnsEmptyPageAndFacets() {
    TattooRepository repository(std::vector<TattooDefinition>{});

    const auto page = repository.query();
    const auto& facets = repository.facets();

    expect(page.entries.empty() && page.totalEntries == 0 && page.matchedEntries == 0,
        "expected empty repository counts");
    expect(page.pageIndex == 0 && page.pageCount == 0,
        "expected empty repository paging");
    expect(facets.sources.empty() && facets.sections.empty() && facets.areas.empty(),
        "expected empty repository facets");
}

void queriesLargeLibraryWithoutExternalDependencies() {
    std::vector<TattooDefinition> definitions;
    definitions.reserve(2048);
    for (std::size_t index = 0; index < 2048; ++index) {
        definitions.push_back(definition(
            "pack.json",
            "Pack",
            index % 2 == 0 ? "Even" : "Odd",
            "Mark " + std::to_string(index),
            std::to_string(index) + ".dds",
            "Body",
            index));
    }
    TattooRepository repository(std::move(definitions));

    const auto page = repository.query(TattooFilter{
        .section = "even",
        .pageIndex = 1,
        .pageSize = 48,
    });

    expect(page.totalEntries == 2048 && page.matchedEntries == 1024,
        "expected complete metadata counts");
    expect(page.entries.size() == 48 && page.pageCount == 22,
        "expected bounded page from large library");
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
    failures += run(
        "orders definitions and preserves metadata",
        ordersDefinitionsDeterministicallyAndPreservesMetadata);
    failures += run("paginates and clamps out-of-range page", paginatesAndClampsOutOfRangePage);
    failures += run("zero page size uses default", zeroPageSizeUsesDefault);
    failures += run(
        "searches across metadata case-insensitively",
        searchesAcrossMetadataCaseInsensitively);
    failures += run(
        "combines source, section, and area filters",
        combinesSourceSectionAndAreaFilters);
    failures += run("empty match resets paging", emptyMatchResetsPaging);
    failures += run("builds stable source-aware facets", buildsStableSourceAwareFacets);
    failures += run("filters domains and builds deterministic domain facets", filtersDomainsAndBuildsDeterministicDomainFacets);
    failures += run(
        "classifies defaults without false material capabilities",
        classifiesMaterialMetadataWithoutTreatingDefaultsAsCapabilities);
    failures += run(
        "classifies independent material signals",
        classifiesIndependentMaterialSignals);
    failures += run(
        "material filters use AND semantics and narrow facets",
        materialFiltersComposeWithAndSemanticsAndContextualFacets);
    failures += run(
        "contextual facets follow Area then Source",
        contextualFacetsFollowAreaThenSource);
    failures += run(
        "applied identity filter precedes pagination and narrows facets",
        appliedIdentityFilterPrecedesPaginationAndNarrowsFacets);
    failures += run(
        "applied identity filter uses runtime-exact identity",
        appliedIdentityFilterUsesRuntimeExactIdentity);
    failures += run(
        "recent identity filter uses exact area and newest-first order",
        recentIdentityFilterUsesExactAreaAndNewestFirstOrder);
    failures += run(
        "empty repository returns empty page and facets",
        emptyRepositoryReturnsEmptyPageAndFacets);
    failures += run(
        "queries large library without external dependencies",
        queriesLargeLibraryWithoutExternalDependencies);
    return failures == 0 ? 0 : 1;
}
