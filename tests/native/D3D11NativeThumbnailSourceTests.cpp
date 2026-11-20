#include "native/D3D11NativeThumbnailSource.h"
#include "textures/ThumbnailDiskCache.h"
#include "textures/ScheduledTextureArchiveReader.h"

#include <DirectXTex.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <chrono>
#include <cstring>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using stui::native::D3D11NativeThumbnailSource;
using stui::native::NativeThumbnailFailure;
using stui::native::NativeThumbnailFailureStage;
using stui::textures::TextureBytesResult;
using stui::textures::TextureResolveError;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = fs::temp_directory_path() /
            ("slavetats-ui-native-thumbnail-source-" + std::to_string(unique));
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

    [[nodiscard]] const fs::path& path() const noexcept { return m_path; }

private:
    fs::path m_path;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

Microsoft::WRL::ComPtr<ID3D11Device> createDevice() {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    D3D_FEATURE_LEVEL featureLevel{};
    const HRESULT result = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, device.GetAddressOf(), &featureLevel, nullptr);
    if (FAILED(result)) {
        throw std::runtime_error("could not create WARP D3D11 device");
    }
    return device;
}

std::vector<std::uint8_t> makeDds(std::size_t width, std::size_t height) {
    DirectX::ScratchImage source;
    if (FAILED(source.Initialize2D(
            DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1))) {
        throw std::runtime_error("could not create DDS fixture");
    }

    DirectX::Blob dds;
    if (FAILED(DirectX::SaveToDDSMemory(
            source.GetImages(), source.GetImageCount(), source.GetMetadata(),
            DirectX::DDS_FLAGS_NONE, dds))) {
        throw std::runtime_error("could not encode DDS fixture");
    }

    const auto* begin = static_cast<const std::uint8_t*>(dds.GetBufferPointer());
    return {begin, begin + dds.GetBufferSize()};
}

std::vector<std::uint8_t> makeLargeBc3Dds() {
    DirectX::ScratchImage source;
    if (FAILED(source.Initialize2D(DXGI_FORMAT_BC3_UNORM, 1024, 1024, 1, 1)))
        throw std::runtime_error("could not create BC3 fixture");
    DirectX::Blob dds;
    if (FAILED(DirectX::SaveToDDSMemory(source.GetImages(), source.GetImageCount(),
            source.GetMetadata(), DirectX::DDS_FLAGS_NONE, dds)))
        throw std::runtime_error("could not encode BC3 fixture");
    const auto* begin = static_cast<const std::uint8_t*>(dds.GetBufferPointer());
    return {begin, begin + dds.GetBufferSize()};
}

std::vector<std::uint8_t> makeMipmappedBc7Dds() {
    DirectX::ScratchImage source;
    if (FAILED(source.Initialize2D(DXGI_FORMAT_BC7_UNORM, 512, 512, 1, 4)))
        throw std::runtime_error("could not create BC7 fixture");
    for (std::size_t mip = 0; mip < 4; ++mip) {
        auto* image = source.GetImage(mip, 0, 0);
        std::memset(image->pixels, 0, image->slicePitch);
    }
    DirectX::Blob dds;
    if (FAILED(DirectX::SaveToDDSMemory(source.GetImages(), source.GetImageCount(),
            source.GetMetadata(), DirectX::DDS_FLAGS_NONE, dds)))
        throw std::runtime_error("could not encode BC7 fixture");
    const auto* begin = static_cast<const std::uint8_t*>(dds.GetBufferPointer());
    return {begin, begin + dds.GetBufferSize()};
}

std::unique_ptr<D3D11NativeThumbnailSource> makeSource(
    const fs::path& root,
    ID3D11Device* device,
    stui::textures::TextureArchiveReader archiveReader) {
    return std::make_unique<D3D11NativeThumbnailSource>(
        root, device, 12, 2min, std::move(archiveReader));
}

void looseFileWinsWithoutArchiveRead() {
    TemporaryDirectory directory;
    const auto dds = makeDds(2, 3);
    directory.write(fs::path("Pack") / "loose.dds", dds);
    const auto device = createDevice();
    std::size_t archiveReads{};
    auto source = makeSource(directory.path(), device.Get(), [&](std::string_view) {
        ++archiveReads;
        return TextureBytesResult(std::vector<std::uint8_t>{1, 2, 3});
    });

    const auto loaded = source->load("Pack/loose.dds", [] { return true; }, {});

    expect(loaded.has_value(), "expected loose DDS to upload");
    expect(archiveReads == 0, "expected loose DDS to skip archive reader");
}

void looseMissFallsBackToCanonicalArchivePath() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    std::string requestedPath;
    const auto dds = makeDds(2, 3);
    auto source = makeSource(directory.path(), device.Get(), [&](std::string_view path) {
        requestedPath = path;
        return TextureBytesResult(dds);
    });

    const auto loaded = source->load("Pack/archive.dds", [] { return true; }, {});

    expect(loaded.has_value(), "expected archive DDS to upload");
    expect(requestedPath == "textures\\actors\\character\\slavetats\\Pack\\archive.dds",
        "expected canonical archive resource path");
}

void invalidPathMapsToBroken() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    auto source = makeSource(directory.path(), device.Get(), [](std::string_view) {
        return TextureBytesResult(std::unexpected(TextureResolveError::notFound));
    });

    const auto loaded = source->load("../outside.dds", [] { return true; }, {});

    expect(!loaded && loaded.error() == NativeThumbnailFailure::broken,
        "expected invalid path to map to broken");
}

void missingSourcesMapToMissing() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    auto source = makeSource(directory.path(), device.Get(), [](std::string_view) {
        return TextureBytesResult(std::unexpected(TextureResolveError::notFound));
    });

    const auto loaded = source->load("Pack/missing.dds", [] { return true; }, {});

    expect(!loaded && loaded.error() == NativeThumbnailFailure::missing,
        "expected absent loose and archive DDS to map to missing");
}

void malformedDdsMapsToBroken() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    auto source = makeSource(directory.path(), device.Get(), [](std::string_view) {
        return TextureBytesResult(std::vector<std::uint8_t>{1, 2, 3});
    });

    const auto loaded = source->load("Pack/broken.dds", [] { return true; }, {});

    expect(!loaded && loaded.error() == NativeThumbnailFailure::broken,
        "expected malformed DDS to map to broken");
}

void uploadFailureReportsItsStageAndTexturePath() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    std::string reportedPath;
    NativeThumbnailFailureStage reportedStage{};
    auto source = std::make_unique<D3D11NativeThumbnailSource>(
        directory.path(),
        device.Get(),
        12,
        2min,
        [](std::string_view) {
            return TextureBytesResult(std::vector<std::uint8_t>{1, 2, 3});
        },
        [&](std::string_view path, NativeThumbnailFailureStage stage) {
            reportedPath = path;
            reportedStage = stage;
        });

    const auto loaded = source->load("Pack/broken.dds", [] { return true; }, {});

    expect(!loaded && loaded.error() == NativeThumbnailFailure::broken,
        "expected malformed DDS to remain broken");
    expect(reportedPath == "Pack/broken.dds",
        "expected failure report to retain the requested texture path");
    expect(reportedStage == NativeThumbnailFailureStage::upload,
        "expected malformed DDS to report the upload stage");
}

void cancellationAfterResolveAvoidsCacheEntry() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    bool current = true;
    const auto dds = makeDds(2, 3);
    auto source = makeSource(directory.path(), device.Get(), [&](std::string_view) {
        current = false;
        return TextureBytesResult(dds);
    });

    const auto loaded = source->load("Pack/cancelled.dds", [&] { return current; }, {});

    expect(!loaded && loaded.error() == NativeThumbnailFailure::cancelled,
        "expected cancellation after resolve");
    expect(!source->find("Pack/cancelled.dds", {}),
        "expected cancelled load to leave no cache entry");
}

void cacheHitReusesTextureWithoutSecondArchiveRead() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    const auto dds = makeDds(2, 3);
    std::size_t archiveReads{};
    auto source = makeSource(directory.path(), device.Get(), [&](std::string_view) {
        ++archiveReads;
        return TextureBytesResult(dds);
    });

    const auto first = source->load("Pack/cached.dds", [] { return true; }, {});
    const auto second = source->load(
        "Pack/cached.dds", [] { return true; }, stui::textures::TextureCacheTimePoint{} + 1s);

    expect(first.has_value() && second.has_value(), "expected cached loads to succeed");
    expect(*first == *second, "expected cache hit to return the same texture");
    expect(archiveReads == 1, "expected cache hit to skip a second archive read");
}

void diskCacheSkipsArchiveAfterGpuCacheIsCleared() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    const auto dds = makeLargeBc3Dds();
    std::size_t archiveReads{};
    D3D11NativeThumbnailSource source(directory.path() / "loose", device.Get(),
        2, 2min, [&](std::string_view) {
            ++archiveReads;
            return TextureBytesResult(dds);
        }, {}, directory.path() / "cache");
    const auto first = source.load("Pack/large.dds", [] { return true; }, {});
    expect(first && first->get()->width == 192, "first load should build a small thumbnail");
    source.clear();
    const auto second = source.load("Pack/large.dds", [] { return true; }, {});
    expect(second && second->get()->width == 192, "second load should use cached thumbnail");
    expect(archiveReads == 1, "disk hit should avoid reading the archive again");
    expect(source.clearDiskCache(), "clear button should remove disk cache");
    source.clear();
    const auto third = source.load("Pack/large.dds", [] { return true; }, {});
    expect(third.has_value() && archiveReads == 2,
        "load after clearing disk cache should read original DDS again");
}

void smallDdsAlsoUsesDiskCache() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    const auto dds = makeDds(64, 64);
    int archiveReads{};
    D3D11NativeThumbnailSource source(directory.path() / "loose", device.Get(),
        2, 2min, [&](std::string_view) {
            ++archiveReads;
            return TextureBytesResult(dds);
        }, {}, directory.path() / "cache");
    expect(source.load("Pack/small.dds", [] { return true; }, {}).has_value(),
        "small DDS should load from source");
    source.clear();
    expect(source.load("Pack/small.dds", [] { return true; }, {}).has_value() &&
        archiveReads == 1, "small DDS disk hit should skip archive read");
}

void diskCacheInvalidatesLooseChangesAndPreservesOtherFilesOnClear() {
    TemporaryDirectory directory;
    const auto looseRoot = directory.path() / "loose";
    directory.write(fs::path("loose") / "Pack" / "tattoo.dds", {1, 2});
    stui::textures::ThumbnailDiskCache cache(directory.path() / "cache", looseRoot);
    const std::vector<std::uint8_t> thumbnail{3, 4, 5};
    cache.store("Pack/tattoo.dds", thumbnail);
    expect(cache.bytes() == thumbnail.size(), "disk usage should include stored DDS");
    expect(cache.load("Pack/tattoo.dds") == thumbnail,
        "unchanged loose source should reuse disk thumbnail");
    const auto original = looseRoot / "Pack" / "tattoo.dds";
    fs::last_write_time(original, fs::last_write_time(original) + 1h);
    expect(!cache.load("Pack/tattoo.dds"),
        "modified loose source should invalidate disk thumbnail");
    expect(!cache.load("../outside.dds"), "traversal must not read outside cache");
    directory.write(fs::path("cache") / "keep.txt", {8});
    expect(cache.clear(), "cache clear should succeed");
    expect(cache.bytes() == 0, "disk usage should reset after clear");
    expect(fs::exists(directory.path() / "cache" / "keep.txt"),
        "clear should leave unrelated files alone");
}

void diskCacheSeparatesExternalIdentityAndInvalidatesChanges() {
    TemporaryDirectory directory;
    const auto root = directory.path() / "textures" / "actors" / "character" / "slavetats";
    directory.write(fs::path("textures") / "actors" / "character" / "slavetats" / "mark.dds", {1, 2});
    stui::textures::ThumbnailDiskCache cache(directory.path() / "cache", root);
    using stui::textures::TextureResourceKind;
    const auto catalog = stui::textures::TextureResolver::resource(TextureResourceKind::catalog, "mark.dds");
    const auto external = stui::textures::TextureResolver::resource(TextureResourceKind::external, "textures/actors/character/slavetats/mark.dds");
    expect(catalog && external, "valid identities required");
    cache.store(*catalog, std::vector<std::uint8_t>{3});
    cache.store(*external, std::vector<std::uint8_t>{4});
    expect(cache.load(*catalog) == std::vector<std::uint8_t>{3} && cache.load(*external) == std::vector<std::uint8_t>{4}, "resource kinds must not share a cache entry");
    const auto oldRevision = cache.revision(*external);
    expect(oldRevision.has_value(), "old source revision must exist");
    const auto source = root / "mark.dds";
    fs::last_write_time(source, fs::last_write_time(source) + 1h);
    cache.store(*external, std::vector<std::uint8_t>{7}, *oldRevision);
    expect(!cache.load(*external), "changed external file must invalidate its thumbnail");
    fs::remove(source);
    expect(!cache.load(*external), "missing loose file must not reuse the loose thumbnail");
}
void externalSourceInvalidatesGpuAndFallsBackToArchive() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    const auto dds = makeDds(64, 64);
    const auto relative = fs::path("textures") / "other" / "mark.dds";
    directory.write(relative, dds);
    const auto root = directory.path() / "textures" / "actors" / "character" / "slavetats";
    int archiveReads = 0;
    D3D11NativeThumbnailSource source(root, device.Get(), 4, 2min, [&](std::string_view) {
        ++archiveReads;
        return TextureBytesResult(dds);
    }, {}, directory.path() / "cache");
    const auto identity = stui::textures::TextureResolver::resource(stui::core::TextureResourceKind::external, "textures/other/mark.dds");
    expect(identity.has_value(), "valid identity");
    const auto first = source.load(*identity, [] { return true; }, {});
    expect(first && archiveReads == 0, "loose file should win");
    const auto path = directory.path() / relative;
    fs::last_write_time(path, fs::last_write_time(path) + 1h);
    const auto second = source.load(*identity, [] { return true; }, {});
    expect(second && *first != *second, "changed loose source must invalidate GPU entry");
    fs::remove(path);
    const auto third = source.load(*identity, [] { return true; }, {});
    expect(third && archiveReads == 1, "missing loose source must fall back to archive");
}

void externalSourceFallsBackForEscapingSymlink() {
    TemporaryDirectory directory;
    TemporaryDirectory outside;
    const auto device = createDevice();
    const auto dds = makeDds(64, 64);
    outside.write("mark.dds", dds);
    fs::create_directories(directory.path() / "textures" / "other");
    std::error_code error;
    fs::create_symlink(outside.path() / "mark.dds", directory.path() / "textures" / "other" / "mark.dds", error);
    if (error) { std::cout << "SKIP external source symlink unavailable: " << error.message() << '\n'; return; }
    const auto root = directory.path() / "textures" / "actors" / "character" / "slavetats";
    int archiveReads = 0;
    D3D11NativeThumbnailSource source(root, device.Get(), 4, 2min, [&](std::string_view) {
        ++archiveReads;
        return TextureBytesResult(dds);
    }, {}, directory.path() / "cache");
    const auto identity = stui::textures::TextureResolver::resource(stui::core::TextureResourceKind::external, "textures/other/mark.dds");
    expect(identity && source.load(*identity, [] { return true; }, {}) && archiveReads == 1,
        "escaping symlink must use archive fallback");
}
void loadsExternalIdentityThroughNativeSource() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    const auto dds = makeDds(64, 64);
    std::vector<std::string> reads;
    const auto root = directory.path() / "textures" / "actors" / "character" / "slavetats";
    D3D11NativeThumbnailSource source(root, device.Get(), 4, 2min, [&](std::string_view path) {
        reads.emplace_back(path);
        return TextureBytesResult(dds);
    }, {}, directory.path() / "cache");
    using stui::textures::TextureResourceKind;
    const auto identity = stui::textures::TextureResolver::resource(TextureResourceKind::external, "textures/other/mark.dds");
    expect(identity && source.load(*identity, [] { return true; }, {}).has_value(), "external resource must load");
    expect(reads == std::vector<std::string>{"textures\\other\\mark.dds"}, "external request must use its Data path");
    source.clear();
    expect(source.load(*identity, [] { return true; }, {}).has_value() && reads.size() == 1, "disk cache must reuse external resource");
}
void prebuildWritesDiskWithoutUploadingGpuTexture() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    const auto dds = makeLargeBc3Dds();
    std::size_t archiveReads{};
    D3D11NativeThumbnailSource source(directory.path() / "loose", device.Get(),
        12, 2min, [&](std::string_view) {
            ++archiveReads;
            return TextureBytesResult(dds);
        }, {}, directory.path() / "cache");
    expect(source.prepareDiskCache("Pack/large.dds"),
        "prebuild should create a cache entry");
    expect(source.diskCacheBytes() > 0, "prebuild should report disk usage");
    expect(!source.find("Pack/large.dds", {}),
        "prebuild should not upload a GPU texture");
    expect(source.prepareDiskCache("Pack/large.dds") && archiveReads == 1,
        "second prebuild should reuse disk entry");
}

void prebuildRepairsCorruptDiskEntry() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    const auto dds = makeLargeBc3Dds();
    std::size_t archiveReads{};
    stui::textures::ThumbnailDiskCache cache(directory.path() / "cache",
        directory.path() / "loose");
    cache.store("Pack/large.dds", std::vector<std::uint8_t>{1, 2, 3, 4});
    D3D11NativeThumbnailSource source(directory.path() / "loose", device.Get(),
        12, 2min, [&](std::string_view) {
            ++archiveReads;
            return TextureBytesResult(dds);
        }, {}, directory.path() / "cache");
    expect(source.prepareDiskCache("Pack/large.dds"),
        "prebuild should replace a corrupt cache entry");
    expect(archiveReads == 1, "repair should read the source DDS once");
    expect(source.load("Pack/large.dds", [] { return true; }, {}).has_value() &&
        archiveReads == 1, "repaired entry should load without another archive read");
}

void reportsDiskLookupAndUploadTiming() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    const auto dds = makeLargeBc3Dds();
    std::vector<stui::native::NativeThumbnailTimingStage> stages;
    D3D11NativeThumbnailSource source(directory.path() / "loose", device.Get(),
        12, 2min, [&](std::string_view) { return TextureBytesResult(dds); },
        {}, directory.path() / "cache",
        [&](std::string_view path, stui::native::NativeThumbnailTimingStage stage,
            double elapsedMs) {
            expect(path == "Pack/timed.dds" && elapsedMs >= 0.0,
                "timing should identify a nonnegative phase duration");
            stages.push_back(stage);
        });
    expect(source.prepareDiskCache("Pack/timed.dds"), "timing fixture should prebuild");
    expect(source.load("Pack/timed.dds", [] { return true; }, {}).has_value(),
        "cached thumbnail should upload");
    expect(stages == std::vector<stui::native::NativeThumbnailTimingStage>{
            stui::native::NativeThumbnailTimingStage::diskLookup,
            stui::native::NativeThumbnailTimingStage::upload},
        "cache hit should report disk read and GPU upload separately");
}

void diskLookupSerializesLimitPruning() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    const auto dds = makeDds(2, 3);
    std::promise<void> started;
    std::promise<bool> completed;
    auto result = completed.get_future();
    std::jthread limiter;
    bool serialized = false;
    D3D11NativeThumbnailSource* sourcePointer = nullptr;
    D3D11NativeThumbnailSource source(directory.path() / "loose", device.Get(),
        12, 2min, [&](std::string_view) { return TextureBytesResult(dds); },
        {}, directory.path() / "cache",
        [&](std::string_view, stui::native::NativeThumbnailTimingStage stage, double) {
            if (stage != stui::native::NativeThumbnailTimingStage::diskLookup) return;
            limiter = std::jthread([&] {
                started.set_value();
                completed.set_value(sourcePointer->setDiskCacheLimit(128ULL * 1024 * 1024));
            });
            started.get_future().wait();
            serialized = result.wait_for(100ms) == std::future_status::timeout;
        });
    sourcePointer = &source;
    expect(source.prepareDiskCache("Pack/serialized.dds"), "disk fixture should prebuild");
    const auto loaded = source.load("Pack/serialized.dds", [] { return true; }, {});
    limiter.join();
    expect(loaded.has_value() && result.get(), "load and deferred prune should both succeed");
    expect(serialized, "disk lookup and prune must hold the same source mutex");
    expect(source.find("Pack/serialized.dds", {}) == *loaded,
        "disk limit changes must preserve ready GPU textures");
}

void prebuiltBc7LoadsSmallTextureWithoutSecondArchiveRead() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    const auto dds = makeMipmappedBc7Dds();
    std::size_t archiveReads{};
    D3D11NativeThumbnailSource source(directory.path() / "loose", device.Get(),
        12, 2min, [&](std::string_view) {
            ++archiveReads;
            return TextureBytesResult(dds);
        }, {}, directory.path() / "cache");
    expect(source.prepareDiskCache("Pack/bc7.dds"),
        "BC7 should prebuild into disk cache");
    const auto loaded = source.load("Pack/bc7.dds", [] { return true; }, {});
    expect(loaded && (*loaded)->width == 192 && (*loaded)->height == 192,
        "BC7 disk hit should upload only a 192px texture");
    expect(archiveReads == 1,
        "BC7 disk hit should skip rereading the original texture");
}

void diskCacheEvictsOldestFilesAtByteBudget() {
    TemporaryDirectory directory;
    stui::textures::ThumbnailDiskCache cache(
        directory.path() / "cache", directory.path() / "loose", 8);
    const std::vector<std::uint8_t> bytes{1, 2, 3, 4};
    cache.store("a.dds", bytes);
    for (const auto& entry : fs::directory_iterator(directory.path() / "cache")) {
        fs::last_write_time(entry.path(), fs::file_time_type::clock::now() - 1h);
    }
    cache.store("b.dds", bytes);
    cache.store("c.dds", bytes);
    expect(!cache.load("a.dds"), "disk budget should evict oldest thumbnail");
    expect(cache.load("b.dds") == bytes && cache.load("c.dds") == bytes,
        "disk budget should retain two recent thumbnails");
}

void diskCacheShrinkPrunesImmediatelyAndReportsFailures() {
    TemporaryDirectory directory;
    const auto cacheDirectory = directory.path() / "cache";
    stui::textures::ThumbnailDiskCache cache(cacheDirectory, directory.path() / "loose", 16);
    const std::vector<std::uint8_t> bytes{1, 2, 3, 4};
    cache.store("a.dds", bytes);
    for (const auto& entry : fs::directory_iterator(cacheDirectory))
        fs::last_write_time(entry.path(), fs::file_time_type::clock::now() - 1h);
    cache.store("b.dds", bytes);
    cache.store("c.dds", bytes);
    { std::ofstream output(cacheDirectory / "keep.txt"); output << "keep"; }
    expect(cache.setMaxBytes(8), "shrinking should prune immediately");
    expect(cache.bytes() == 8 && !cache.load("a.dds") && cache.load("b.dds") == bytes &&
        cache.load("c.dds") == bytes, "shrink must evict oldest entry and update usage");
    expect(fs::exists(cacheDirectory / "keep.txt"), "shrink must preserve unrelated files");
    expect(cache.setMaxBytes(16) && cache.bytes() == 8, "increase retains entries");
    expect(!cache.setMaxBytes(0), "zero budget should be rejected");
    const auto notDirectory = directory.path() / "file";
    { std::ofstream output(notDirectory); output << "blocked"; }
    stui::textures::ThumbnailDiskCache broken(notDirectory, directory.path(), 16);
    expect(!broken.setMaxBytes(8), "filesystem failures must be reported");
    stui::textures::ThumbnailDiskCache missing(directory.path() / "missing", directory.path());
    expect(missing.setMaxBytes(128), "absent empty cache should accept a limit");
}

void clearRemovesCacheEntries() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    const auto dds = makeDds(2, 3);
    auto source = makeSource(directory.path(), device.Get(), [&](std::string_view) {
        return TextureBytesResult(dds);
    });

    const auto loaded = source->load("Pack/clear.dds", [] { return true; }, {});
    expect(loaded.has_value(), "expected cache fixture load");

    source->clear();

    expect(!source->find("Pack/clear.dds", {}), "expected clear to remove cache entry");
}

void nullDeviceConstructsUnavailablePlaceholderSourceWithoutArchiveRead() {
    TemporaryDirectory directory;
    bool archiveRead{};
    auto source = makeSource(directory.path(), nullptr, [&](std::string_view) {
        archiveRead = true;
        return TextureBytesResult(std::vector<std::uint8_t>{1, 2, 3});
    });

    expect(!source->available(), "expected null device source to remain unavailable");
    const auto loaded = source->load("Pack/unavailable.dds", [] { return true; }, {});
    expect(!loaded && loaded.error() == NativeThumbnailFailure::deviceUnavailable,
        "expected unavailable source load failure without a crash");
    source->pruneExpired({});
    source->clear();
    expect(!source->find("Pack/unavailable.dds", {}),
        "expected unavailable source to retain no cached texture");
    expect(!archiveRead, "expected unavailable source to skip archive reads");
}

void transientArchiveDelayRemainsRetryable() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    const auto dds = makeDds(2, 3);
    bool delayed = true;
    stui::textures::ScheduledTextureArchiveReader reader(
        [&](auto task) { if (!delayed) task(); },
        [&](auto) { return TextureBytesResult(dds); }, 5ms);
    auto source = makeSource(directory.path(), device.Get(), [&](auto path) { return reader.read(path); });
    const auto first = source->load("delayed.dds", [] { return true; }, {});
    expect(!first && first.error() == NativeThumbnailFailure::cancelled,
        "scheduler delay must be retryable, not negative-cached as broken DDS");
    delayed = false;
    expect(source->load("delayed.dds", [] { return true; }, {}).has_value(), "retry must load the valid texture");
}

void renderLookupDoesNotWaitForDiskWork() {
    TemporaryDirectory directory;
    const auto device = createDevice();
    const auto dds = makeDds(2, 3);
    std::promise<void> entered, release;
    auto ready = entered.get_future();
    auto gate = release.get_future().share();
    auto source = std::make_unique<D3D11NativeThumbnailSource>(
        directory.path(), device.Get(), 12, 2min,
        [&](std::string_view) { return TextureBytesResult(dds); },
        stui::native::NativeThumbnailFailureObserver{}, directory.path() / "cache",
        [&](auto, auto stage, double) {
            if (stage == stui::native::NativeThumbnailTimingStage::diskLookup) {
                entered.set_value(); gate.wait();
            }
        });
    auto loading = std::async(std::launch::async, [&] { return source->load("busy.dds", [] { return true; }, {}); });
    ready.wait();
    auto lookup = std::async(std::launch::async, [&] { return source->find("other.dds", {}); });
    const bool nonblocking = lookup.wait_for(100ms) == std::future_status::ready;
    release.set_value();
    lookup.get();
    expect(loading.get().has_value(), "background fixture must load");
    expect(nonblocking, "render lookup must not wait for disk/decode work");
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
    failures += run("transient archive delay is retryable", transientArchiveDelayRemainsRetryable);
    failures += run("render lookup does not wait for disk work", renderLookupDoesNotWaitForDiskWork);
    failures += run("disk lookup serializes limit pruning", diskLookupSerializesLimitPruning);
    failures += run("loose file wins without archive read", looseFileWinsWithoutArchiveRead);
    failures += run("loose miss falls back to canonical archive path", looseMissFallsBackToCanonicalArchivePath);
    failures += run("invalid path maps to broken", invalidPathMapsToBroken);
    failures += run("missing sources map to missing", missingSourcesMapToMissing);
    failures += run("malformed DDS maps to broken", malformedDdsMapsToBroken);
    failures += run(
        "upload failure reports stage and texture path",
        uploadFailureReportsItsStageAndTexturePath);
    failures += run("cancellation after resolve avoids cache entry", cancellationAfterResolveAvoidsCacheEntry);
    failures += run("cache hit reuses texture without second archive read", cacheHitReusesTextureWithoutSecondArchiveRead);
    failures += run("disk thumbnail survives GPU eviction and can be cleared",
        diskCacheSkipsArchiveAfterGpuCacheIsCleared);
    failures += run("small DDS also reuses disk cache", smallDdsAlsoUsesDiskCache);
    failures += run("disk cache validates source and clears only cache files",
        diskCacheInvalidatesLooseChangesAndPreservesOtherFilesOnClear);
    failures += run("external disk identity and invalidation", diskCacheSeparatesExternalIdentityAndInvalidatesChanges);
    failures += run("external source invalidates GPU and falls back", externalSourceInvalidatesGpuAndFallsBackToArchive);
    failures += run("external source rejects symlink and falls back", externalSourceFallsBackForEscapingSymlink);
    failures += run("external identity loads through native source", loadsExternalIdentityThroughNativeSource);
    failures += run("prebuild writes disk without GPU upload",
        prebuildWritesDiskWithoutUploadingGpuTexture);
    failures += run("prebuild repairs corrupt disk entry",
        prebuildRepairsCorruptDiskEntry);
    failures += run("reports disk lookup and upload timing",
        reportsDiskLookupAndUploadTiming);
    failures += run("prebuilt BC7 loads small texture",
        prebuiltBc7LoadsSmallTextureWithoutSecondArchiveRead);
    failures += run("disk cache evicts oldest files at byte budget",
        diskCacheEvictsOldestFilesAtByteBudget);
    failures += run("disk cache shrinks safely", diskCacheShrinkPrunesImmediatelyAndReportsFailures);
    failures += run("clear removes cache entries", clearRemovesCacheEntries);
    failures += run(
        "null device constructs unavailable placeholder source without archive read",
        nullDeviceConstructsUnavailablePlaceholderSourceWithoutArchiveRead);
    return failures == 0 ? 0 : 1;
}
