#include "native/OfficialMenuFrameworkAdapter.h"
#include "runtime/HotkeyBinding.h"

#include <RE/Skyrim.h>

#include "native/NativeThumbnailRuntime.h"
#include "native/NativeSlotWorkflowModel.h"
#include "native/NativeSlotWorkflowRuntime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <limits>
#include <utility>
#include <vector>

#include "SKSEMenuFramework.h"

namespace stui::native {
namespace {

SKSEMenuFramework::Model::AddSectionItemFunction g_addSectionItem{};
SKSEMenuFramework::Model::AddWindowFunction g_addWindow{};

bool equalsFoldedASCII(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    return std::ranges::equal(left, right, [](char lhs, char rhs) {
        const auto fold = [](char value) {
            return value >= 'A' && value <= 'Z'
                ? static_cast<char>(value + ('a' - 'A'))
                : value;
        };
        return fold(lhs) == fold(rhs);
    });
}

void addOfficialSectionItem(const char* path, MenuCallback callback) {
    g_addSectionItem(
        path, reinterpret_cast<SKSEMenuFramework::Model::RenderFunction>(callback));
}

void* addOfficialWindow(MenuCallback callback) {
    return g_addWindow(
        reinterpret_cast<SKSEMenuFramework::Model::RenderFunction>(callback));
}

void setOfficialWindowOpen(void* window, bool open) noexcept {
    static_cast<SKSEMenuFramework::Model::WindowInterface*>(window)->IsOpen = open;
}

bool isOfficialWindowOpen(const void* window) noexcept {
    return static_cast<const SKSEMenuFramework::Model::WindowInterface*>(window)->IsOpen.load();
}

void setOfficialWindowBlocking(void* window, bool blocking) noexcept {
    static_cast<SKSEMenuFramework::Model::WindowInterface*>(window)->BlockUserInput = blocking;
}

MenuFrameworkBindings resolveBindings() {
    const HMODULE module = GetModuleHandleW(L"SKSEMenuFramework.dll");
    if (!module) {
        return {};
    }
    g_addSectionItem = reinterpret_cast<SKSEMenuFramework::Model::AddSectionItemFunction>(
        GetProcAddress(module, "AddSectionItem"));
    g_addWindow = reinterpret_cast<SKSEMenuFramework::Model::AddWindowFunction>(
        GetProcAddress(module, "AddWindow"));
    return {
        reinterpret_cast<MenuFrameworkBindings::GetVersionFunction>(
            GetProcAddress(module, "GetMenuFrameworkVersion")),
        g_addSectionItem ? &addOfficialSectionItem : nullptr,
        g_addWindow ? &addOfficialWindow : nullptr,
        &setOfficialWindowOpen,
        &isOfficialWindowOpen,
        &setOfficialWindowBlocking,
    };
}

}  // namespace

OfficialMenuFrameworkAdapter::OfficialMenuFrameworkAdapter()
    : OfficialMenuFrameworkAdapter(resolveBindings()) {}

OfficialMenuFrameworkAdapter::OfficialMenuFrameworkAdapter(MenuFrameworkBindings bindings)
    : bindings_(std::move(bindings)) {}

SlotCardTreatment slotCardTreatment(core::SlotOccupancy occupancy) noexcept {
    switch (occupancy) {
    case core::SlotOccupancy::empty:
        return SlotCardTreatment::add;
    case core::SlotOccupancy::slaveTats:
        return SlotCardTreatment::replace;
    case core::SlotOccupancy::external:
        return SlotCardTreatment::disabled;
    }
    return SlotCardTreatment::disabled;
}

SlotPageRange calculateSlotPage(
    std::size_t slotCount,
    std::size_t requestedPage,
    std::size_t pageSize) noexcept {
    if (slotCount == 0 || pageSize == 0) {
        return {};
    }

    const std::size_t pageCount = slotCount / pageSize +
        (slotCount % pageSize != 0 ? 1 : 0);
    const std::size_t pageIndex = std::min(requestedPage, pageCount - 1);
    const std::size_t begin = pageIndex * pageSize;
    return {
        .pageIndex = pageIndex,
        .pageCount = pageCount,
        .begin = begin,
        .end = std::min(slotCount, begin + pageSize),
    };
}

std::string_view slotAreaLabel(core::TattooArea area) noexcept {
    switch (area) {
    case core::TattooArea::body:
        return "BODY";
    case core::TattooArea::face:
        return "FACE";
    case core::TattooArea::hands:
        return "HANDS";
    case core::TattooArea::feet:
        return "FEET";
    }
    return "BODY";
}

std::vector<std::string> collectVisibleSlotTexturePaths(
    const core::TattooSlots& slots,
    std::size_t pageIndex,
    std::size_t pageSize) {
    const auto page = calculateSlotPage(slots.slots.size(), pageIndex, pageSize);
    std::vector<std::string> paths;
    paths.reserve(page.end - page.begin);
    for (std::size_t index = page.begin; index < page.end; ++index) {
        const auto& slot = slots.slots[index];
        if (slot.occupancy == core::SlotOccupancy::slaveTats && slot.tattoo &&
            !slot.tattoo->texturePath.empty()) {
            paths.push_back(slot.tattoo->texturePath);
        }
    }
    return paths;
}

std::string formatSlotTargetLabel(core::TattooArea area, std::int32_t slot) {
    return "Player / " + std::string(slotAreaLabel(area)) + " / Slot " +
        std::to_string(slot);
}

std::string previewApplyButtonLabel(std::int32_t slot, bool retry) {
    return std::string(retry ? "Retry Slot " : "Apply to Slot ") +
        std::to_string(slot);
}

bool isPreviewApplyEnabled(
    SlotWorkflowScreen screen,
    bool hasTarget,
    bool hasTattoo) noexcept {
    return screen == SlotWorkflowScreen::preview && hasTarget && hasTattoo;
}

bool isAppearanceSaveEnabled(
    SlotWorkflowScreen screen,
    const AppearanceEditSession* session) noexcept {
    return screen == SlotWorkflowScreen::editAppearance && session &&
        session->edited != session->original;
}

bool isAppearanceEditingEnabled(
    SlotWorkflowScreen screen,
    const AppearanceEditSession* session) noexcept {
    return screen == SlotWorkflowScreen::editAppearance && session &&
        session->mode == core::UpdateTattooAppearanceMode::updateAndSynchronize;
}

AppearanceSavePresentation appearanceSavePresentation(
    SlotWorkflowScreen screen,
    const AppearanceEditSession* session) noexcept {
    return {
        .label = session &&
                session->mode == core::UpdateTattooAppearanceMode::synchronizeOnly
            ? "Retry Sync"
            : "Save",
        .enabled = isAppearanceSaveEnabled(screen, session),
    };
}

std::string removeButtonLabel(std::int32_t slot, RemoveButtonState state) {
    switch (state) {
    case RemoveButtonState::initial:
        return "Remove from Slot " + std::to_string(slot);
    case RemoveButtonState::retryRemove:
        return "Retry Remove Slot " + std::to_string(slot);
    case RemoveButtonState::retrySynchronization:
        return "Retry Sync Slot " + std::to_string(slot);
    }
    return "Remove from Slot " + std::to_string(slot);
}

bool isRemoveConfirmationEnabled(
    SlotWorkflowScreen screen,
    bool hasTarget) noexcept {
    return screen == SlotWorkflowScreen::removeConfirmation && hasTarget;
}

SlotLockActionPresentation slotLockActionPresentation(bool locked) noexcept {
    return {
        .iconCodepoint = locked ? 0xF023U : 0xF09CU,
        .tooltip = locked ? "Unlock tattoo" : "Lock tattoo",
        .mutationsEnabled = !locked,
    };
}

std::string_view domainPresentationLabel(std::string_view domain) noexcept {
    return domain.empty() ? "default" : domain;
}

std::string_view domainThumbnailBadgeLabel(std::string_view domain) noexcept {
    return domain.empty() || equalsFoldedASCII(domain, "default") ? "" : domain;
}

std::vector<std::string> buildCatalogBrowserDomainOptions(
    const std::vector<std::string>& domains) {
    std::vector<std::string> options;
    options.reserve(domains.size() + 1);
    options.emplace_back("All Domains");
    options.insert(options.end(), domains.begin(), domains.end());
    return options;
}

std::vector<std::string> collectPickerTexturePaths(
    const repository::TattooPage& page) {
    const std::size_t visibleCount = pickerVisibleCardCount(page);
    std::vector<std::string> paths;
    paths.reserve(visibleCount);
    for (std::size_t index = 0; index < visibleCount; ++index) {
        if (!page.entries[index].texturePath.empty()) {
            paths.push_back(page.entries[index].texturePath);
        }
    }
    return paths;
}

std::size_t pickerVisibleCardCount(const repository::TattooPage& page) noexcept {
    constexpr std::size_t maximumVisibleCards = 6;
    return std::min(page.entries.size(), maximumVisibleCards);
}

bool OfficialMenuFrameworkAdapter::available() const noexcept {
    return bindings_.getVersion && bindings_.addSectionItem && bindings_.addWindow &&
           bindings_.setWindowOpen && bindings_.isWindowOpen && bindings_.setWindowBlocking;
}

float OfficialMenuFrameworkAdapter::version() const noexcept {
    return bindings_.getVersion ? bindings_.getVersion() : 0.0F;
}

RegistrationResult OfficialMenuFrameworkAdapter::setSection(std::string_view section) {
    if (!available()) {
        return std::unexpected(MenuRegistrationError::missingExport);
    }
    section_ = section;
    return {};
}

RegistrationResult OfficialMenuFrameworkAdapter::addSectionItem(
    std::string_view path, MenuCallback callback) {
    if (!bindings_.addSectionItem || section_.empty()) {
        return std::unexpected(MenuRegistrationError::missingExport);
    }
    const std::string fullPath = section_ + "/" + std::string(path);
    bindings_.addSectionItem(fullPath.c_str(), callback);
    return {};
}

std::expected<MenuWindow, MenuRegistrationError> OfficialMenuFrameworkAdapter::addWindow(
    MenuCallback callback, bool pauseGame) {
    if (!bindings_.addWindow || !bindings_.setWindowBlocking) {
        return std::unexpected(MenuRegistrationError::missingExport);
    }
    void* window = bindings_.addWindow(callback);
    if (!window) {
        return std::unexpected(MenuRegistrationError::windowCreationFailed);
    }
    bindings_.setWindowBlocking(window, pauseGame);
    return reinterpret_cast<MenuWindow>(window);
}

void OfficialMenuFrameworkAdapter::setWindowOpen(MenuWindow window, bool open) noexcept {
    if (bindings_.setWindowOpen && window != 0) {
        bindings_.setWindowOpen(reinterpret_cast<void*>(window), open);
    }
}

bool OfficialMenuFrameworkAdapter::isWindowOpen(MenuWindow window) const noexcept {
    return bindings_.isWindowOpen && window != 0 &&
           bindings_.isWindowOpen(reinterpret_cast<const void*>(window));
}

FoundationLayout OfficialMenuFrameworkAdapter::calculateFoundationLayout(
    MenuPosition viewportPosition, MenuSize viewportSize) noexcept {
    constexpr float margin = 20.0F;
    const MenuSize panelSize{
        viewportSize.width * 0.4F,
        viewportSize.height - (margin * 2.0F),
    };
    return {
        {
            viewportPosition.x + viewportSize.width - panelSize.width - margin,
            viewportPosition.y + margin,
        },
        panelSize,
    };
}

CatalogThumbnailFit fitCatalogThumbnail(
    std::size_t textureWidth,
    std::size_t textureHeight,
    float maximumWidth,
    float maximumHeight) noexcept {
    if (textureWidth == 0 || textureHeight == 0 || maximumWidth <= 0.0F ||
        maximumHeight <= 0.0F) {
        return {};
    }

    const float width = static_cast<float>(textureWidth);
    const float height = static_cast<float>(textureHeight);
    const float scale = std::min(maximumWidth / width, maximumHeight / height);
    return {.width = width * scale, .height = height * scale};
}

CatalogCardGridPosition catalogCardGridPosition(
    std::size_t index,
    std::size_t columnCount) noexcept {
    if (columnCount == 0) {
        return {};
    }
    return {.row = index / columnCount, .column = index % columnCount};
}

std::string catalogCardWidgetId(
    std::string_view role,
    std::string_view sourceId,
    std::size_t sourceIndex) {
    return std::string(role) + "##" + std::string(sourceId) + ":" +
        std::to_string(sourceIndex);
}

CatalogBrowserGridLayout calculateCatalogBrowserGridLayout(
    float availableHeight,
    float footerHeight,
    float metadataHeight,
    std::size_t rowCount) noexcept {
    if (rowCount == 0) {
        return {};
    }

    const float gridHeight = std::max(0.0F, availableHeight - footerHeight);
    const float rowHeight = gridHeight / static_cast<float>(rowCount);
    return {
        .gridHeight = gridHeight,
        .rowHeight = rowHeight,
        .thumbnailHeight = std::max(0.0F, rowHeight - metadataHeight),
    };
}

CatalogBadgeLayout calculateCatalogBadgeLayout(
    float containerWidth,
    float textWidth,
    float textHeight,
    float horizontalPadding,
    float verticalPadding,
    float margin) noexcept {
    const float width = std::max(0.0F, textWidth + horizontalPadding * 2.0F);
    const float height = std::max(0.0F, textHeight + verticalPadding * 2.0F);
    const float x = std::max(0.0F, containerWidth - margin - width);
    const float y = std::max(0.0F, margin);
    return {
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .textX = x + horizontalPadding,
        .textY = y + verticalPadding,
    };
}

float calculateCatalogCardMetadataHeight(
    float,
    float) noexcept {
    return 0.0F;
}

float calculateRightAlignedControlX(
    float availableWidth,
    float controlWidth) noexcept {
    return std::max(0.0F, availableWidth - controlWidth);
}

UnifiedFooterLayout calculateUnifiedFooterLayout(
    float availableWidth,
    float closeWidth) noexcept {
    const float safeAvailableWidth = std::max(0.0F, availableWidth);
    const float safeCloseWidth = std::clamp(closeWidth, 0.0F, safeAvailableWidth);
    const float closeX = safeAvailableWidth - safeCloseWidth;
    return {
        .actionWidth = closeX,
        .closeWidth = safeCloseWidth,
        .closeX = closeX,
    };
}

PickerFooterActionLayout calculatePickerFooterActionLayout(
    float availableWidth,
    float cancelWidth,
    float closeWidth,
    float itemSpacing) noexcept {
    const float safeCancelWidth = std::max(0.0F, cancelWidth);
    const float safeCloseWidth = std::max(0.0F, closeWidth);
    const float safeItemSpacing = std::max(0.0F, itemSpacing);
    const float groupWidth = safeCancelWidth + safeItemSpacing + safeCloseWidth;
    const float cancelX = calculateRightAlignedControlX(availableWidth, groupWidth);
    return PickerFooterActionLayout{
        .groupWidth = groupWidth,
        .cancelX = cancelX,
        .closeX = cancelX + safeCancelWidth + safeItemSpacing,
    };
}

TattooColorComponents tattooColorComponents(std::int32_t color) noexcept {
    const auto value = static_cast<std::uint32_t>(
        std::clamp(color, 0, 0xFFFFFF));
    return TattooColorComponents{
        .red = static_cast<float>((value >> 16U) & 0xFFU) / 255.0F,
        .green = static_cast<float>((value >> 8U) & 0xFFU) / 255.0F,
        .blue = static_cast<float>(value & 0xFFU) / 255.0F,
    };
}

std::int32_t tattooColorValue(TattooColorComponents components) noexcept {
    const auto channel = [](float value) {
        return static_cast<std::uint32_t>(
            std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    return static_cast<std::int32_t>(
        (channel(components.red) << 16U) |
        (channel(components.green) << 8U) |
        channel(components.blue));
}

std::string_view appearanceTextureMetadata(std::string_view texturePath) noexcept {
    return texturePath.empty() ? "None" : texturePath;
}

std::optional<AppearanceThumbnailPresentation> editAppearanceThumbnailPresentation(
    const AppearanceEditSession* session) noexcept {
    if (!session) {
        return std::nullopt;
    }
    return AppearanceThumbnailPresentation{
        .texturePath = session->texturePath,
        .color = tattooColorComponents(session->edited.color),
        .alpha = session->edited.alpha,
    };
}

EditAppearanceFramePresentation editAppearanceFramePresentation(
    SlotWorkflowScreen screen,
    const AppearanceEditSession* postCommandSession) noexcept {
    const bool shouldContinue =
        (screen == SlotWorkflowScreen::editAppearance ||
            screen == SlotWorkflowScreen::savingAppearance) &&
        postCommandSession;
    return {
        .shouldContinue = shouldContinue,
        .thumbnail = shouldContinue
            ? editAppearanceThumbnailPresentation(postCommandSession)
            : std::nullopt,
    };
}

void orchestrateEditAppearanceFrame(
    NativeSlotWorkflowModel& workflow,
    EditAppearanceFrameInteraction interaction,
    const std::function<void()>& teardown,
    const std::function<void(const AppearanceThumbnailPresentation&)>& continueRendering) {
    if (interaction.appearanceChanged) {
        workflow.setEditedAppearance(
            interaction.color,
            interaction.alpha,
            interaction.glow,
            interaction.glossiness,
            interaction.specularStrength,
            interaction.emissiveMult);
    }
    if (interaction.cancelRequested) {
        workflow.cancelEditAppearance();
    }

    const auto presentation = editAppearanceFramePresentation(
        workflow.screen(), workflow.editAppearance());
    if (!presentation.shouldContinue) {
        if (teardown) {
            teardown();
        }
        return;
    }
    if (continueRendering) {
        continueRendering(presentation.thumbnail.value());
    }
}

std::string formatCatalogTattooTooltip(
    std::string_view tattooName,
    const std::vector<std::int32_t>& inUseSlots) {
    std::string tooltip(tattooName);
    if (inUseSlots.empty()) {
        return tooltip;
    }

    tooltip += "\nIn use: Slots ";
    for (std::size_t index = 0; index < inUseSlots.size(); ++index) {
        if (index != 0) {
            tooltip += ", ";
        }
        tooltip += std::to_string(inUseSlots[index]);
    }
    return tooltip;
}

std::optional<SlotColorSwatchPresentation> calculateSlotColorSwatch(
    const core::TattooSlot& slot,
    float containerWidth,
    float containerHeight,
    float size,
    float margin) noexcept {
    if (slot.occupancy != core::SlotOccupancy::slaveTats || !slot.tattoo) {
        return std::nullopt;
    }

    const float safeWidth = std::max(0.0F, containerWidth);
    const float safeHeight = std::max(0.0F, containerHeight);
    const float safeMargin = std::max(0.0F, margin);
    const float maximumSize = std::max(
        0.0F,
        std::min(safeWidth, safeHeight) - safeMargin * 2.0F);
    const float safeSize = std::clamp(size, 0.0F, maximumSize);
    const auto rgb = static_cast<std::uint32_t>(
        std::clamp(slot.tattoo->color, 0, 0xFFFFFF));
    const auto imGuiColor = 0xFF000000U |
        ((rgb & 0x0000FFU) << 16U) |
        (rgb & 0x00FF00U) |
        ((rgb & 0xFF0000U) >> 16U);

    return SlotColorSwatchPresentation{
        .x = std::max(0.0F, safeWidth - safeMargin - safeSize),
        .y = std::max(0.0F, safeHeight - safeMargin - safeSize),
        .size = safeSize,
        .fillColor = imGuiColor,
        .borderColor = 0xFF202020U,
    };
}

namespace {

char canonicalThumbnailPathCharacter(char character) noexcept {
    if (character == '\\') {
        return '/';
    }
    if (character >= 'A' && character <= 'Z') {
        return static_cast<char>(character - 'A' + 'a');
    }
    return character;
}

bool equivalentThumbnailPaths(std::string_view left, std::string_view right) noexcept {
    return left.size() == right.size() && std::ranges::equal(
        left,
        right,
        {},
        canonicalThumbnailPathCharacter,
        canonicalThumbnailPathCharacter);
}

}  // namespace

std::string_view catalogThumbnailStatusLabel(NativeThumbnailStatus status) noexcept {
    switch (status) {
    case NativeThumbnailStatus::loading:
        return "Loading";
    case NativeThumbnailStatus::missing:
        return "Missing";
    case NativeThumbnailStatus::broken:
        return "Broken";
    case NativeThumbnailStatus::placeholder:
    case NativeThumbnailStatus::ready:
        return {};
    }
    return {};
}

std::optional<std::size_t> findCatalogThumbnailViewIndex(
    std::string_view texturePath,
    const std::vector<NativeThumbnailView>& thumbnailViews) noexcept {
    for (std::size_t index = 0; index < thumbnailViews.size(); ++index) {
        if (equivalentThumbnailPaths(texturePath, thumbnailViews[index].texturePath)) {
            return index;
        }
    }
    return std::nullopt;
}

void CatalogBrowserPageInputState::synchronize(
    std::size_t pageIndex,
    std::size_t pageCount) noexcept {
    const std::size_t oneBasedPage = pageCount == 0 ? 0 : pageIndex + 1;
    const int committedPageNumber = static_cast<int>(std::min(
        oneBasedPage,
        static_cast<std::size_t>(std::numeric_limits<int>::max())));
    if (!editing_ || committedPageNumber != committedPageNumber_ || pageCount != pageCount_) {
        pendingPageNumber_ = committedPageNumber;
    }
    committedPageNumber_ = committedPageNumber;
    pageCount_ = pageCount;
    if (pageCount_ == 0) {
        editing_ = false;
    }
}

int& CatalogBrowserPageInputState::pendingPageNumber() noexcept {
    return pendingPageNumber_;
}

std::optional<std::size_t> CatalogBrowserPageInputState::finishFrame(
    bool itemActive,
    bool committedOnEnter,
    bool committedOnDeactivate) noexcept {
    if (pageCount_ == 0) {
        editing_ = false;
        return std::nullopt;
    }
    if (committedOnEnter || committedOnDeactivate) {
        editing_ = false;
        return pendingPageNumber_ > 0 ? static_cast<std::size_t>(pendingPageNumber_) : 0;
    }
    editing_ = itemActive;
    return std::nullopt;
}

CatalogBrowserEmptyState classifyCatalogBrowserEmptyState(
    bool hasSnapshot,
    const repository::TattooPage& page) noexcept {
    if (!hasSnapshot || page.totalEntries == 0) {
        return CatalogBrowserEmptyState::emptyCatalog;
    }
    if (page.matchedEntries == 0) {
        return CatalogBrowserEmptyState::noMatches;
    }
    return CatalogBrowserEmptyState::none;
}

std::string_view catalogBrowserEmptyMessage(CatalogBrowserEmptyState state) noexcept {
    switch (state) {
    case CatalogBrowserEmptyState::emptyCatalog:
        return "The tattoo catalog is empty. Refresh the catalog to browse tattoos.";
    case CatalogBrowserEmptyState::noMatches:
        return "No tattoos match the current filters.";
    case CatalogBrowserEmptyState::none:
        return {};
    }
    return {};
}

std::vector<CatalogBrowserSourceOption> buildCatalogBrowserSourceOptions(
    const std::vector<repository::TattooSourceOption>& sources) {
    std::vector<CatalogBrowserSourceOption> options;
    options.reserve(sources.size());
    for (const auto& source : sources) {
        options.push_back({
            .label = source.packName + " (" + source.sourceId + ")",
            .sourceId = source.sourceId,
        });
    }
    return options;
}

namespace {

NativeThumbnailEpoch slotThumbnailEpoch(core::TattooArea area) {
    static const std::array<NativeThumbnailEpoch, 4> epochs{
        std::make_shared<int>(0),
        std::make_shared<int>(1),
        std::make_shared<int>(2),
        std::make_shared<int>(3),
    };
    switch (area) {
    case core::TattooArea::body:
        return epochs[0];
    case core::TattooArea::face:
        return epochs[1];
    case core::TattooArea::hands:
        return epochs[2];
    case core::TattooArea::feet:
        return epochs[3];
    }
    return epochs[0];
}

void renderSlotImage(
    const core::TattooSlot& slot,
    const std::vector<NativeThumbnailView>& thumbnailViews,
    ImGuiMCP::ImVec2 region,
    ImGuiMCP::ImVec4 tint = {1.0F, 1.0F, 1.0F, 1.0F}) {
    if (slot.occupancy == core::SlotOccupancy::empty) {
        ImGuiMCP::TextUnformatted("Add");
        return;
    }
    if (slot.occupancy == core::SlotOccupancy::external) {
        ImGuiMCP::TextUnformatted("External - Locked");
        return;
    }
    if (!slot.tattoo || slot.tattoo->texturePath.empty()) {
        ImGuiMCP::TextUnformatted("No thumbnail");
        return;
    }

    const auto thumbnailIndex = findCatalogThumbnailViewIndex(
        slot.tattoo->texturePath,
        thumbnailViews);
    const NativeThumbnailView* thumbnail = thumbnailIndex
        ? &thumbnailViews[*thumbnailIndex]
        : nullptr;
    if (thumbnail && thumbnail->status == NativeThumbnailStatus::ready &&
        thumbnail->texture && thumbnail->texture->shaderResourceView) {
        const auto fit = fitCatalogThumbnail(
            thumbnail->texture->width,
            thumbnail->texture->height,
            region.x,
            region.y);
        const auto origin = ImGuiMCP::GetCursorPos();
        ImGuiMCP::SetCursorPos({
            origin.x + (region.x - fit.width) / 2.0F,
            origin.y + (region.y - fit.height) / 2.0F,
        });
        ImGuiMCP::Image(
            static_cast<ImGuiMCP::ImTextureID>(
                thumbnail->texture->shaderResourceView.get()),
            {fit.width, fit.height},
            {0.0F, 0.0F},
            {1.0F, 1.0F},
            tint);
        return;
    }
    if (thumbnail) {
        const auto label = catalogThumbnailStatusLabel(thumbnail->status);
        if (!label.empty()) {
            ImGuiMCP::TextUnformatted(label.data());
        }
    }
}

void renderCurrentSlots(
    NativeSlotWorkflowModel& workflow,
    NativeThumbnailRuntime& thumbnails,
    const std::function<void()>& close) {
    const auto* slots = workflow.slots();
    const auto paths = slots
        ? collectVisibleSlotTexturePaths(
              *slots,
              workflow.slotPageIndex(),
              NativeSlotWorkflowModel::kPageSize)
        : std::vector<std::string>{};
    thumbnails.synchronize(slotThumbnailEpoch(workflow.selectedArea()), paths);
    thumbnails.pump();
    const auto thumbnailViews = thumbnails.views();

    const auto* viewport = ImGuiMCP::GetMainViewport();
    if (!viewport) {
        return;
    }
    const auto layout = OfficialMenuFrameworkAdapter::calculateFoundationLayout(
        {viewport->Pos.x, viewport->Pos.y},
        {viewport->Size.x, viewport->Size.y});
    ImGuiMCP::SetNextWindowPos(
        {layout.position.x, layout.position.y}, ImGuiMCP::ImGuiCond_Appearing, {0.0F, 0.0F});
    ImGuiMCP::SetNextWindowSize(
        {layout.size.width, layout.size.height}, ImGuiMCP::ImGuiCond_Appearing);
    bool open = true;
    ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGuiMCP::Begin(
        "Tattoo Browser##SlaveTatsUI",
        &open,
        ImGuiMCP::ImGuiWindowFlags_NoCollapse |
            ImGuiMCP::ImGuiWindowFlags_NoScrollbar |
            ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse);

    ImGuiMCP::TextUnformatted("Current Tattoos - Player");
    ImGuiMCP::SameLine();
    if (ImGuiMCP::Button("Refresh")) {
        workflow.refreshSelectedArea();
    }

    constexpr std::array<core::TattooArea, 4> areas{
        core::TattooArea::body,
        core::TattooArea::face,
        core::TattooArea::hands,
        core::TattooArea::feet,
    };
    for (std::size_t index = 0; index < areas.size(); ++index) {
        if (index != 0) {
            ImGuiMCP::SameLine();
        }
        const auto label = std::string(slotAreaLabel(areas[index])) + "##SlotArea" +
            std::to_string(index);
        ImGuiMCP::BeginDisabled(areas[index] == workflow.selectedArea());
        if (ImGuiMCP::Button(label.c_str())) {
            workflow.selectArea(areas[index]);
        }
        ImGuiMCP::EndDisabled();
    }

    if (const auto* error = workflow.error()) {
        ImGuiMCP::TextUnformatted(error->message.c_str());
    }

    const auto* style = ImGuiMCP::GetStyle();
    const float footerHeight = ImGuiMCP::GetFrameHeightWithSpacing();
    const auto gridLayout = calculateCatalogBrowserGridLayout(
        ImGuiMCP::GetContentRegionAvail().y,
        footerHeight,
        0.0F,
        3);
    const auto page = slots
        ? calculateSlotPage(
              slots->slots.size(),
              workflow.slotPageIndex(),
              NativeSlotWorkflowModel::kPageSize)
        : SlotPageRange{};

    if (!slots) {
        if (ImGuiMCP::BeginChild(
                "SlotLoadingState",
                {0.0F, gridLayout.gridHeight},
                ImGuiMCP::ImGuiChildFlags_None,
                ImGuiMCP::ImGuiWindowFlags_NoScrollbar |
                    ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse)) {
            ImGuiMCP::TextUnformatted("Loading Player tattoo slots...");
        }
        ImGuiMCP::EndChild();
    } else if (ImGuiMCP::BeginTable(
                   "CurrentSlotCards",
                   2,
                   ImGuiMCP::ImGuiTableFlags_SizingStretchSame |
                       ImGuiMCP::ImGuiTableFlags_BordersInner |
                       ImGuiMCP::ImGuiTableFlags_NoPadOuterX |
                       ImGuiMCP::ImGuiTableFlags_NoPadInnerX,
                   {0.0F, gridLayout.gridHeight})) {
        for (std::size_t index = page.begin; index < page.end; ++index) {
            const auto gridPosition = catalogCardGridPosition(index - page.begin, 2);
            if (gridPosition.column == 0) {
                ImGuiMCP::TableNextRow(0, gridLayout.rowHeight);
            }
            ImGuiMCP::TableSetColumnIndex(static_cast<int>(gridPosition.column));
            const auto& slot = slots->slots[index];
            const auto widgetId = std::string("SlotCard##") + std::to_string(slot.index);
            const bool disabled = slotCardTreatment(slot.occupancy) ==
                SlotCardTreatment::disabled;
            bool lockClicked = false;
            bool lockHovered = false;
            ImGuiMCP::BeginDisabled(disabled);
            ImGuiMCP::PushStyleColor(
                ImGuiMCP::ImGuiCol_ChildBg,
                ImGuiMCP::ImVec4(0.08F, 0.08F, 0.08F, 1.0F));
            if (ImGuiMCP::BeginChild(
                    widgetId.c_str(),
                    {0.0F, std::max(1.0F, gridLayout.rowHeight)},
                    ImGuiMCP::ImGuiChildFlags_Border,
                    ImGuiMCP::ImGuiWindowFlags_NoScrollbar |
                        ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse)) {
                const auto imageRegion = ImGuiMCP::GetContentRegionAvail();
                const auto imageScreenOrigin = ImGuiMCP::GetCursorScreenPos();
                renderSlotImage(slot, thumbnailViews, imageRegion);
                if (const auto swatch = calculateSlotColorSwatch(
                        slot, imageRegion.x, imageRegion.y, 16.0F, 6.0F)) {
                    auto* drawList = ImGuiMCP::GetWindowDrawList();
                    const ImGuiMCP::ImVec2 minimum{
                        imageScreenOrigin.x + swatch->x,
                        imageScreenOrigin.y + swatch->y,
                    };
                    const ImGuiMCP::ImVec2 maximum{
                        minimum.x + swatch->size,
                        minimum.y + swatch->size,
                    };
                    ImGuiMCP::ImDrawListManager::AddRectFilled(
                        drawList,
                        minimum,
                        maximum,
                        swatch->fillColor,
                        0.0F,
                        0);
                    ImGuiMCP::ImDrawListManager::AddRect(
                        drawList,
                        minimum,
                        maximum,
                        swatch->borderColor,
                        0.0F,
                        0,
                        1.0F);
                }
                if (slot.occupancy == core::SlotOccupancy::slaveTats && slot.tattoo) {
                    const auto lockPresentation = slotLockActionPresentation(slot.tattoo->locked);
                    const auto lockIcon = FontAwesome::UnicodeToUtf8(
                        lockPresentation.iconCodepoint);
                    const auto lockWidgetId = lockIcon + "##SlotLock" +
                        std::to_string(slot.index);
                    FontAwesome::PushSolid();
                    const auto iconSize = ImGuiMCP::CalcTextSize(lockIcon.c_str());
                    const float buttonSize = std::max(
                        24.0F,
                        iconSize.y + (style ? style->FramePadding.y * 2.0F : 8.0F));
                    ImGuiMCP::SetCursorScreenPos({
                        imageScreenOrigin.x + imageRegion.x - buttonSize - 6.0F,
                        imageScreenOrigin.y + 6.0F,
                    });
                    ImGuiMCP::BeginDisabled(workflow.isLockStateChangeInFlight());
                    if (ImGuiMCP::Button(lockWidgetId.c_str(), {buttonSize, buttonSize})) {
                        lockClicked = workflow.toggleSlotLock(slot.index);
                    }
                    lockHovered = ImGuiMCP::IsItemHovered(
                        ImGuiMCP::ImGuiHoveredFlags_AllowWhenDisabled);
                    if (lockHovered) {
                        ImGuiMCP::SetTooltip("%s", lockPresentation.tooltip.data());
                    }
                    ImGuiMCP::EndDisabled();
                    FontAwesome::Pop();
                }
            }
            ImGuiMCP::EndChild();
            ImGuiMCP::PopStyleColor();
            ImGuiMCP::EndDisabled();
            if (!disabled && !lockClicked && !lockHovered && ImGuiMCP::IsItemClicked()) {
                (void)workflow.selectSlot(slot.index);
            }
            if (ImGuiMCP::IsItemHovered()) {
                if (slot.tattoo) {
                    ImGuiMCP::SetTooltip("Slot %d - %s", slot.index, slot.tattoo->name.c_str());
                } else if (disabled) {
                    ImGuiMCP::SetTooltip("Slot %d is managed by another overlay mod", slot.index);
                } else {
                    ImGuiMCP::SetTooltip("Add a tattoo to Slot %d", slot.index);
                }
            }
        }
        ImGuiMCP::EndTable();
    }

    const float closeButtonWidth = ImGuiMCP::CalcTextSize("Close").x +
        (style ? style->FramePadding.x * 2.0F : 16.0F);
    if (ImGuiMCP::BeginTable(
            "SlotFooter",
            2,
            ImGuiMCP::ImGuiTableFlags_SizingStretchProp |
                ImGuiMCP::ImGuiTableFlags_NoPadOuterX)) {
        ImGuiMCP::TableSetupColumn("SlotPagination", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
        ImGuiMCP::TableSetupColumn(
            "SlotCloseAction", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, closeButtonWidth);
        ImGuiMCP::TableNextRow();
        ImGuiMCP::TableSetColumnIndex(0);
        ImGuiMCP::BeginDisabled(page.pageCount == 0 || page.pageIndex == 0);
        if (ImGuiMCP::Button("Prev##Slots")) {
            workflow.previousSlotPage();
        }
        ImGuiMCP::EndDisabled();
        ImGuiMCP::SameLine();
        ImGuiMCP::TextUnformatted("Page");
        ImGuiMCP::SameLine();
        static CatalogBrowserPageInputState slotPageInputState;
        slotPageInputState.synchronize(page.pageIndex, page.pageCount);
        ImGuiMCP::SetNextItemWidth(56.0F);
        ImGuiMCP::InputInt(
            "##SlotPageNumber",
            &slotPageInputState.pendingPageNumber(),
            0,
            0);
        if (const auto requestedPage = slotPageInputState.finishFrame(
                ImGuiMCP::IsItemActive(),
                false,
                ImGuiMCP::IsItemDeactivatedAfterEdit())) {
            workflow.setSlotPageNumber(*requestedPage);
        }
        ImGuiMCP::SameLine();
        ImGuiMCP::Text("/ %zu", page.pageCount);
        ImGuiMCP::SameLine();
        ImGuiMCP::BeginDisabled(page.pageCount == 0 || page.pageIndex + 1 >= page.pageCount);
        if (ImGuiMCP::Button("Next##Slots")) {
            workflow.nextSlotPage();
        }
        ImGuiMCP::EndDisabled();

        ImGuiMCP::TableSetColumnIndex(1);
        if (ImGuiMCP::Button("Close")) {
            open = false;
        }
        ImGuiMCP::EndTable();
    }
    ImGuiMCP::End();
    ImGuiMCP::PopStyleVar();
    if (!open && close) {
        close();
    }
}

const core::TattooSlot* selectedWorkflowSlot(
    const NativeSlotWorkflowModel& workflow) noexcept {
    const auto target = workflow.targetSlot();
    const auto* slots = workflow.slots();
    if (!target || !slots) {
        return nullptr;
    }

    const auto found = std::ranges::find_if(
        slots->slots,
        [target](const core::TattooSlot& slot) { return slot.index == *target; });
    return found == slots->slots.end() ? nullptr : &*found;
}

void renderSlotActions(
    NativeSlotWorkflowModel& workflow,
    NativeThumbnailRuntime& thumbnails,
    const std::function<void()>& close) {
    const auto target = workflow.targetSlot();
    const auto* slot = selectedWorkflowSlot(workflow);
    std::vector<std::string> paths;
    if (slot && slot->tattoo && !slot->tattoo->texturePath.empty()) {
        paths.push_back(slot->tattoo->texturePath);
    }
    thumbnails.synchronize(slotThumbnailEpoch(workflow.selectedArea()), paths);
    thumbnails.pump();
    const auto thumbnailViews = thumbnails.views();

    const auto* viewport = ImGuiMCP::GetMainViewport();
    if (!viewport) {
        return;
    }
    const auto layout = OfficialMenuFrameworkAdapter::calculateFoundationLayout(
        {viewport->Pos.x, viewport->Pos.y},
        {viewport->Size.x, viewport->Size.y});
    ImGuiMCP::SetNextWindowPos(
        {layout.position.x, layout.position.y}, ImGuiMCP::ImGuiCond_Appearing, {0.0F, 0.0F});
    ImGuiMCP::SetNextWindowSize(
        {layout.size.width, layout.size.height}, ImGuiMCP::ImGuiCond_Appearing);
    bool open = true;
    ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGuiMCP::Begin(
        "Tattoo Browser##SlaveTatsUI",
        &open,
        ImGuiMCP::ImGuiWindowFlags_NoCollapse |
            ImGuiMCP::ImGuiWindowFlags_NoScrollbar |
            ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse);

    const bool confirming =
        workflow.screen() == SlotWorkflowScreen::removeConfirmation;
    const bool removing = workflow.screen() == SlotWorkflowScreen::removing;
    ImGuiMCP::TextUnformatted(
        confirming || removing ? "Remove Tattoo?" : "Tattoo Slot Actions");
    if (target) {
        const auto targetLabel = formatSlotTargetLabel(workflow.selectedArea(), *target);
        ImGuiMCP::TextUnformatted(targetLabel.c_str());
    }
    if (slot && slot->tattoo) {
        ImGuiMCP::Text("%s / %s",
            slot->tattoo->section.c_str(),
            slot->tattoo->name.c_str());
        ImGuiMCP::Text("Domain: %s", domainPresentationLabel(slot->tattoo->domain).data());
    }
    if (const auto* error = workflow.error()) {
        ImGuiMCP::TextUnformatted(error->message.c_str());
    } else if (removing) {
        ImGuiMCP::TextUnformatted("Removing tattoo...");
    } else if (confirming) {
        ImGuiMCP::TextUnformatted("This will clear the selected SlaveTats slot.");
    }

    const float footerHeight = ImGuiMCP::GetFrameHeightWithSpacing();
    const float imageHeight = std::max(
        1.0F,
        ImGuiMCP::GetContentRegionAvail().y - footerHeight);
    ImGuiMCP::PushStyleColor(
        ImGuiMCP::ImGuiCol_ChildBg,
        ImGuiMCP::ImVec4(0.08F, 0.08F, 0.08F, 1.0F));
    if (ImGuiMCP::BeginChild(
            "SlotActionImage",
            {0.0F, imageHeight},
            ImGuiMCP::ImGuiChildFlags_Border,
            ImGuiMCP::ImGuiWindowFlags_NoScrollbar |
                ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse)) {
        if (slot) {
            renderSlotImage(*slot, thumbnailViews, ImGuiMCP::GetContentRegionAvail());
        }
    }
    ImGuiMCP::EndChild();
    ImGuiMCP::PopStyleColor();

    const auto* style = ImGuiMCP::GetStyle();
    const float closeButtonWidth = ImGuiMCP::CalcTextSize("Close").x +
        (style ? style->FramePadding.x * 2.0F : 16.0F);
    const auto footerLayout = calculateUnifiedFooterLayout(
        ImGuiMCP::GetContentRegionAvail().x,
        closeButtonWidth);
    if (ImGuiMCP::BeginTable(
            "SlotActionsFooter",
            2,
            ImGuiMCP::ImGuiTableFlags_SizingStretchProp |
                ImGuiMCP::ImGuiTableFlags_NoPadOuterX)) {
        ImGuiMCP::TableSetupColumn(
            "SlotWorkflowActions", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
        ImGuiMCP::TableSetupColumn(
            "SlotWorkflowClose",
            ImGuiMCP::ImGuiTableColumnFlags_WidthFixed,
            footerLayout.closeWidth);
        ImGuiMCP::TableNextRow();
        ImGuiMCP::TableSetColumnIndex(0);
        if (confirming || removing) {
            ImGuiMCP::BeginDisabled(removing);
            if (ImGuiMCP::Button("Cancel")) {
                workflow.cancelRemove();
            }
            ImGuiMCP::EndDisabled();
            ImGuiMCP::SameLine();
            const bool canRemove = isRemoveConfirmationEnabled(
                workflow.screen(), target.has_value());
            RemoveButtonState buttonState = RemoveButtonState::initial;
            if (const auto* error = workflow.error()) {
                buttonState = error->code == core::ServiceErrorCode::synchronizeFailed
                    ? RemoveButtonState::retrySynchronization
                    : RemoveButtonState::retryRemove;
            }
            const auto removeLabel = removeButtonLabel(target.value_or(-1), buttonState);
            ImGuiMCP::BeginDisabled(!canRemove);
            if (ImGuiMCP::Button(removeLabel.c_str())) {
                (void)workflow.confirmRemove();
            }
            ImGuiMCP::EndDisabled();
        } else {
            const bool locked = slot && slot->tattoo && slot->tattoo->locked;
            const auto lockPresentation = slotLockActionPresentation(locked);
            const bool lockStateChangeInFlight = workflow.isLockStateChangeInFlight();
            if (ImGuiMCP::Button("Back")) {
                workflow.backToSlots();
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::BeginDisabled(!lockPresentation.mutationsEnabled || lockStateChangeInFlight);
            if (ImGuiMCP::Button("Replace")) {
                (void)workflow.replaceSelectedSlot();
            }
            ImGuiMCP::EndDisabled();
            const bool canEditAppearance = slot &&
                slot->occupancy == core::SlotOccupancy::slaveTats && slot->tattoo &&
                slot->tattoo->runtimeHandle != 0;
            if (canEditAppearance) {
                ImGuiMCP::SameLine();
                ImGuiMCP::BeginDisabled(lockStateChangeInFlight);
                if (ImGuiMCP::Button("Edit Appearance")) {
                    (void)workflow.beginEditAppearance();
                }
                ImGuiMCP::EndDisabled();
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::BeginDisabled(!lockPresentation.mutationsEnabled || lockStateChangeInFlight);
            if (ImGuiMCP::Button("Remove")) {
                (void)workflow.requestRemove();
            }
            ImGuiMCP::EndDisabled();
        }
        ImGuiMCP::TableSetColumnIndex(1);
        if (ImGuiMCP::Button("Close")) {
            open = false;
        }
        ImGuiMCP::EndTable();
    }

    ImGuiMCP::End();
    ImGuiMCP::PopStyleVar();
    if (!open && close) {
        close();
    }
}

void renderPreview(
    NativeSlotWorkflowModel& workflow,
    NativeCatalogBrowserModel& catalog,
    NativeThumbnailRuntime& thumbnails,
    const std::function<void()>& close) {
    const auto* preview = workflow.previewTattoo();
    const auto* appearance = workflow.previewAppearance();
    const auto target = workflow.targetSlot();
    std::vector<std::string> paths;
    if (preview && !preview->texturePath.empty()) {
        paths.push_back(preview->texturePath);
    }
    NativeThumbnailEpoch epoch = catalog.snapshot();
    if (!epoch) {
        static const NativeThumbnailEpoch fallbackEpoch = std::make_shared<int>(4);
        epoch = fallbackEpoch;
    }
    thumbnails.synchronize(std::move(epoch), paths);
    thumbnails.pump();
    const auto thumbnailViews = thumbnails.views();

    const auto* viewport = ImGuiMCP::GetMainViewport();
    if (!viewport) {
        return;
    }
    const auto layout = OfficialMenuFrameworkAdapter::calculateFoundationLayout(
        {viewport->Pos.x, viewport->Pos.y},
        {viewport->Size.x, viewport->Size.y});
    ImGuiMCP::SetNextWindowPos(
        {layout.position.x, layout.position.y}, ImGuiMCP::ImGuiCond_Appearing, {0.0F, 0.0F});
    ImGuiMCP::SetNextWindowSize(
        {layout.size.width, layout.size.height}, ImGuiMCP::ImGuiCond_Appearing);
    bool open = true;
    ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGuiMCP::Begin(
        "Tattoo Browser##SlaveTatsUI",
        &open,
        ImGuiMCP::ImGuiWindowFlags_NoCollapse |
            ImGuiMCP::ImGuiWindowFlags_NoScrollbar |
            ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse);

    ImGuiMCP::TextUnformatted("Preview Tattoo");
    if (target) {
        const auto targetLabel = formatSlotTargetLabel(workflow.selectedArea(), *target);
        ImGuiMCP::TextUnformatted(targetLabel.c_str());
    }
    if (preview) {
        ImGuiMCP::Text("%s / %s", preview->section.c_str(), preview->name.c_str());
    }
    const bool applying = workflow.screen() == SlotWorkflowScreen::applying;
    if (const auto* error = workflow.error()) {
        ImGuiMCP::TextUnformatted(error->message.c_str());
    } else if (applying) {
        ImGuiMCP::TextUnformatted("Applying tattoo...");
    }

    TattooColorComponents colorComponents =
        tattooColorComponents(appearance ? appearance->color : 0xFFFFFF);
    float pickerColor[3]{
        colorComponents.red,
        colorComponents.green,
        colorComponents.blue,
    };
    float alpha = appearance ? appearance->alpha : 1.0F;
    ImGuiMCP::BeginDisabled(applying || !appearance);
    const bool colorChanged = ImGuiMCP::ColorEdit3(
        "Color",
        pickerColor,
        ImGuiMCP::ImGuiColorEditFlags_DisplayHex |
            ImGuiMCP::ImGuiColorEditFlags_Uint8);
    const bool alphaChanged =
        ImGuiMCP::SliderFloat("Alpha", &alpha, 0.0F, 1.0F, "%.2f");
    if (colorChanged || alphaChanged) {
        colorComponents = TattooColorComponents{
            .red = pickerColor[0],
            .green = pickerColor[1],
            .blue = pickerColor[2],
        };
        workflow.setPreviewAppearance(tattooColorValue(colorComponents), alpha);
    }
    ImGuiMCP::EndDisabled();

    const float footerHeight = ImGuiMCP::GetFrameHeightWithSpacing();
    const float imageHeight = std::max(
        1.0F,
        ImGuiMCP::GetContentRegionAvail().y - footerHeight);
    ImGuiMCP::PushStyleColor(
        ImGuiMCP::ImGuiCol_ChildBg,
        ImGuiMCP::ImVec4(0.08F, 0.08F, 0.08F, 1.0F));
    if (ImGuiMCP::BeginChild(
            "PreviewImage",
            {0.0F, imageHeight},
            ImGuiMCP::ImGuiChildFlags_Border,
            ImGuiMCP::ImGuiWindowFlags_NoScrollbar |
                ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse)) {
        if (preview) {
            core::TattooSlot previewSlot{
                .index = target.value_or(-1),
                .occupancy = core::SlotOccupancy::slaveTats,
                .tattoo = core::TattooEntry{
                    .section = preview->section,
                    .name = preview->name,
                    .texturePath = preview->texturePath,
                },
            };
            const auto tintColor = tattooColorComponents(
                appearance ? appearance->color : 0xFFFFFF);
            renderSlotImage(
                previewSlot,
                thumbnailViews,
                ImGuiMCP::GetContentRegionAvail(),
                {
                    tintColor.red,
                    tintColor.green,
                    tintColor.blue,
                    appearance ? appearance->alpha : 1.0F,
                });
        }
    }
    ImGuiMCP::EndChild();
    ImGuiMCP::PopStyleColor();

    const auto* style = ImGuiMCP::GetStyle();
    const float closeButtonWidth = ImGuiMCP::CalcTextSize("Close").x +
        (style ? style->FramePadding.x * 2.0F : 16.0F);
    const auto footerLayout = calculateUnifiedFooterLayout(
        ImGuiMCP::GetContentRegionAvail().x,
        closeButtonWidth);
    if (ImGuiMCP::BeginTable(
            "PreviewFooter",
            2,
            ImGuiMCP::ImGuiTableFlags_SizingStretchProp |
                ImGuiMCP::ImGuiTableFlags_NoPadOuterX)) {
        ImGuiMCP::TableSetupColumn(
            "PreviewActions", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
        ImGuiMCP::TableSetupColumn(
            "PreviewClose",
            ImGuiMCP::ImGuiTableColumnFlags_WidthFixed,
            footerLayout.closeWidth);
        ImGuiMCP::TableNextRow();
        ImGuiMCP::TableSetColumnIndex(0);
        ImGuiMCP::BeginDisabled(applying);
        if (ImGuiMCP::Button("Cancel")) {
            workflow.cancelPreview();
        }
        ImGuiMCP::EndDisabled();
        ImGuiMCP::SameLine();
        const bool canApply = isPreviewApplyEnabled(
            workflow.screen(), target.has_value(), preview != nullptr);
        const auto applyLabel = previewApplyButtonLabel(
            target.value_or(-1), workflow.error() != nullptr);
        ImGuiMCP::BeginDisabled(!canApply);
        if (ImGuiMCP::Button(applyLabel.c_str())) {
            (void)workflow.confirmApply();
        }
        ImGuiMCP::EndDisabled();
        ImGuiMCP::TableSetColumnIndex(1);
        if (ImGuiMCP::Button("Close")) {
            open = false;
        }
        ImGuiMCP::EndTable();
    }

    ImGuiMCP::End();
    ImGuiMCP::PopStyleVar();
    if (!open && close) {
        close();
    }
}

void renderEditAppearance(
    NativeSlotWorkflowModel& workflow,
    NativeThumbnailRuntime& thumbnails,
    const std::function<void()>& close) {
    const auto* session = workflow.editAppearance();
    const auto initialFramePresentation =
        editAppearanceFramePresentation(workflow.screen(), session);
    if (!initialFramePresentation.shouldContinue) {
        return;
    }
    std::vector<std::string> paths;
    if (initialFramePresentation.thumbnail &&
        !initialFramePresentation.thumbnail->texturePath.empty()) {
        paths.emplace_back(initialFramePresentation.thumbnail->texturePath);
    }
    thumbnails.synchronize(slotThumbnailEpoch(workflow.selectedArea()), paths);
    thumbnails.pump();
    const auto thumbnailViews = thumbnails.views();

    const auto* viewport = ImGuiMCP::GetMainViewport();
    if (!viewport) {
        return;
    }
    const auto layout = OfficialMenuFrameworkAdapter::calculateFoundationLayout(
        {viewport->Pos.x, viewport->Pos.y},
        {viewport->Size.x, viewport->Size.y});
    ImGuiMCP::SetNextWindowPos(
        {layout.position.x, layout.position.y}, ImGuiMCP::ImGuiCond_Appearing, {0.0F, 0.0F});
    ImGuiMCP::SetNextWindowSize(
        {layout.size.width, layout.size.height}, ImGuiMCP::ImGuiCond_Appearing);
    bool open = true;
    ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGuiMCP::Begin(
        "Tattoo Browser##SlaveTatsUI",
        &open,
        ImGuiMCP::ImGuiWindowFlags_NoCollapse |
            ImGuiMCP::ImGuiWindowFlags_NoScrollbar |
            ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse);

    const bool saving = workflow.screen() == SlotWorkflowScreen::savingAppearance;
    ImGuiMCP::TextUnformatted("Edit Appearance");
    if (session) {
        const auto targetLabel = formatSlotTargetLabel(session->area, session->slot);
        ImGuiMCP::TextUnformatted(targetLabel.c_str());
    }
    if (const auto* error = workflow.error()) {
        ImGuiMCP::TextUnformatted(error->message.c_str());
    } else if (saving) {
        ImGuiMCP::TextUnformatted("Saving appearance...");
    }

    TattooColorComponents colorComponents =
        tattooColorComponents(session ? session->edited.color : 0xFFFFFF);
    float colorValues[3]{
        colorComponents.red,
        colorComponents.green,
        colorComponents.blue,
    };
    float alpha = session ? session->edited.alpha : 1.0F;
    TattooColorComponents glowComponents =
        tattooColorComponents(session ? session->edited.glow : 0);
    float glowValues[3]{
        glowComponents.red,
        glowComponents.green,
        glowComponents.blue,
    };
    float emissiveMult = session ? session->edited.emissiveMult : 1.0F;
    float glossiness = session ? session->edited.glossiness : 0.0F;
    float specularStrength = session ? session->edited.specularStrength : 0.0F;
    ImGuiMCP::BeginDisabled(!isAppearanceEditingEnabled(workflow.screen(), session));
    ImGuiMCP::SeparatorText("Basic");
    const bool colorChanged = ImGuiMCP::ColorEdit3(
        "Color", colorValues, ImGuiMCP::ImGuiColorEditFlags_NoInputs);
    const bool alphaChanged =
        ImGuiMCP::SliderFloat("Alpha", &alpha, 0.0F, 1.0F, "%.2f");
    ImGuiMCP::SeparatorText("Material / Emission");
    const bool glowChanged = ImGuiMCP::ColorEdit3(
        "Glow Color", glowValues, ImGuiMCP::ImGuiColorEditFlags_NoInputs);
    const bool emissiveSliderChanged = ImGuiMCP::SliderFloat(
        "Emission Strength", &emissiveMult, 0.0F, 10.0F, "%.2f");
    const bool emissiveInputChanged =
        ImGuiMCP::InputFloat("Emission Strength Value", &emissiveMult, 0.0F, 0.0F, "%.3f");
    const bool glossinessSliderChanged =
        ImGuiMCP::SliderFloat("Glossiness", &glossiness, 0.0F, 10.0F, "%.2f");
    const bool glossinessInputChanged =
        ImGuiMCP::InputFloat("Glossiness Value", &glossiness, 0.0F, 0.0F, "%.3f");
    const bool specularSliderChanged = ImGuiMCP::SliderFloat(
        "Specular Strength", &specularStrength, 0.0F, 10.0F, "%.2f");
    const bool specularInputChanged = ImGuiMCP::InputFloat(
        "Specular Strength Value", &specularStrength, 0.0F, 0.0F, "%.3f");
    const EditAppearanceFrameInteraction frameInteraction{
        .appearanceChanged = colorChanged || alphaChanged || glowChanged ||
            emissiveSliderChanged || emissiveInputChanged || glossinessSliderChanged ||
            glossinessInputChanged || specularSliderChanged || specularInputChanged,
        .color = tattooColorValue({
                .red = colorValues[0],
                .green = colorValues[1],
                .blue = colorValues[2],
            }),
        .alpha = alpha,
        .glow = tattooColorValue({
            .red = glowValues[0],
            .green = glowValues[1],
            .blue = glowValues[2],
        }),
        .glossiness = glossiness,
        .specularStrength = specularStrength,
        .emissiveMult = emissiveMult,
    };
    ImGuiMCP::EndDisabled();

    if (session) {
        if (session->edited.glow != 0 || !session->glowTexture.empty() ||
            session->edited.emissiveMult != 1.0F) {
            ImGuiMCP::SameLine();
            ImGuiMCP::TextUnformatted("Glow");
        }
        if (!session->bump.empty()) {
            ImGuiMCP::SameLine();
            ImGuiMCP::TextUnformatted("Bump");
        }
        ImGuiMCP::TextUnformatted("Glow Texture:");
        ImGuiMCP::SameLine();
        ImGuiMCP::TextUnformatted(appearanceTextureMetadata(session->glowTexture).data());
        ImGuiMCP::TextUnformatted("Bump Texture:");
        ImGuiMCP::SameLine();
        ImGuiMCP::TextUnformatted(appearanceTextureMetadata(session->bump).data());
    }

    const auto teardown = [] {
        ImGuiMCP::End();
        ImGuiMCP::PopStyleVar();
    };
    orchestrateEditAppearanceFrame(
        workflow,
        frameInteraction,
        teardown,
        [&](const AppearanceThumbnailPresentation& thumbnailPresentation) {
            const float footerHeight = ImGuiMCP::GetFrameHeightWithSpacing();
            const float imageHeight = std::max(
                1.0F,
                ImGuiMCP::GetContentRegionAvail().y - footerHeight);
            ImGuiMCP::PushStyleColor(
                ImGuiMCP::ImGuiCol_ChildBg,
                ImGuiMCP::ImVec4(0.08F, 0.08F, 0.08F, 1.0F));
            if (ImGuiMCP::BeginChild(
                    "EditAppearanceImage",
                    {0.0F, imageHeight},
                    ImGuiMCP::ImGuiChildFlags_Border,
                    ImGuiMCP::ImGuiWindowFlags_NoScrollbar |
                        ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse)) {
                if (session) {
                    const core::TattooSlot editSlot{
                        .index = session->slot,
                        .occupancy = core::SlotOccupancy::slaveTats,
                        .tattoo = core::TattooEntry{
                            .texturePath = std::string(thumbnailPresentation.texturePath)},
                    };
                    renderSlotImage(
                        editSlot,
                        thumbnailViews,
                        ImGuiMCP::GetContentRegionAvail(),
                        {
                            thumbnailPresentation.color.red,
                            thumbnailPresentation.color.green,
                            thumbnailPresentation.color.blue,
                            thumbnailPresentation.alpha,
                        });
                }
            }
            ImGuiMCP::EndChild();
            ImGuiMCP::PopStyleColor();

            const auto* style = ImGuiMCP::GetStyle();
            const float closeButtonWidth = ImGuiMCP::CalcTextSize("Close").x +
                (style ? style->FramePadding.x * 2.0F : 16.0F);
            const auto footerLayout = calculateUnifiedFooterLayout(
                ImGuiMCP::GetContentRegionAvail().x,
                closeButtonWidth);
            bool cancelled = false;
            if (ImGuiMCP::BeginTable(
                    "EditAppearanceFooter",
                    2,
                    ImGuiMCP::ImGuiTableFlags_SizingStretchProp |
                        ImGuiMCP::ImGuiTableFlags_NoPadOuterX)) {
                ImGuiMCP::TableSetupColumn(
                    "EditAppearanceActions", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
                ImGuiMCP::TableSetupColumn(
                    "EditAppearanceClose",
                    ImGuiMCP::ImGuiTableColumnFlags_WidthFixed,
                    footerLayout.closeWidth);
                ImGuiMCP::TableNextRow();
                ImGuiMCP::TableSetColumnIndex(0);
                ImGuiMCP::BeginDisabled(saving);
                cancelled = ImGuiMCP::Button("Cancel");
                ImGuiMCP::EndDisabled();
                ImGuiMCP::SameLine();
                const auto savePresentation = appearanceSavePresentation(
                    workflow.screen(), workflow.editAppearance());
                ImGuiMCP::BeginDisabled(!savePresentation.enabled);
                if (ImGuiMCP::Button(savePresentation.label.data())) {
                    (void)workflow.confirmAppearanceUpdate();
                }
                ImGuiMCP::EndDisabled();
                ImGuiMCP::TableSetColumnIndex(1);
                ImGuiMCP::BeginDisabled(saving);
                if (ImGuiMCP::Button("Close")) {
                    open = false;
                }
                ImGuiMCP::EndDisabled();
                ImGuiMCP::EndTable();
            }

            if (cancelled) {
                workflow.cancelEditAppearance();
            }
            teardown();
            if (!open && close) {
                close();
            }
        });
}

}  // namespace

bool OfficialMenuFrameworkAdapter::renderLauncher(runtime::HotkeyBinding& hotkey) {
    ImGuiMCP::TextUnformatted("Open SlaveTatsUI when you are ready to browse tattoos.");
    const auto hotkeyLabel = hotkey.label();
    static bool hotkeySaveFailed = false;
    if (ImGuiMCP::BeginCombo("Hotkey", hotkeyLabel.c_str())) {
        const auto selectedKey = hotkey.key();
        for (const auto& option : runtime::hotkeyOptions()) {
            const bool selected = option.key == selectedKey;
            if (ImGuiMCP::Selectable(option.label.data(), selected)) {
                hotkeySaveFailed = !hotkey.select(option.key);
            }
            if (selected) {
                ImGuiMCP::SetItemDefaultFocus();
            }
        }
        ImGuiMCP::EndCombo();
    }
    if (hotkeySaveFailed) {
        ImGuiMCP::TextUnformatted("Could not save the hotkey setting.");
    }
    return ImGuiMCP::Button("Open Tattoo Browser");
}

void OfficialMenuFrameworkAdapter::renderFoundation(
    NativeSlotWorkflowModel& workflow,
    NativeSlotWorkflowRuntime& slotRuntime,
    NativeCatalogBrowserModel& model,
    NativeThumbnailRuntime& thumbnails,
    const std::function<void()>& close) {
    slotRuntime.pump();
    if (workflow.screen() == SlotWorkflowScreen::currentSlots) {
        renderCurrentSlots(workflow, thumbnails, close);
        return;
    }
    if (workflow.screen() == SlotWorkflowScreen::slotActions ||
        workflow.screen() == SlotWorkflowScreen::removeConfirmation ||
        workflow.screen() == SlotWorkflowScreen::removing) {
        renderSlotActions(workflow, thumbnails, close);
        return;
    }
    if (workflow.screen() == SlotWorkflowScreen::editAppearance ||
        workflow.screen() == SlotWorkflowScreen::savingAppearance) {
        renderEditAppearance(workflow, thumbnails, close);
        return;
    }
    if (workflow.screen() == SlotWorkflowScreen::preview ||
        workflow.screen() == SlotWorkflowScreen::applying) {
        renderPreview(workflow, model, thumbnails, close);
        return;
    }

    model.refresh();
    const auto targetArea = slotAreaLabel(workflow.selectedArea());
    if (model.filter().area != targetArea) {
        model.setArea(std::string(targetArea));
    }
    thumbnails.synchronize(model.snapshot(), collectPickerTexturePaths(model.page()));
    thumbnails.pump();
    const auto thumbnailViews = thumbnails.views();

    const auto* viewport = ImGuiMCP::GetMainViewport();
    if (!viewport) {
        return;
    }
    const auto layout = calculateFoundationLayout(
        {viewport->Pos.x, viewport->Pos.y},
        {viewport->Size.x, viewport->Size.y});
    ImGuiMCP::SetNextWindowPos(
        {layout.position.x, layout.position.y}, ImGuiMCP::ImGuiCond_Appearing, {0.0F, 0.0F});
    ImGuiMCP::SetNextWindowSize(
        {layout.size.width, layout.size.height}, ImGuiMCP::ImGuiCond_Appearing);
    bool open = true;
    ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGuiMCP::Begin(
        "Tattoo Browser##SlaveTatsUI",
        &open,
        ImGuiMCP::ImGuiWindowFlags_NoCollapse |
            ImGuiMCP::ImGuiWindowFlags_NoScrollbar |
            ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse);

    const auto targetLabel = formatSlotTargetLabel(
        workflow.selectedArea(), workflow.targetSlot().value_or(-1));
    ImGuiMCP::Text("Target: %s", targetLabel.c_str());

    static bool filtersExpanded = false;
    if (ImGuiMCP::Button(filtersExpanded ? "Hide filters" : "Filters")) {
        filtersExpanded = !filtersExpanded;
    }

    constexpr std::size_t searchCapacity = 256;
    std::array<char, searchCapacity> searchBuffer{};
    const auto& filter = model.filter();
    const std::size_t searchLength = std::min(filter.search.size(), searchBuffer.size() - 1);
    std::copy_n(filter.search.data(), searchLength, searchBuffer.data());
    const auto snapshot = model.snapshot();
    std::vector<CatalogBrowserSourceOption> sourceOptions;
    std::vector<std::string> domainOptions;
    std::vector<const char*> domainLabels;
    std::vector<const char*> sectionLabels{"All sections"};
    int domainIndex = 0;
    int sourceIndex = 0;
    int sectionIndex = 0;
    const auto contextualFacets = model.contextualFacets();

    if (snapshot) {
        sourceOptions = buildCatalogBrowserSourceOptions(contextualFacets.sources);
        domainOptions = buildCatalogBrowserDomainOptions(contextualFacets.domains);
        domainLabels.reserve(domainOptions.size());
        for (const auto& option : domainOptions) {
            domainLabels.push_back(option.c_str());
        }
        for (std::size_t index = 0; index < contextualFacets.domains.size(); ++index) {
            if (contextualFacets.domains[index] == filter.domain) {
                domainIndex = static_cast<int>(index + 1);
            }
        }
        for (std::size_t index = 0; index < contextualFacets.sources.size(); ++index) {
            if (contextualFacets.sources[index].sourceId == filter.sourceId) {
                sourceIndex = static_cast<int>(index + 1);
            }
        }
        for (std::size_t index = 0; index < contextualFacets.sections.size(); ++index) {
            sectionLabels.push_back(contextualFacets.sections[index].c_str());
            if (contextualFacets.sections[index] == filter.section) {
                sectionIndex = static_cast<int>(index + 1);
            }
        }
    }
    if (domainLabels.empty()) {
        domainOptions = buildCatalogBrowserDomainOptions({});
        domainLabels.push_back(domainOptions.front().c_str());
    }

    std::vector<const char*> sourceLabels{"All sources"};
    sourceLabels.reserve(sourceOptions.size() + 1);
    for (const auto& source : sourceOptions) {
        sourceLabels.push_back(source.label.c_str());
    }

    if (filtersExpanded && ImGuiMCP::BeginTable(
            "FilterControls",
            2,
            ImGuiMCP::ImGuiTableFlags_SizingStretchProp)) {
        ImGuiMCP::TableSetupColumn(
            "FilterLabel", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 72.0F);
        ImGuiMCP::TableSetupColumn(
            "FilterControl", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);

        ImGuiMCP::TableNextRow();
        ImGuiMCP::TableSetColumnIndex(0);
        ImGuiMCP::AlignTextToFramePadding();
        ImGuiMCP::TextUnformatted("Domain");
        ImGuiMCP::TableSetColumnIndex(1);
        ImGuiMCP::SetNextItemWidth(-1.0F);
        if (ImGuiMCP::Combo(
                "##Domain",
                &domainIndex,
                domainLabels.data(),
                static_cast<int>(domainLabels.size()))) {
            model.setDomain(domainIndex == 0 ? "" : contextualFacets.domains[domainIndex - 1]);
        }

        ImGuiMCP::TableNextRow();
        ImGuiMCP::TableSetColumnIndex(0);
        ImGuiMCP::AlignTextToFramePadding();
        ImGuiMCP::TextUnformatted("Search");
        ImGuiMCP::TableSetColumnIndex(1);
        ImGuiMCP::SetNextItemWidth(-1.0F);
        if (ImGuiMCP::InputText("##Search", searchBuffer.data(), searchBuffer.size())) {
            model.setSearch(searchBuffer.data());
        }

        ImGuiMCP::TableNextRow();
        ImGuiMCP::TableSetColumnIndex(0);
        ImGuiMCP::AlignTextToFramePadding();
        ImGuiMCP::TextUnformatted("Source");
        ImGuiMCP::TableSetColumnIndex(1);
        ImGuiMCP::SetNextItemWidth(-1.0F);
        if (ImGuiMCP::Combo(
                "##Source",
                &sourceIndex,
                sourceLabels.data(),
                static_cast<int>(sourceLabels.size()))) {
            model.setSourceId(
                sourceIndex == 0 ? "" : sourceOptions[sourceIndex - 1].sourceId);
        }

        ImGuiMCP::TableNextRow();
        ImGuiMCP::TableSetColumnIndex(0);
        ImGuiMCP::AlignTextToFramePadding();
        ImGuiMCP::TextUnformatted("Section");
        ImGuiMCP::TableSetColumnIndex(1);
        ImGuiMCP::SetNextItemWidth(-1.0F);
        if (ImGuiMCP::Combo(
                "##Section",
                &sectionIndex,
                sectionLabels.data(),
                static_cast<int>(sectionLabels.size()))) {
            model.setSection(
                sectionIndex == 0 ? "" : contextualFacets.sections[sectionIndex - 1]);
        }
        ImGuiMCP::EndTable();
    }

    const auto& page = model.page();
    const auto emptyState = classifyCatalogBrowserEmptyState(snapshot != nullptr, page);
    const auto* style = ImGuiMCP::GetStyle();
    const float itemSpacing = style ? style->ItemSpacing.y : 4.0F;
    const float metadataHeight = calculateCatalogCardMetadataHeight(
        ImGuiMCP::GetTextLineHeightWithSpacing(), itemSpacing);
    const float footerHeight = ImGuiMCP::GetFrameHeightWithSpacing();
    const auto gridLayout = calculateCatalogBrowserGridLayout(
        ImGuiMCP::GetContentRegionAvail().y,
        footerHeight,
        metadataHeight,
        3);
    if (emptyState != CatalogBrowserEmptyState::none) {
        if (ImGuiMCP::BeginChild(
                "CatalogEmptyState",
                {0.0F, gridLayout.gridHeight},
                ImGuiMCP::ImGuiChildFlags_None,
                ImGuiMCP::ImGuiWindowFlags_NoScrollbar |
                    ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse)) {
            ImGuiMCP::TextUnformatted(catalogBrowserEmptyMessage(emptyState).data());
        }
        ImGuiMCP::EndChild();
    } else {
        constexpr std::size_t columnCount = 2;
        constexpr auto tableFlags =
            ImGuiMCP::ImGuiTableFlags_SizingStretchSame |
            ImGuiMCP::ImGuiTableFlags_BordersInner |
            ImGuiMCP::ImGuiTableFlags_NoPadOuterX |
            ImGuiMCP::ImGuiTableFlags_NoPadInnerX;
        if (ImGuiMCP::BeginTable(
                "TattooCards",
                static_cast<int>(columnCount),
                tableFlags,
                {0.0F, gridLayout.gridHeight})) {
            for (std::size_t index = 0; index < pickerVisibleCardCount(page); ++index) {
                const auto gridPosition = catalogCardGridPosition(index, columnCount);
                if (gridPosition.column == 0) {
                    ImGuiMCP::TableNextRow(0, gridLayout.rowHeight);
                }
                ImGuiMCP::TableSetColumnIndex(static_cast<int>(gridPosition.column));
                const auto& tattoo = page.entries[index];
                const auto domainLabel = domainThumbnailBadgeLabel(tattoo.domain);
                const auto inUseSlots = workflow.inUseSlots(tattoo);
                const auto thumbnailIndex = findCatalogThumbnailViewIndex(
                    tattoo.texturePath,
                    thumbnailViews);
                const NativeThumbnailView* thumbnail = thumbnailIndex
                    ? &thumbnailViews[*thumbnailIndex]
                    : nullptr;

                const auto thumbnailWidgetId = catalogCardWidgetId(
                    "Thumbnail", tattoo.sourceId, tattoo.sourceIndex);

                ImGuiMCP::PushStyleColor(
                    ImGuiMCP::ImGuiCol_ChildBg,
                    ImGuiMCP::ImVec4(0.08F, 0.08F, 0.08F, 1.0F));
                if (ImGuiMCP::BeginChild(
                        thumbnailWidgetId.c_str(),
                        {0.0F, std::max(1.0F, gridLayout.thumbnailHeight)},
                        ImGuiMCP::ImGuiChildFlags_Border,
                            ImGuiMCP::ImGuiWindowFlags_NoScrollbar |
                            ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse)) {
                    const auto imageRegion = ImGuiMCP::GetContentRegionAvail();
                    const auto imageOrigin = ImGuiMCP::GetCursorPos();
                    const auto imageScreenOrigin = ImGuiMCP::GetCursorScreenPos();
                    if (thumbnail && thumbnail->status == NativeThumbnailStatus::ready &&
                        thumbnail->texture && thumbnail->texture->shaderResourceView) {
                        const auto texture = thumbnail->texture;
                        const auto fit = fitCatalogThumbnail(
                            texture->width,
                            texture->height,
                            imageRegion.x,
                            imageRegion.y);
                        ImGuiMCP::SetCursorPos({
                            imageOrigin.x + (imageRegion.x - fit.width) / 2.0F,
                            imageOrigin.y + (imageRegion.y - fit.height) / 2.0F,
                        });
                        ImGuiMCP::Image(
                            static_cast<ImGuiMCP::ImTextureID>(
                                texture->shaderResourceView.get()),
                            {fit.width, fit.height});
                    } else if (thumbnail) {
                        const auto label = catalogThumbnailStatusLabel(thumbnail->status);
                        if (!label.empty()) {
                            ImGuiMCP::TextUnformatted(label.data());
                        }
                    }

                    if (!inUseSlots.empty()) {
                        constexpr const char* badgeText = "In Use";
                        const auto textSize = ImGuiMCP::CalcTextSize(badgeText);
                        const auto badge = calculateCatalogBadgeLayout(
                            imageRegion.x,
                            textSize.x,
                            textSize.y,
                            6.0F,
                            3.0F,
                            4.0F);
                        auto* drawList = ImGuiMCP::GetWindowDrawList();
                        ImGuiMCP::ImDrawListManager::AddRectFilled(
                            drawList,
                            {
                                imageScreenOrigin.x + badge.x,
                                imageScreenOrigin.y + badge.y,
                            },
                            {
                                imageScreenOrigin.x + badge.x + badge.width,
                                imageScreenOrigin.y + badge.y + badge.height,
                            },
                            0xB8000000,
                            3.0F,
                            0);
                        ImGuiMCP::ImDrawListManager::AddText(
                            drawList,
                            {
                                imageScreenOrigin.x + badge.textX,
                                imageScreenOrigin.y + badge.textY,
                            },
                            0xFFFFFFFF,
                            badgeText);
                    }
                    if (!domainLabel.empty()) {
                        const auto domainTextSize = ImGuiMCP::CalcTextSize(domainLabel.data());
                        auto* drawList = ImGuiMCP::GetWindowDrawList();
                        ImGuiMCP::ImDrawListManager::AddRectFilled(
                            drawList,
                            {
                                imageScreenOrigin.x + 4.0F,
                                imageScreenOrigin.y + 4.0F,
                            },
                            {
                                imageScreenOrigin.x + domainTextSize.x + 16.0F,
                                imageScreenOrigin.y + domainTextSize.y + 14.0F,
                            },
                            0xB8000000,
                            3.0F,
                            0);
                        ImGuiMCP::ImDrawListManager::AddText(
                            drawList,
                            {imageScreenOrigin.x + 10.0F, imageScreenOrigin.y + 8.0F},
                            0xFFFFFFFF,
                            domainLabel.data());
                    }
                }
                ImGuiMCP::EndChild();
                ImGuiMCP::PopStyleColor();
                if (ImGuiMCP::IsItemHovered()) {
                    const auto tooltip = formatCatalogTattooTooltip(tattoo.name, inUseSlots);
                    ImGuiMCP::SetTooltip("%s", tooltip.c_str());
                }
                if (ImGuiMCP::IsItemClicked()) {
                    workflow.selectTattoo(tattoo);
                }
            }
            ImGuiMCP::EndTable();
        }
    }

    const float horizontalButtonPadding =
        style ? style->FramePadding.x * 2.0F : 16.0F;
    const float cancelButtonWidth =
        ImGuiMCP::CalcTextSize("Cancel").x + horizontalButtonPadding;
    const float closeButtonWidth =
        ImGuiMCP::CalcTextSize("Close").x + horizontalButtonPadding;
    const float actionSpacing = style ? style->ItemSpacing.x : 8.0F;
    const float footerActionWidth =
        cancelButtonWidth + actionSpacing + closeButtonWidth;
    if (ImGuiMCP::BeginTable(
            "CatalogFooter",
            2,
            ImGuiMCP::ImGuiTableFlags_SizingStretchProp |
                ImGuiMCP::ImGuiTableFlags_NoPadOuterX)) {
        ImGuiMCP::TableSetupColumn(
            "Pagination", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
        ImGuiMCP::TableSetupColumn(
            "PickerActions",
            ImGuiMCP::ImGuiTableColumnFlags_WidthFixed,
            footerActionWidth);
        ImGuiMCP::TableNextRow();
        ImGuiMCP::TableSetColumnIndex(0);

        const bool hasPages = page.pageCount != 0;
        ImGuiMCP::BeginDisabled(!hasPages || page.pageIndex == 0);
        if (ImGuiMCP::Button("Prev")) {
            model.previousPage();
        }
        ImGuiMCP::EndDisabled();

        ImGuiMCP::SameLine();
        ImGuiMCP::TextUnformatted("Page");
        ImGuiMCP::SameLine();
        static CatalogBrowserPageInputState pageInputState;
        pageInputState.synchronize(page.pageIndex, page.pageCount);
        ImGuiMCP::SetNextItemWidth(56.0F);
        ImGuiMCP::InputInt(
            "##PageNumber",
            &pageInputState.pendingPageNumber(),
            0,
            0);
        const bool itemActive = ImGuiMCP::IsItemActive();
        const bool committedOnDeactivate = ImGuiMCP::IsItemDeactivatedAfterEdit();
        if (const auto requestedPage = pageInputState.finishFrame(
                itemActive, false, committedOnDeactivate)) {
            model.setPageNumber(*requestedPage);
        }
        ImGuiMCP::SameLine();
        ImGuiMCP::Text("/ %zu", page.pageCount);
        ImGuiMCP::SameLine();
        ImGuiMCP::BeginDisabled(!hasPages || page.pageIndex + 1 >= page.pageCount);
        if (ImGuiMCP::Button("Next")) {
            model.nextPage();
        }
        ImGuiMCP::EndDisabled();

        ImGuiMCP::TableSetColumnIndex(1);
        const auto actionLayout = calculatePickerFooterActionLayout(
            ImGuiMCP::GetContentRegionAvail().x,
            cancelButtonWidth,
            closeButtonWidth,
            actionSpacing);
        const float actionOriginX = ImGuiMCP::GetCursorPosX();
        ImGuiMCP::SetCursorPosX(actionOriginX + actionLayout.cancelX);
        if (ImGuiMCP::Button("Cancel")) {
            workflow.backToSlots();
        }
        ImGuiMCP::SameLine();
        ImGuiMCP::SetCursorPosX(actionOriginX + actionLayout.closeX);
        if (ImGuiMCP::Button("Close")) {
            open = false;
        }
        ImGuiMCP::EndTable();
    }
    ImGuiMCP::End();
    ImGuiMCP::PopStyleVar();
    if (!open && close) {
        close();
    }
}

}  // namespace stui::native
