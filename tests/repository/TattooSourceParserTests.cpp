#include "repository/TattooSourceParser.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace fs = std::filesystem;
using stui::repository::TattooSourceFile;
using stui::repository::parseTattooSource;

class TemporaryJsonFile {
public:
    explicit TemporaryJsonFile(std::string_view json) {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = fs::temp_directory_path() / ("slavetats-ui-parser-" + std::to_string(unique) + ".json");
        std::ofstream stream(m_path);
        stream << json;
    }

    ~TemporaryJsonFile() {
        std::error_code error;
        fs::remove(m_path, error);
    }

    [[nodiscard]] TattooSourceFile source() const {
        return TattooSourceFile{
            .sourceId = "textures/actors/character/slavetats/sample.json",
            .sourceFile = "Sample.json",
            .packName = "Sample",
            .effectivePath = m_path,
        };
    }

private:
    fs::path m_path;
};

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

void parserPreservesSourceAndOptionalMetadata() {
    TemporaryJsonFile file(R"([{"name":"Mark","section":"Marks","texture":"Pack\\mark.dds","area":"Body","glow":16777215,"in_bsa":1,"credit":"Artist","glowTexture":"Pack\\mark_g.dds","bump":"Pack\\mark_n.dds","emissiveMult":3.75,"glossiness":2.5,"specularStrength":1.25}])");

    const auto report = parseTattooSource(file.source());

    expect(report.issues.empty(), "expected valid source without issues");
    expect(report.definitions.size() == 1, "expected one definition");
    const auto& tattoo = report.definitions.front();
    expect(tattoo.sourceId == "textures/actors/character/slavetats/sample.json", "expected source ID");
    expect(tattoo.sourceFile == "Sample.json", "expected source filename");
    expect(tattoo.packName == "Sample", "expected pack name");
    expect(tattoo.sourceIndex == 0, "expected source entry index");
    expect(tattoo.name == "Mark" && tattoo.section == "Marks", "expected name and section");
    expect(tattoo.texturePath == "Pack\\mark.dds" && tattoo.area == "Body", "expected texture and area");
    expect(tattoo.glow == 16777215, "expected optional glow");
    expect(tattoo.inBsa == true, "expected integer in_bsa normalized to bool");
    expect(tattoo.credit == "Artist", "expected optional credit");
    expect(tattoo.glowTexture == "Pack\\mark_g.dds", "expected glow texture metadata");
    expect(tattoo.bump == "Pack\\mark_n.dds", "expected bump metadata");
    expect(tattoo.emissiveMult == 3.75F, "expected emissive multiplier metadata");
    expect(tattoo.glossiness == 2.5F, "expected glossiness metadata");
    expect(tattoo.specularStrength == 1.25F, "expected specular strength metadata");
}

void legacyEntryLeavesAdvancedMaterialMetadataAbsent() {
    TemporaryJsonFile file(R"([{"name":"Legacy","section":"Marks","texture":"Pack\\legacy.dds","area":"Body"}])");

    const auto report = parseTattooSource(file.source());

    expect(report.issues.empty(), "expected legacy source without issues");
    expect(report.definitions.size() == 1, "expected one legacy definition");
    const auto& tattoo = report.definitions.front();
    expect(!tattoo.glowTexture.has_value(), "expected absent legacy glow texture metadata");
    expect(!tattoo.bump.has_value(), "expected absent legacy bump metadata");
    expect(!tattoo.emissiveMult.has_value(), "expected absent legacy emissive multiplier metadata");
    expect(!tattoo.glossiness.has_value(), "expected absent legacy glossiness metadata");
    expect(!tattoo.specularStrength.has_value(), "expected absent legacy specular strength metadata");
}

void parserPreservesAndNormalizesDomains() {
    TemporaryJsonFile file(R"([
        {"name":"Custom","section":"Marks","texture":"Pack\\custom.dds","area":"Body","domain":"custom"},
        {"name":"Missing","section":"Marks","texture":"Pack\\missing.dds","area":"Body"},
        {"name":"Empty","section":"Marks","texture":"Pack\\empty.dds","area":"Body","domain":""}
    ])");

    const auto report = parseTattooSource(file.source());

    expect(report.issues.empty() && report.definitions.size() == 3,
        "expected valid domain fixture entries");
    expect(report.definitions[0].domain == "custom",
        "expected explicit domain preserved");
    expect(report.definitions[1].domain == "default",
        "expected missing domain normalized to default");
    expect(report.definitions[2].domain == "default",
        "expected empty domain normalized to default");
}

void malformedSiblingIsSkippedWithoutDiscardingValidEntry() {
    TemporaryJsonFile file(R"([{"name":"Missing texture","section":"Broken","area":"Body"},{"name":"Good","section":"Pack","texture":"Pack\\good.dds","area":"Body"}])");

    const auto report = parseTattooSource(file.source());

    expect(report.definitions.size() == 1, "expected valid sibling to remain");
    expect(report.definitions.front().name == "Good", "expected the valid sibling");
    expect(report.definitions.front().sourceIndex == 1, "expected original source index");
    expect(report.issues.size() == 1, "expected one indexed issue");
    expect(report.issues.front().entryIndex == 0, "expected malformed entry index");
}

void invalidRootProducesSourceIssue() {
    TemporaryJsonFile file(R"({"name":"not an array"})");

    const auto report = parseTattooSource(file.source());

    expect(report.definitions.empty(), "expected no definitions from invalid root");
    expect(report.issues.size() == 1, "expected one source issue");
    expect(!report.issues.front().entryIndex.has_value(), "expected root issue without entry index");
}

void invalidJsonProducesSourceIssue() {
    TemporaryJsonFile file(R"([{"name":"unfinished"})");

    const auto report = parseTattooSource(file.source());

    expect(report.definitions.empty(), "expected no definitions from invalid JSON");
    expect(report.issues.size() == 1, "expected one invalid JSON issue");
    expect(!report.issues.front().entryIndex.has_value(), "expected JSON issue without entry index");
}

void invalidOptionalFieldDoesNotDiscardValidSibling() {
    TemporaryJsonFile file(R"([{"name":"Bad","section":"Pack","texture":"Pack\\bad.dds","area":"Body","credit":42},{"name":"Good","section":"Pack","texture":"Pack\\good.dds","area":"Body"}])");

    const auto report = parseTattooSource(file.source());

    expect(report.definitions.size() == 1, "expected valid sibling after invalid optional field");
    expect(report.definitions.front().name == "Good", "expected valid sibling to remain");
    expect(report.issues.size() == 1, "expected one optional field issue");
    expect(report.issues.front().entryIndex == 0, "expected invalid optional field entry index");
}

void invalidDomainDoesNotDiscardValidSibling() {
    TemporaryJsonFile file(R"([
        {"name":"Bad","section":"Pack","texture":"Pack\\bad.dds","area":"Body","domain":42},
        {"name":"Good","section":"Pack","texture":"Pack\\good.dds","area":"Body"}
    ])");

    const auto report = parseTattooSource(file.source());

    expect(report.definitions.size() == 1 && report.definitions.front().name == "Good",
        "expected valid sibling after invalid domain");
    expect(report.issues.size() == 1 && report.issues.front().entryIndex == 0 &&
            report.issues.front().message == "field 'domain' must be a string",
        "expected explicit invalid domain issue");
}

void malformedAdvancedMaterialFieldDoesNotDiscardValidSibling(std::string_view malformedField) {
    const std::string json =
        "[{\"name\":\"Bad\",\"section\":\"Pack\",\"texture\":\"Pack\\\\bad.dds\",\"area\":\"Body\"," +
        std::string(malformedField) +
        "},{\"name\":\"Good\",\"section\":\"Pack\",\"texture\":\"Pack\\\\good.dds\",\"area\":\"Body\"}]";
    TemporaryJsonFile file(json);

    const auto report = parseTattooSource(file.source());

    expect(report.definitions.size() == 1, "expected valid sibling after malformed advanced material field");
    expect(report.definitions.front().name == "Good", "expected valid advanced material sibling");
    expect(report.issues.size() == 1, "expected one advanced material field issue");
    expect(report.issues.front().entryIndex == 0, "expected malformed advanced material field entry index");
}

void invalidGlowTextureDoesNotDiscardValidSibling() {
    malformedAdvancedMaterialFieldDoesNotDiscardValidSibling("\"glowTexture\":17");
}

void invalidBumpDoesNotDiscardValidSibling() {
    malformedAdvancedMaterialFieldDoesNotDiscardValidSibling("\"bump\":false");
}

void negativeEmissiveMultiplierDoesNotDiscardValidSibling() {
    malformedAdvancedMaterialFieldDoesNotDiscardValidSibling("\"emissiveMult\":-0.1");
}

void invalidGlossinessDoesNotDiscardValidSibling() {
    malformedAdvancedMaterialFieldDoesNotDiscardValidSibling("\"glossiness\":\"high\"");
}

void invalidSpecularStrengthDoesNotDiscardValidSibling() {
    malformedAdvancedMaterialFieldDoesNotDiscardValidSibling("\"specularStrength\":null");
}

void overflowingMaterialFloatDoesNotDiscardValidSibling() {
    malformedAdvancedMaterialFieldDoesNotDiscardValidSibling("\"emissiveMult\":1e100");
}

void missingSourceProducesSourceIssue() {
    TattooSourceFile source{
        .sourceId = "textures/actors/character/slavetats/missing.json",
        .sourceFile = "Missing.json",
        .packName = "Missing",
        .effectivePath = fs::temp_directory_path() / "slavetats-ui-source-that-does-not-exist.json",
    };

    const auto report = parseTattooSource(source);

    expect(report.definitions.empty(), "expected no definitions from missing source");
    expect(report.issues.size() == 1, "expected one missing source issue");
    expect(!report.issues.front().entryIndex.has_value(), "expected missing source issue without entry index");
}

void nonObjectEntryDoesNotDiscardValidSibling() {
    TemporaryJsonFile file(R"([7,{"name":"Good","section":"Pack","texture":"Pack\\good.dds","area":"Body"}])");

    const auto report = parseTattooSource(file.source());

    expect(report.definitions.size() == 1, "expected valid sibling after non-object entry");
    expect(report.definitions.front().name == "Good", "expected valid object sibling");
    expect(report.issues.size() == 1, "expected one non-object entry issue");
    expect(report.issues.front().entryIndex == 0, "expected non-object entry index");
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
    failures += run("parser preserves source and optional metadata", parserPreservesSourceAndOptionalMetadata);
    failures += run("legacy entry leaves advanced material metadata absent", legacyEntryLeavesAdvancedMaterialMetadataAbsent);
    failures += run("parser preserves and normalizes domains", parserPreservesAndNormalizesDomains);
    failures += run("malformed sibling does not discard valid entry", malformedSiblingIsSkippedWithoutDiscardingValidEntry);
    failures += run("invalid root produces source issue", invalidRootProducesSourceIssue);
    failures += run("invalid JSON produces source issue", invalidJsonProducesSourceIssue);
    failures += run("invalid optional field does not discard valid sibling", invalidOptionalFieldDoesNotDiscardValidSibling);
    failures += run("invalid domain does not discard valid sibling", invalidDomainDoesNotDiscardValidSibling);
    failures += run("invalid glow texture does not discard valid sibling", invalidGlowTextureDoesNotDiscardValidSibling);
    failures += run("invalid bump does not discard valid sibling", invalidBumpDoesNotDiscardValidSibling);
    failures += run("negative emissive multiplier does not discard valid sibling", negativeEmissiveMultiplierDoesNotDiscardValidSibling);
    failures += run("invalid glossiness does not discard valid sibling", invalidGlossinessDoesNotDiscardValidSibling);
    failures += run("invalid specular strength does not discard valid sibling", invalidSpecularStrengthDoesNotDiscardValidSibling);
    failures += run("overflowing material float does not discard valid sibling", overflowingMaterialFloatDoesNotDiscardValidSibling);
    failures += run("missing source produces source issue", missingSourceProducesSourceIssue);
    failures += run("non-object entry does not discard valid sibling", nonObjectEntryDoesNotDiscardValidSibling);
    return failures == 0 ? 0 : 1;
}
