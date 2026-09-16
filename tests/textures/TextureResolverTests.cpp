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
    failures += run("normalizes relative texture path", normalizesRelativeTattooTexturePath);
    failures += run("rejects paths outside tattoo root", rejectsPathsOutsideTattooRoot);
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
