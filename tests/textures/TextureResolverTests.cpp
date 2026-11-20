#include "textures/TextureResolver.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using stui::textures::TextureResolveError;
using stui::textures::TextureResolver;
using stui::textures::TextureSource;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = fs::temp_directory_path() /
            ("slavetats-ui-texture-resolver-" + std::to_string(unique));
        fs::create_directories(m_path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(m_path, error);
    }

    void write(const fs::path& relativePath, const std::vector<std::uint8_t>& bytes) const {
        fs::create_directories((m_path / relativePath).parent_path());
        std::ofstream stream(m_path / relativePath, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    [[nodiscard]] const fs::path& path() const noexcept {
        return m_path;
    }

private:
    fs::path m_path;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void normalizesRelativeTattooTexturePath() {
    const auto path = TextureResolver::normalize("LewdMarks/Folder\\mark.dds");

    expect(path.has_value(), "expected relative texture path accepted");
    expect(*path == "LewdMarks\\Folder\\mark.dds",
        "expected canonical resource separators");
}

void rejectsPathsOutsideTattooRoot() {
    for (const std::string_view path : {
             "../outside.dds", "folder/../../outside.dds", "C:\\outside.dds", "/outside.dds"}) {
        expect(TextureResolver::normalize(path) ==
                std::unexpected(TextureResolveError::invalidPath),
            "expected unsafe texture path rejected");
    }
}

void separatesTypedResourceIdentities() {
    using stui::textures::TextureResourceKind;
    const auto catalog = TextureResolver::resource(TextureResourceKind::catalog, "mark.dds");
    const auto external = TextureResolver::resource(TextureResourceKind::external, "Textures/Actors/Character/SlaveTats/mark.DDS");
    expect(catalog && external && catalog->path() == external->path() && *catalog != *external, "same Data path with different kinds must never collide");
    const auto variant = TextureResolver::resource(TextureResourceKind::external, "textures\\actors\\character\\slavetats\\mark.dds");
    expect(variant && *external == *variant, "external identity must normalize case and separators");
    expect(!TextureResolver::resource(TextureResourceKind::external, "../mark.dds"), "unsafe external identity must be rejected");
}

void matchesOnlyValidatedExternalProperties() {
    expect(TextureResolver::matchingExternal("Actors/Character/mark.DDS", "textures/actors/character/mark.dds").has_value(), "equivalent normalized paths must match");
    expect(!TextureResolver::matchingExternal("textures/a.dds", "textures/b.dds"), "conflicting paths must be unavailable");
    expect(!TextureResolver::matchingExternal("textures/../a.dds", "textures/../a.dds"), "matching malformed paths must be unavailable");
    expect(!TextureResolver::matchingExternal("", "textures/a.dds"), "missing property must be unavailable");
}
void validatesExternalTextureResources() {
    const auto relative = TextureResolver::normalizeExternal("Actors/Character/Overlays/example.dds");
    expect(relative && *relative == "textures\\Actors\\Character\\Overlays\\example.dds", "expected texture-relative runtime path under Data/Textures");
    const auto valid = TextureResolver::normalizeExternal("Textures/actors/example.DDS");
    expect(valid && *valid == "textures\\actors\\example.DDS",
        "expected normalized Data-relative texture resource");
    for (const std::string_view path : std::initializer_list<std::string_view>{
             "../textures/a.dds", "C:/textures/a.dds", "//server/textures/a.dds",
             "meshes/a.dds", "textures/a.png", "textures/../a.dds",
             std::string_view("textures/a.dds\0evil", 19)}) {
        expect(TextureResolver::normalizeExternal(path) ==
                std::unexpected(TextureResolveError::invalidPath),
            "expected unsafe external texture rejected");
    }
}

void resolvesTypedIdentityUsingItsResourceRoot() {
    TemporaryDirectory directory;
    TextureResolver resolver(directory.path() / "textures" / "actors" / "character" / "slavetats");
    using stui::textures::TextureResourceKind;
    const auto catalog = TextureResolver::resource(TextureResourceKind::catalog, "mark.dds");
    const auto external = TextureResolver::resource(TextureResourceKind::external, "textures/other/mark.dds");
    expect(catalog && external, "expected valid identities");
    std::vector<std::string> requested;
    const auto read = [&](std::string_view path) {
        requested.emplace_back(path);
        return stui::textures::TextureBytesResult(std::vector<std::uint8_t>{1});
    };
    expect(resolver.resolve(*catalog, read).has_value() && resolver.resolve(*external, read).has_value(), "both resources resolve");
    expect(requested == std::vector<std::string>{"textures\\actors\\character\\slavetats\\mark.dds", "textures\\other\\mark.dds"}, "each kind must resolve its own Data path");
}
void externalArchiveUsesDataRelativePath() {
    TemporaryDirectory directory;
    TextureResolver resolver(directory.path());
    std::string requested;
    const auto result = resolver.resolveExternal("Actors/Character/mark.dds", [&](std::string_view path) {
        requested = path;
        return stui::textures::TextureBytesResult(std::vector<std::uint8_t>{3, 4});
    });
    expect(result && result->bytes == std::vector<std::uint8_t>({3, 4}), "external archive bytes should load");
    expect(requested == "textures\\Actors\\Character\\mark.dds", "external archive must use Data-relative identity");
}
void externalLooseResourceTakesPrecedence() {
    TemporaryDirectory directory;
    directory.write(fs::path("textures") / "Actors" / "Character" / "mark.dds", {8, 9});
    const auto tattooRoot = directory.path() / "textures" / "actors" / "character" / "slavetats";
    TextureResolver resolver(tattooRoot);
    bool called = false;
    const auto result = resolver.resolveExternal("Actors/Character/mark.dds", [&](std::string_view) {
        called = true;
        return stui::textures::TextureBytesResult(std::vector<std::uint8_t>{1});
    });
    expect(result && result->source == TextureSource::loose && result->bytes == std::vector<std::uint8_t>({8, 9}),
        "expected external loose file beneath Data/Textures");
    expect(!called, "archive must not be consulted after loose hit");
}
void externalRedirectCannotEscapeTextureRoot() {
    TemporaryDirectory directory;
    TemporaryDirectory outside;
    outside.write("outside.dds", {4, 2});
    const auto textureRoot = directory.path() / "textures";
    fs::create_directories(textureRoot / "Actors");
    std::error_code error;
    fs::create_symlink(outside.path() / "outside.dds", textureRoot / "Actors" / "escape.dds", error);
    if (error) {
        std::cout << "SKIP external symlink containment unavailable: " << error.message() << '\n';
        return;
    }
    TextureResolver resolver(textureRoot / "actors" / "character" / "slavetats");
    const auto result = resolver.resolveExternal("Actors/escape.dds", [](std::string_view) {
        return stui::textures::TextureBytesResult(std::vector<std::uint8_t>{7});
    });
    expect(result && result->source == TextureSource::archive && result->bytes == std::vector<std::uint8_t>{7},
        "redirect outside texture root must use resource reader rather than loose file");
}
void looseTextureTakesPrecedenceOverArchive() {
    TemporaryDirectory directory;
    directory.write(fs::path("Pack") / "mark.dds", {1, 2, 3, 4});
    TextureResolver resolver(directory.path());
    bool archiveCalled = false;

    const auto result = resolver.resolve("Pack/mark.dds", [&](std::string_view) {
        archiveCalled = true;
        return stui::textures::TextureBytesResult(std::vector<std::uint8_t>{9});
    });

    expect(result.has_value(), "expected loose texture resolution");
    expect(result->source == TextureSource::loose, "expected loose source");
    expect(result->bytes == std::vector<std::uint8_t>({1, 2, 3, 4}),
        "expected exact loose DDS bytes");
    expect(!archiveCalled, "expected archive skipped after loose hit");
}

void archiveFallbackReceivesNormalizedResourcePath() {
    TemporaryDirectory directory;
    TextureResolver resolver(directory.path());
    std::string requestedPath;

    const auto result = resolver.resolve("Pack/mark.dds", [&](std::string_view path) {
        requestedPath = path;
        return stui::textures::TextureBytesResult(std::vector<std::uint8_t>{5, 6});
    });

    expect(result.has_value(), "expected archive fallback resolution");
    expect(result->source == TextureSource::archive, "expected archive source");
    expect(result->bytes == std::vector<std::uint8_t>({5, 6}),
        "expected exact archive DDS bytes");
    expect(requestedPath == "textures\\actors\\character\\slavetats\\Pack\\mark.dds",
        "expected normalized full resource path");
}

void archiveFailureIsPreserved() {
    TemporaryDirectory directory;
    TextureResolver resolver(directory.path());

    const auto result = resolver.resolve("missing.dds", [](std::string_view) {
        return stui::textures::TextureBytesResult(
            std::unexpected(TextureResolveError::notFound));
    });

    expect(result == std::unexpected(TextureResolveError::notFound),
        "expected archive failure preserved");
}

void explicitLooseAndArchiveResolutionStaySeparate() {
    TemporaryDirectory directory;
    directory.write(fs::path("Pack") / ".keep", {});
    TextureResolver resolver(directory.path());

    const auto loose = resolver.resolveLoose("Pack");
    expect(loose == std::unexpected(TextureResolveError::notFound),
        "expected loose miss without archive access");

    std::string requestedPath;
    const auto archive = resolver.resolveArchive("Pack", [&](std::string_view path) {
        requestedPath = path;
        return stui::textures::TextureBytesResult(std::vector<std::uint8_t>{7, 8});
    });

    expect(archive.has_value(), "expected explicit archive resolution");
    expect(archive->source == TextureSource::archive, "expected archive source");
    expect(archive->bytes == std::vector<std::uint8_t>({7, 8}),
        "expected archive bytes");
    expect(requestedPath == "textures\\actors\\character\\slavetats\\Pack",
        "expected canonical archive resource path");
}

void emptyArchiveReaderReturnsError() {
    TemporaryDirectory directory;
    TextureResolver resolver(directory.path());
    const auto result = resolver.resolveArchive("missing.dds", {});

    expect(result == std::unexpected(TextureResolveError::readFailed),
        "expected empty archive reader to return an error");
}

void symlinkCannotEscapeLooseRoot() {
    TemporaryDirectory root;
    TemporaryDirectory outside;
    outside.write("outside.dds", {4, 2});
    std::error_code error;
    fs::create_symlink(outside.path() / "outside.dds", root.path() / "link.dds", error);
    if (error) {
        std::cout << "SKIP symlink containment unavailable: " << error.message() << '\n';
        return;
    }
    TextureResolver resolver(root.path());
    const auto result = resolver.resolveLoose("link.dds");
    expect(result == std::unexpected(TextureResolveError::invalidPath),
        "expected symlink outside loose root rejected");
}

void redirectedLoosePathFallsBackToArchiveResolution() {
    TemporaryDirectory root;
    TemporaryDirectory redirected;
    redirected.write("redirected.dds", {4, 2});
    std::error_code error;
    fs::create_symlink(
        redirected.path() / "redirected.dds",
        root.path() / "redirected.dds",
        error);
    if (error) {
        std::cout << "SKIP redirected-path fallback unavailable: " << error.message() << '\n';
        return;
    }
    TextureResolver resolver(root.path());
    std::string requestedPath;

    const auto result = resolver.resolve("redirected.dds", [&](std::string_view path) {
        requestedPath = path;
        return stui::textures::TextureBytesResult(std::vector<std::uint8_t>{7, 8});
    });

    expect(result.has_value(),
        "expected redirected loose path to fall back to resource resolution");
    expect(result->source == TextureSource::archive,
        "expected redirected path to use the safe resource reader");
    expect(result->bytes == std::vector<std::uint8_t>({7, 8}),
        "expected bytes from the resource reader instead of the redirected loose path");
    expect(requestedPath ==
            "textures\\actors\\character\\slavetats\\redirected.dds",
        "expected canonical resource path after loose redirection");
}

void rejectsOversizedLooseFileBeforeRead() {
    TemporaryDirectory directory;
    directory.write("huge.dds", {});
    fs::resize_file(directory.path() / "huge.dds", 128ULL * 1024 * 1024 + 1);
    TextureResolver resolver(directory.path());
    const auto result = resolver.resolveLoose("huge.dds");
    expect(!result && result.error() == TextureResolveError::readFailed,
        "oversized loose DDS must fail before allocating file contents");
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
    failures += run("bounds loose texture allocations", rejectsOversizedLooseFileBeforeRead);
    failures += run("normalizes relative texture path", normalizesRelativeTattooTexturePath);
    failures += run("rejects paths outside tattoo root", rejectsPathsOutsideTattooRoot);
    failures += run("separates typed resource identities", separatesTypedResourceIdentities);
    failures += run("matches only validated external properties", matchesOnlyValidatedExternalProperties);
    failures += run("validates external texture resources", validatesExternalTextureResources);
    failures += run("external redirect cannot escape texture root", externalRedirectCannotEscapeTextureRoot);
    failures += run("external loose file precedes archive", externalLooseResourceTakesPrecedence);
    failures += run("resolves typed identities in their own roots", resolvesTypedIdentityUsingItsResourceRoot);
    failures += run("external archive uses Data-relative path", externalArchiveUsesDataRelativePath);
    failures += run("loose texture precedes archive", looseTextureTakesPrecedenceOverArchive);
    failures += run(
        "archive fallback receives normalized resource path",
        archiveFallbackReceivesNormalizedResourcePath);
    failures += run("archive failure is preserved", archiveFailureIsPreserved);
    failures += run(
        "loose and archive resolution stay separate",
        explicitLooseAndArchiveResolutionStaySeparate);
    failures += run("empty archive reader returns error", emptyArchiveReaderReturnsError);
    failures += run(
        "symlink cannot escape loose root",
        symlinkCannotEscapeLooseRoot);
    failures += run(
        "redirected loose path falls back to archive resolution",
        redirectedLoosePathFallsBackToArchiveResolution);
    return failures == 0 ? 0 : 1;
}
