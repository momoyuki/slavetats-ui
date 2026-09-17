#pragma once

#include "native/NativeCatalogBrowserModel.h"
#include "native/MenuFrameworkPort.h"
#include "core/TattooModels.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stui::runtime {
class HotkeyBinding;
}

namespace stui::native {

class NativeThumbnailRuntime;
class NativeSlotWorkflowModel;
class NativeSlotWorkflowRuntime;
struct AppearanceEditSession;
enum class SlotWorkflowScreen;
enum class NativeThumbnailStatus;
struct NativeThumbnailView;

struct MenuFrameworkBindings {
    using GetVersionFunction = float (*)();
    using AddSectionItemFunction = void (*)(const char*, MenuCallback);
    using AddWindowFunction = void* (*)(MenuCallback);
    using SetWindowOpenFunction = void (*)(void*, bool) noexcept;
    using IsWindowOpenFunction = bool (*)(const void*) noexcept;
    using SetWindowBlockingFunction = void (*)(void*, bool) noexcept;

    GetVersionFunction getVersion{};
    AddSectionItemFunction addSectionItem{};
    AddWindowFunction addWindow{};
    SetWindowOpenFunction setWindowOpen{};
    IsWindowOpenFunction isWindowOpen{};
    SetWindowBlockingFunction setWindowBlocking{};
};

struct MenuPosition {
    float x{};
    float y{};
};

struct MenuSize {
    float width{};
    float height{};
};

struct FoundationLayout {
    MenuPosition position;
    MenuSize size;
};

struct CatalogThumbnailFit {
    float width{};
    float height{};
};

enum class SlotCardTreatment {
    add,
    replace,
    disabled,
};

struct SlotPageRange {
    std::size_t pageIndex{};
    std::size_t pageCount{};
    std::size_t begin{};
    std::size_t end{};
};

[[nodiscard]] SlotCardTreatment slotCardTreatment(
    core::SlotOccupancy occupancy) noexcept;
[[nodiscard]] SlotPageRange calculateSlotPage(
    std::size_t slotCount,
    std::size_t requestedPage,
    std::size_t pageSize) noexcept;
[[nodiscard]] std::string_view slotAreaLabel(core::TattooArea area) noexcept;
[[nodiscard]] std::vector<std::string> collectVisibleSlotTexturePaths(
    const core::TattooSlots& slots,
    std::size_t pageIndex,
    std::size_t pageSize);
[[nodiscard]] std::string formatSlotTargetLabel(
    core::TattooArea area,
    std::int32_t slot);
[[nodiscard]] std::string previewApplyButtonLabel(
    std::int32_t slot,
    bool retry = false);
[[nodiscard]] bool isPreviewApplyEnabled(
    SlotWorkflowScreen screen,
    bool hasTarget,
    bool hasTattoo) noexcept;
[[nodiscard]] bool isAppearanceSaveEnabled(
    SlotWorkflowScreen screen,
    const AppearanceEditSession* session) noexcept;
[[nodiscard]] bool isAppearanceEditingEnabled(
    SlotWorkflowScreen screen,
    const AppearanceEditSession* session) noexcept;
struct AppearanceSavePresentation {
    std::string_view label;
    bool enabled{};
};

[[nodiscard]] AppearanceSavePresentation appearanceSavePresentation(
    SlotWorkflowScreen screen,
    const AppearanceEditSession* session) noexcept;
enum class RemoveButtonState {
    initial,
    retryRemove,
    retrySynchronization,
};

[[nodiscard]] std::string removeButtonLabel(
    std::int32_t slot,
    RemoveButtonState state);
[[nodiscard]] bool isRemoveConfirmationEnabled(
    SlotWorkflowScreen screen,
    bool hasTarget) noexcept;
struct SlotLockActionPresentation {
    unsigned int iconCodepoint{};
    std::string_view tooltip;
    bool mutationsEnabled{};
};

[[nodiscard]] SlotLockActionPresentation slotLockActionPresentation(bool locked) noexcept;
[[nodiscard]] std::string_view domainPresentationLabel(std::string_view domain) noexcept;
[[nodiscard]] std::string_view domainThumbnailBadgeLabel(std::string_view domain) noexcept;
[[nodiscard]] std::vector<std::string> buildCatalogBrowserDomainOptions(
    const std::vector<std::string>& domains);
[[nodiscard]] std::vector<std::string> collectPickerTexturePaths(
    const repository::TattooPage& page);
[[nodiscard]] std::size_t pickerVisibleCardCount(
    const repository::TattooPage& page) noexcept;

struct CatalogCardGridPosition {
    std::size_t row{};
    std::size_t column{};
};

[[nodiscard]] CatalogCardGridPosition catalogCardGridPosition(
    std::size_t index,
    std::size_t columnCount) noexcept;

[[nodiscard]] std::string catalogCardWidgetId(
    std::string_view role,
    std::string_view sourceId,
    std::size_t sourceIndex);

struct CatalogBrowserGridLayout {
    float gridHeight{};
    float rowHeight{};
    float thumbnailHeight{};
};

struct CatalogBadgeLayout {
    float x{};
    float y{};
    float width{};
    float height{};
    float textX{};
    float textY{};
};

[[nodiscard]] CatalogBadgeLayout calculateCatalogBadgeLayout(
    float containerWidth,
    float textWidth,
    float textHeight,
    float horizontalPadding,
    float verticalPadding,
    float margin) noexcept;

[[nodiscard]] float calculateCatalogCardMetadataHeight(
    float textLineHeight,
    float itemSpacing) noexcept;

[[nodiscard]] float calculateRightAlignedControlX(
    float availableWidth,
    float controlWidth) noexcept;

struct UnifiedFooterLayout {
    float actionWidth{};
    float closeWidth{};
    float closeX{};
};

[[nodiscard]] UnifiedFooterLayout calculateUnifiedFooterLayout(
    float availableWidth,
    float closeWidth) noexcept;

struct PickerFooterActionLayout {
    float groupWidth{};
    float cancelX{};
    float closeX{};
};

struct TattooColorComponents {
    float red{};
    float green{};
    float blue{};
};

struct AppearanceThumbnailPresentation {
    std::string_view texturePath;
    TattooColorComponents color;
    float alpha{};
};

struct EditAppearanceFramePresentation {
    bool shouldContinue{};
    std::optional<AppearanceThumbnailPresentation> thumbnail;
};

struct EditAppearanceFrameInteraction {
    bool appearanceChanged{};
    std::int32_t color{0xFFFFFF};
    float alpha{1.0F};
    std::int32_t glow{};
    float glossiness{};
    float specularStrength{};
    float emissiveMult{1.0F};
    bool cancelRequested{};
};

struct SlotColorSwatchPresentation {
    float x{};
    float y{};
    float size{};
    std::uint32_t fillColor{};
    std::uint32_t borderColor{};
};

[[nodiscard]] TattooColorComponents tattooColorComponents(
    std::int32_t color) noexcept;
[[nodiscard]] std::int32_t tattooColorValue(
    TattooColorComponents components) noexcept;
[[nodiscard]] std::string_view appearanceTextureMetadata(
    std::string_view texturePath) noexcept;
[[nodiscard]] std::optional<AppearanceThumbnailPresentation> editAppearanceThumbnailPresentation(
    const AppearanceEditSession* session) noexcept;
[[nodiscard]] EditAppearanceFramePresentation editAppearanceFramePresentation(
    SlotWorkflowScreen screen,
    const AppearanceEditSession* postCommandSession) noexcept;
void orchestrateEditAppearanceFrame(
    NativeSlotWorkflowModel& workflow,
    EditAppearanceFrameInteraction interaction,
    const std::function<void()>& teardown,
    const std::function<void(const AppearanceThumbnailPresentation&)>& continueRendering);

[[nodiscard]] std::string formatCatalogTattooTooltip(
    std::string_view tattooName,
    const std::vector<std::int32_t>& inUseSlots);

[[nodiscard]] std::optional<SlotColorSwatchPresentation> calculateSlotColorSwatch(
    const core::TattooSlot& slot,
    float containerWidth,
    float containerHeight,
    float size,
    float margin) noexcept;

[[nodiscard]] PickerFooterActionLayout calculatePickerFooterActionLayout(
    float availableWidth,
    float cancelWidth,
    float closeWidth,
    float itemSpacing) noexcept;

[[nodiscard]] CatalogBrowserGridLayout calculateCatalogBrowserGridLayout(
    float availableHeight,
    float footerHeight,
    float metadataHeight,
    std::size_t rowCount) noexcept;

[[nodiscard]] CatalogThumbnailFit fitCatalogThumbnail(
    std::size_t textureWidth,
    std::size_t textureHeight,
    float maximumWidth,
    float maximumHeight) noexcept;

[[nodiscard]] std::string_view catalogThumbnailStatusLabel(
    NativeThumbnailStatus status) noexcept;

[[nodiscard]] std::optional<std::size_t> findCatalogThumbnailViewIndex(
    std::string_view texturePath,
    const std::vector<NativeThumbnailView>& thumbnailViews) noexcept;

class CatalogBrowserPageInputState {
public:
    void synchronize(std::size_t pageIndex, std::size_t pageCount) noexcept;
    [[nodiscard]] int& pendingPageNumber() noexcept;
    [[nodiscard]] std::optional<std::size_t> finishFrame(
        bool itemActive,
        bool committedOnEnter,
        bool committedOnDeactivate) noexcept;

private:
    int pendingPageNumber_{};
    int committedPageNumber_{};
    std::size_t pageCount_{};
    bool editing_{};
};

enum class CatalogBrowserEmptyState {
    none,
    emptyCatalog,
    noMatches,
};

[[nodiscard]] CatalogBrowserEmptyState classifyCatalogBrowserEmptyState(
    bool hasSnapshot,
    const repository::TattooPage& page) noexcept;
[[nodiscard]] std::string_view catalogBrowserEmptyMessage(
    CatalogBrowserEmptyState state) noexcept;

struct CatalogBrowserSourceOption {
    std::string label;
    std::string sourceId;
};

[[nodiscard]] std::vector<CatalogBrowserSourceOption> buildCatalogBrowserSourceOptions(
    const std::vector<repository::TattooSourceOption>& sources);

class OfficialMenuFrameworkAdapter final : public MenuFrameworkPort {
public:
    OfficialMenuFrameworkAdapter();
    explicit OfficialMenuFrameworkAdapter(MenuFrameworkBindings bindings);

    [[nodiscard]] bool available() const noexcept override;
    [[nodiscard]] float version() const noexcept override;
    [[nodiscard]] RegistrationResult setSection(std::string_view section) override;
    [[nodiscard]] RegistrationResult addSectionItem(
        std::string_view path, MenuCallback callback) override;
    [[nodiscard]] std::expected<MenuWindow, MenuRegistrationError> addWindow(
        MenuCallback callback, bool pauseGame) override;
    void setWindowOpen(MenuWindow window, bool open) noexcept override;
    [[nodiscard]] bool isWindowOpen(MenuWindow window) const noexcept override;

    [[nodiscard]] static FoundationLayout calculateFoundationLayout(
        MenuPosition viewportPosition, MenuSize viewportSize) noexcept;
    [[nodiscard]] static bool renderLauncher(runtime::HotkeyBinding& hotkey);
    static void renderFoundation(
        NativeSlotWorkflowModel& workflow,
        NativeSlotWorkflowRuntime& slotRuntime,
        NativeCatalogBrowserModel& catalog,
        NativeThumbnailRuntime& thumbnails,
        const std::function<void()>& close);

private:
    MenuFrameworkBindings bindings_;
    std::string section_;
};

}  // namespace stui::native
