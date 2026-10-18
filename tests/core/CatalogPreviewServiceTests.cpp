#include "core/SlaveTatsService.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace stui::core;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

class FakeRuntime final : public ITattooRuntime {
public:
    bool apiAvailable() const noexcept override { return true; }
    bool jContainersReady() const noexcept override { return true; }
    TattooQueryResult queryAvailable(std::string_view) override { return std::vector<TattooEntry>{}; }
    TattooSlotsResult querySlots(std::uint32_t, TattooArea) override { return TattooSlots{}; }
    ApplyTattooResult applyToSlot(const ApplyTattooRequest&) override {
        return std::unexpected(ServiceError{ServiceErrorCode::applyFailed, "unused"});
    }
    RemoveTattooResult removeFromSlot(const RemoveTattooRequest&) override {
        return std::unexpected(ServiceError{ServiceErrorCode::removeFailed, "unused"});
    }
    UpdateTattooAppearanceResult updateAppearance(const UpdateTattooAppearanceRequest&) override {
        return std::unexpected(ServiceError{ServiceErrorCode::updateFailed, "unused"});
    }
    SetTattooLockedResult setTattooLocked(const SetTattooLockedRequest&) override {
        return std::unexpected(ServiceError{ServiceErrorCode::lockFailed, "unused"});
    }
    CatalogPreviewBeginResult beginCatalogPreview(const CatalogPreviewBeginRequest& request) override {
        ++beginCount;
        beginRequest = request;
        return CatalogPreviewBeginSuccess{.sessionId = 77};
    }
    CatalogPreviewPaintResult paintCatalogPreview(const CatalogPreviewPaintRequest& request) override {
        ++paintCount;
        paintRequest = request;
        return CatalogPreviewPaintSuccess{};
    }
    CatalogPreviewRestoreResult restoreCatalogPreview(
        const CatalogPreviewRestoreRequest& request) override {
        ++restoreCount;
        restoreRequest = request;
        return CatalogPreviewRestoreSuccess{};
    }
    CatalogPreviewCommitResult commitCatalogPreview(
        const CatalogPreviewCommitRequest& request) override {
        ++commitCount;
        commitRequest = request;
        return ApplyTattooSuccess{request.apply.actorFormId, request.apply.area,
            request.apply.slot, request.apply.section, request.apply.name};
    }

    int beginCount{}, paintCount{}, restoreCount{}, commitCount{};
    CatalogPreviewBeginRequest beginRequest{};
    CatalogPreviewPaintRequest paintRequest{};
    CatalogPreviewRestoreRequest restoreRequest{};
    CatalogPreviewCommitRequest commitRequest{};
};

void validatesAndForwardsExactContext() {
    FakeRuntime runtime;
    SlaveTatsService service(runtime);
    const CatalogPreviewBeginRequest begin{0x123, 9, TattooArea::body, 2};
    const auto begun = service.beginCatalogPreview(begin);
    expect(begun && begun->sessionId == 77 && runtime.beginRequest == begin,
        "valid begin should forward exact actor context");

    TattooEntry candidate{.section = "Pack", .name = "Mark", .area = "Body"};
    const CatalogPreviewPaintRequest paint{77, begin, candidate};
    expect(service.paintCatalogPreview(paint).has_value() && runtime.paintRequest == paint,
        "valid paint should forward exact session and candidate");
    const CatalogPreviewPaintRequest clear{77, begin, std::nullopt};
    expect(service.paintCatalogPreview(clear).has_value() && runtime.paintRequest == clear,
        "hover exit should forward an empty candidate without requiring tattoo identity");
    const CatalogPreviewRestoreRequest restore{77, begin};
    expect(service.restoreCatalogPreview(restore).has_value() && runtime.restoreRequest == restore,
        "valid restore should forward exact session and context");
    CatalogPreviewCommitRequest commit{77, begin,
        ApplyTattooRequest{.actorFormId = 0x123, .area = TattooArea::body,
            .slot = 2, .section = "Pack", .name = "Mark"}};
    expect(service.commitCatalogPreview(commit).has_value() && runtime.commitCount == 1 &&
        runtime.commitRequest.apply.section == "Pack",
        "valid Save should forward exact actor, slot and pinned identity");
}

void rejectsInvalidRequestsBeforeRuntime() {
    FakeRuntime runtime;
    SlaveTatsService service(runtime);
    CatalogPreviewBeginRequest begin{0, 1, TattooArea::body, 0};
    expect(!service.beginCatalogPreview(begin), "zero actor should be rejected");
    begin.actorFormId = 0x14;
    begin.slot = -1;
    expect(!service.beginCatalogPreview(begin), "negative slot should be rejected");

    CatalogPreviewPaintRequest paint{};
    paint.sessionId = 1;
    paint.context = {0x14, 1, TattooArea::body, 0};
    paint.candidate = TattooEntry{.section = "", .name = "Mark", .area = "Body"};
    expect(!service.paintCatalogPreview(paint), "missing identity should be rejected");
    expect(runtime.beginCount == 0 && runtime.paintCount == 0,
        "invalid requests must not reach runtime");
    CatalogPreviewCommitRequest commit{1, paint.context,
        ApplyTattooRequest{.actorFormId = 0x123, .area = TattooArea::body,
            .slot = 0, .section = "Pack", .name = "Mark"}};
    expect(!service.commitCatalogPreview(commit) && runtime.commitCount == 0,
        "Save to a different Actor must stop before runtime");
}

}  // namespace

int main() {
    validatesAndForwardsExactContext();
    rejectsInvalidRequestsBeforeRuntime();
    std::cout << "CatalogPreviewServiceTests passed\n";
}
