#include "native/NativeThumbnailController.h"

#include "repository/TattooCatalogLoader.h"

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <expected>
#include <iostream>
#include <memory>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using stui::native::NativeThumbnailCacheLookup;
using stui::native::NativeThumbnailController;
using stui::native::NativeThumbnailFailure;
using stui::native::NativeThumbnailEpoch;
using stui::native::NativeThumbnailStatus;
using stui::repository::TattooCatalog;
using stui::repository::TattooCatalogSnapshot;
using stui::repository::TattooDefinition;

TattooDefinition tattoo(std::string texturePath, std::size_t sourceIndex) {
    return TattooDefinition{
        .sourceId = "fixture.json",
        .sourceFile = "fixture.json",
        .packName = "Fixture Pack",
        .sourceIndex = sourceIndex,
        .name = "Entry " + std::to_string(sourceIndex),
        .section = "Marks",
        .texturePath = std::move(texturePath),
        .area = "Body",
    };
}

TattooCatalogSnapshot snapshot(std::vector<TattooDefinition> definitions) {
    return std::make_shared<const TattooCatalog>(TattooCatalog{
        .repository = stui::repository::TattooRepository(std::move(definitions)),
        .sourceCount = 1,
    });
}

std::vector<TattooDefinition> page(std::string_view prefix) {
    std::vector<TattooDefinition> entries;
    entries.reserve(6);
    for (std::size_t index = 0; index < 6; ++index) {
        entries.push_back(tattoo(
            std::string(prefix) + std::to_string(index) + ".dds", index));
    }
    return entries;
}

std::shared_ptr<stui::textures::D3D11Texture> readyTexture() {
    return std::make_shared<stui::textures::D3D11Texture>(
        stui::textures::D3D11Texture{.width = 64, .height = 64});
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

const NativeThumbnailCacheLookup noCacheHit = [](std::string_view) {
    return std::shared_ptr<stui::textures::D3D11Texture>{};
};

void acceptsPresentationNeutralVisiblePaths() {
    NativeThumbnailController controller;
    const NativeThumbnailEpoch epoch = std::make_shared<int>(1);
    const std::vector<std::string> paths{"slot/a.dds", "slot/b.dds"};

    controller.synchronize(epoch, paths, noCacheHit);

    const auto views = controller.views();
    expect(views.size() == 2 && views[0].texturePath == "slot/a.dds" &&
            views[1].texturePath == "slot/b.dds",
        "expected slot texture paths accepted without repository entries");
}

void acceptsMultipleVisibleGalleryRows() {
    NativeThumbnailController controller;
    const NativeThumbnailEpoch epoch = std::make_shared<int>(1);
    std::vector<std::string> paths;
    for (std::size_t index = 0; index < 18; ++index) {
        paths.push_back("gallery/" + std::to_string(index) + ".dds");
    }
    controller.synchronize(epoch, paths, noCacheHit);
    expect(controller.views().size() == 18,
        "expected gallery thumbnail window to include multiple rows");

    for (std::size_t index = 18; index < 100; ++index) {
        paths.push_back("gallery/" + std::to_string(index) + ".dds");
    }
    controller.synchronize(epoch, paths, noCacheHit);
    expect(controller.views().size() == 96,
        "expected gallery thumbnail work to remain bounded");
}

void hidesStaleCompletionAfterPageTransition() {
    auto firstPage = page("first-");
    auto secondPage = page("second-");
    auto snapshotA = snapshot(firstPage);
    NativeThumbnailController controller;

    controller.synchronize(snapshotA, firstPage, noCacheHit);
    auto first = controller.takeNextRequest();
    expect(first && first->texturePath == firstPage[0].texturePath,
        "expected first current-page request");
    expect(!controller.takeNextRequest(), "expected only one in-flight request");

    controller.synchronize(snapshotA, secondPage, noCacheHit);
    controller.complete(*first, readyTexture());
    expect(std::ranges::none_of(controller.views(), [&firstPage](const auto& view) {
        return view.texturePath == firstPage[0].texturePath;
    }), "expected stale completion hidden from new page");
}

void limitsViewsToSixUniquePathsInSourceOrder() {
    auto entries = page("path-");
    entries.insert(entries.begin() + 1, tattoo("path-0.dds", 6));
    entries.push_back(tattoo("seventh.dds", 7));
    auto snapshotA = snapshot(entries);
    NativeThumbnailController controller;

    controller.synchronize(snapshotA, entries, noCacheHit);
    const auto views = controller.views();
    expect(views.size() == 6, "expected at most six unique paths");
    expect(views[0].texturePath == "path-0.dds" && views[1].texturePath == "path-1.dds" &&
            views[5].texturePath == "path-5.dds",
        "expected source-order de-duplication before the six-path limit");
}

void keepsInFlightRequestCurrentForSamePage() {
    auto entries = page("same-");
    auto snapshotA = snapshot(entries);
    NativeThumbnailController controller;

    controller.synchronize(snapshotA, entries, noCacheHit);
    const auto request = controller.takeNextRequest();
    expect(request.has_value(), "expected initial request");
    controller.synchronize(snapshotA, entries, noCacheHit);
    expect(controller.isCurrent(*request), "expected same page to retain request generation");
    expect(!controller.takeNextRequest(), "expected same page not to replace in-flight request");
}

void usesCacheHitWithoutQueuingRequest() {
    auto entries = page("cached-");
    auto snapshotA = snapshot(entries);
    auto texture = readyTexture();
    NativeThumbnailController controller;

    controller.synchronize(snapshotA, entries, [&texture](std::string_view path) {
        return path == "cached-0.dds" ? texture : std::shared_ptr<stui::textures::D3D11Texture>{};
    });
    const auto views = controller.views();
    expect(views[0].status == NativeThumbnailStatus::ready && views[0].texture == texture,
        "expected cache hit ready state");
    const auto request = controller.takeNextRequest();
    expect(request && request->texturePath == "cached-1.dds",
        "expected cache hit path omitted from request queue");
}

void retainsMissingAndBrokenForSnapshotRevisits() {
    auto missingPage = page("missing-");
    auto brokenPage = page("broken-");
    auto snapshotA = snapshot(missingPage);
    NativeThumbnailController controller;

    controller.synchronize(snapshotA, missingPage, noCacheHit);
    const auto missing = controller.takeNextRequest();
    controller.complete(*missing, std::unexpected(NativeThumbnailFailure::missing));
    controller.synchronize(snapshotA, brokenPage, noCacheHit);
    controller.synchronize(snapshotA, missingPage, noCacheHit);
    expect(controller.views()[0].status == NativeThumbnailStatus::missing,
        "expected missing result to remain negative-cached for snapshot");
    const auto afterMissing = controller.takeNextRequest();
    expect(afterMissing && afterMissing->texturePath != missingPage[0].texturePath,
        "expected missing path not to be requeued");
    controller.complete(*afterMissing, readyTexture());

    controller.clear();
    controller.synchronize(snapshotA, brokenPage, noCacheHit);
    const auto broken = controller.takeNextRequest();
    controller.complete(*broken, std::unexpected(NativeThumbnailFailure::broken));
    controller.synchronize(snapshotA, missingPage, noCacheHit);
    controller.synchronize(snapshotA, brokenPage, noCacheHit);
    expect(controller.views()[0].status == NativeThumbnailStatus::broken,
        "expected broken result to remain negative-cached for snapshot");
}

void retriesDeviceUnavailableOnlyWhenAsked() {
    auto entries = page("device-");
    auto snapshotA = snapshot(entries);
    NativeThumbnailController controller;

    controller.synchronize(snapshotA, entries, noCacheHit);
    const auto request = controller.takeNextRequest();
    controller.complete(*request, std::unexpected(NativeThumbnailFailure::deviceUnavailable));
    expect(controller.views()[0].status == NativeThumbnailStatus::placeholder,
        "expected unavailable device to return to placeholder");
    for (std::size_t index = 1; index < entries.size(); ++index) {
        const auto next = controller.takeNextRequest();
        expect(next && next->texturePath == entries[index].texturePath,
            "expected remaining current-page path before unavailable retry");
        controller.complete(*next, readyTexture());
    }
    expect(!controller.takeNextRequest(), "expected no automatic device-unavailable retry");
    controller.retryDeviceUnavailable();
    const auto retry = controller.takeNextRequest();
    expect(retry && retry->texturePath == entries[0].texturePath,
        "expected retry after explicit device-unavailable retry");
}

void resetsNegativeCacheForSnapshotIdentityReplacement() {
    auto entries = page("replacement-");
    auto snapshotA = snapshot(entries);
    auto snapshotB = snapshot(entries);
    NativeThumbnailController controller;

    controller.synchronize(snapshotA, entries, noCacheHit);
    const auto first = controller.takeNextRequest();
    controller.complete(*first, std::unexpected(NativeThumbnailFailure::missing));
    controller.synchronize(snapshotB, entries, noCacheHit);
    expect(controller.views()[0].status == NativeThumbnailStatus::placeholder,
        "expected replacement snapshot to discard negative cache");
    const auto replacement = controller.takeNextRequest();
    expect(replacement && replacement->texturePath == entries[0].texturePath,
        "expected replacement snapshot to request previously negative path");
}

void preservesInFlightRequestAcrossClear() {
    auto firstPage = page("before-clear-");
    auto secondPage = page("after-clear-");
    auto snapshotA = snapshot(firstPage);
    NativeThumbnailController controller;

    controller.synchronize(snapshotA, firstPage, noCacheHit);
    const auto first = controller.takeNextRequest();
    expect(first.has_value(), "expected initial request before clear");
    controller.clear();
    controller.synchronize(snapshotA, secondPage, noCacheHit);
    expect(!controller.takeNextRequest(),
        "expected clear to retain ownership of the running request");
    controller.complete(*first, readyTexture());
    const auto second = controller.takeNextRequest();
    expect(second && second->texturePath == secondPage[0].texturePath,
        "expected queued current page request after old completion");
}

void retriesOnlyDeviceUnavailablePathsStillVisible() {
    std::vector<TattooDefinition> oldPage{tattoo("old-device.dds", 0)};
    std::vector<TattooDefinition> newPage{tattoo("new-page.dds", 1)};
    auto snapshotA = snapshot(oldPage);
    NativeThumbnailController controller;

    controller.synchronize(snapshotA, oldPage, noCacheHit);
    const auto oldRequest = controller.takeNextRequest();
    controller.complete(*oldRequest, std::unexpected(NativeThumbnailFailure::deviceUnavailable));
    controller.synchronize(snapshotA, newPage, noCacheHit);
    const auto currentRequest = controller.takeNextRequest();
    expect(currentRequest && currentRequest->texturePath == "new-page.dds",
        "expected only new page work before retry");
    controller.complete(*currentRequest, readyTexture());
    controller.retryDeviceUnavailable();
    expect(!controller.takeNextRequest(),
        "expected retry not to queue device-unavailable path from old page");
}

void requeuesSharedPathAfterStaleCompletion() {
    std::vector<TattooDefinition> firstPage{
        tattoo("shared.dds", 0),
        tattoo("first-page.dds", 1),
    };
    std::vector<TattooDefinition> secondPage{
        tattoo("shared.dds", 2),
        tattoo("second-page.dds", 3),
    };
    auto snapshotA = snapshot(firstPage);
    NativeThumbnailController controller;

    controller.synchronize(snapshotA, firstPage, noCacheHit);
    const auto first = controller.takeNextRequest();
    controller.synchronize(snapshotA, secondPage, noCacheHit);
    expect(!controller.takeNextRequest(), "expected old request to remain the single in-flight task");
    controller.complete(*first, readyTexture());
    const auto secondPageRequest = controller.takeNextRequest();
    expect(secondPageRequest && secondPageRequest->texturePath == "second-page.dds",
        "expected already queued second-page path before shared retry");
    controller.complete(*secondPageRequest, readyTexture());
    const auto current = controller.takeNextRequest();
    expect(current && current->generation != first->generation &&
            current->texturePath == "shared.dds",
        "expected shared path to requeue for current generation after stale completion");
}

void newerSynchronizationWinsWhenOlderLookupCompletesLast() {
    std::vector<TattooDefinition> olderPage{tattoo("older.dds", 0)};
    std::vector<TattooDefinition> newerPage{tattoo("newer.dds", 1)};
    auto snapshotA = snapshot(olderPage);
    NativeThumbnailController controller;
    std::mutex lookupMutex;
    std::condition_variable lookupCondition;
    bool olderLookupStarted = false;
    bool releaseOlderLookup = false;
    auto olderTexture = readyTexture();
    auto newerTexture = readyTexture();

    NativeThumbnailCacheLookup lookup = [&](std::string_view path) {
        if (path == "older.dds") {
            std::unique_lock lock(lookupMutex);
            olderLookupStarted = true;
            lookupCondition.notify_one();
            lookupCondition.wait(lock, [&] { return releaseOlderLookup; });
            return olderTexture;
        }
        return newerTexture;
    };

    std::thread older([&] { controller.synchronize(snapshotA, olderPage, lookup); });
    {
        std::unique_lock lock(lookupMutex);
        lookupCondition.wait(lock, [&] { return olderLookupStarted; });
    }
    controller.synchronize(snapshotA, newerPage, lookup);
    {
        std::scoped_lock lock(lookupMutex);
        releaseOlderLookup = true;
    }
    lookupCondition.notify_one();
    older.join();

    const auto views = controller.views();
    expect(views.size() == 1 && views[0].texturePath == "newer.dds" &&
            views[0].status == NativeThumbnailStatus::ready && views[0].texture == newerTexture,
        "expected later synchronization to survive older delayed lookup");
}

void canonicalizesSlashAndCaseVariants() {
    std::vector<TattooDefinition> entries{
        tattoo("Textures\\Marks\\ALPHA.DDS", 0),
        tattoo("textures/marks/alpha.dds", 1),
    };
    auto snapshotA = snapshot(entries);
    NativeThumbnailController controller;

    controller.synchronize(snapshotA, entries, noCacheHit);
    const auto views = controller.views();
    expect(views.size() == 1 && views[0].texturePath == "textures/marks/alpha.dds",
        "expected slash and ASCII-case variants to de-duplicate canonically");
    const auto request = controller.takeNextRequest();
    expect(request && request->texturePath == "textures/marks/alpha.dds",
        "expected canonical request path");
}

void doesNotLookupAgainForSamePage() {
    auto entries = page("same-lookup-");
    auto snapshotA = snapshot(entries);
    NativeThumbnailController controller;
    std::size_t lookupCount = 0;
    NativeThumbnailCacheLookup lookup = [&lookupCount](std::string_view) {
        ++lookupCount;
        return std::shared_ptr<stui::textures::D3D11Texture>{};
    };

    controller.synchronize(snapshotA, entries, lookup);
    controller.synchronize(snapshotA, entries, lookup);
    expect(lookupCount == 6, "expected same page synchronize not to invoke lookup again");
}

void publishedPageInvalidatesDifferentPendingSynchronization() {
    std::vector<TattooDefinition> pageA{tattoo("page-a.dds", 0)};
    std::vector<TattooDefinition> pageB{tattoo("page-b.dds", 1)};
    auto snapshotA = snapshot(pageA);
    NativeThumbnailController controller;
    std::mutex lookupMutex;
    std::condition_variable lookupCondition;
    bool pageBLookupStarted = false;
    bool releasePageBLookup = false;
    auto textureA = readyTexture();
    auto textureB = readyTexture();

    NativeThumbnailCacheLookup lookup = [&](std::string_view path) {
        if (path == "page-b.dds") {
            std::unique_lock lock(lookupMutex);
            pageBLookupStarted = true;
            lookupCondition.notify_one();
            lookupCondition.wait(lock, [&] { return releasePageBLookup; });
            return textureB;
        }
        return textureA;
    };

    controller.synchronize(snapshotA, pageA, lookup);
    std::thread pendingPageB([&] { controller.synchronize(snapshotA, pageB, lookup); });
    {
        std::unique_lock lock(lookupMutex);
        lookupCondition.wait(lock, [&] { return pageBLookupStarted; });
    }
    controller.synchronize(snapshotA, pageA, lookup);
    {
        std::scoped_lock lock(lookupMutex);
        releasePageBLookup = true;
    }
    lookupCondition.notify_one();
    pendingPageB.join();

    const auto views = controller.views();
    expect(views.size() == 1 && views[0].texturePath == "page-a.dds" &&
            views[0].status == NativeThumbnailStatus::ready && views[0].texture == textureA,
        "expected current published page to invalidate pending different page");
}

void clearsPendingSynchronizationAfterLookupThrows() {
    std::vector<TattooDefinition> entries{tattoo("throwing.dds", 0)};
    auto snapshotA = snapshot(entries);
    NativeThumbnailController controller;
    bool shouldThrow = true;
    std::size_t lookupCount = 0;
    NativeThumbnailCacheLookup lookup = [&](std::string_view) {
        ++lookupCount;
        if (shouldThrow) {
            throw std::runtime_error("expected lookup failure");
        }
        return std::shared_ptr<stui::textures::D3D11Texture>{};
    };

    bool threw = false;
    try {
        controller.synchronize(snapshotA, entries, lookup);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "expected lookup exception to propagate");

    shouldThrow = false;
    controller.synchronize(snapshotA, entries, lookup);
    expect(lookupCount == 2, "expected retry to invoke lookup after exception cleanup");
    const auto request = controller.takeNextRequest();
    expect(request && request->texturePath == "throwing.dds",
        "expected retry to publish request after exception cleanup");
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
    failures += run("accepts presentation-neutral visible paths", acceptsPresentationNeutralVisiblePaths);
    failures += run("accepts multiple visible gallery rows", acceptsMultipleVisibleGalleryRows);
    failures += run("hides stale completion after page transition", hidesStaleCompletionAfterPageTransition);
    failures += run("limits views to six unique paths in source order", limitsViewsToSixUniquePathsInSourceOrder);
    failures += run("keeps in-flight request current for same page", keepsInFlightRequestCurrentForSamePage);
    failures += run("uses cache hit without queuing request", usesCacheHitWithoutQueuingRequest);
    failures += run("retains missing and broken for snapshot revisits", retainsMissingAndBrokenForSnapshotRevisits);
    failures += run("retries device unavailable only when asked", retriesDeviceUnavailableOnlyWhenAsked);
    failures += run("resets negative cache for snapshot identity replacement", resetsNegativeCacheForSnapshotIdentityReplacement);
    failures += run("preserves in-flight request across clear", preservesInFlightRequestAcrossClear);
    failures += run("retries only device-unavailable paths still visible", retriesOnlyDeviceUnavailablePathsStillVisible);
    failures += run("requeues shared path after stale completion", requeuesSharedPathAfterStaleCompletion);
    failures += run("newer synchronization wins when older lookup completes last", newerSynchronizationWinsWhenOlderLookupCompletesLast);
    failures += run("canonicalizes slash and case variants", canonicalizesSlashAndCaseVariants);
    failures += run("does not lookup again for same page", doesNotLookupAgainForSamePage);
    failures += run("published page invalidates different pending synchronization", publishedPageInvalidatesDifferentPendingSynchronization);
    failures += run("clears pending synchronization after lookup throws", clearsPendingSynchronizationAfterLookupThrows);
    return failures == 0 ? 0 : 1;
}
