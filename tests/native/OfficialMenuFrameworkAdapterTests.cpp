#include "native/OfficialMenuFrameworkAdapter.h"
#include "native/NativeCatalogBrowserModel.h"
#include "native/NativeThumbnailController.h"
#include "native/NativeSlotWorkflowModel.h"
#include "native/NativeSlotWorkflowRuntime.h"
#include "core/TattooModels.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void actorTargetIdentityUsesFixedWidthUppercaseFormId() {
    using namespace stui::native;
    expect(formatActorTargetIdentity({ActorTargetKind::crosshair, 0xA2C8E, "Lydia"}) ==
               "Lydia [0x000A2C8E]",
        "expected exact actor name and eight-digit uppercase form ID");
    expect(formatActorTargetIdentity(ActorTarget{}) == "Player [0x00000014]",
        "expected Player identity to retain leading zeroes");
    expect(formatActorTargetIdentity({ActorTargetKind::crosshair, 0xFFFFFFFF, ""}) ==
               "Unnamed Actor [0xFFFFFFFF]",
        "expected fallback actor name and full-width form ID");
    const ActorTarget player;
    expect(currentTattoosTitle(&player) == "Current Tattoos - Player",
        "expected Current Slots title to identify Player");
    const ActorTarget lydia{ActorTargetKind::crosshair, 0xA2C8E, "Lydia"};
    expect(currentTattoosTitle(&lydia) == "Current Tattoos - Lydia",
        "expected Current Slots title to use the selected Actor name");
    const ActorTarget unnamed{ActorTargetKind::crosshair, 0x1234, ""};
    expect(currentTattoosTitle(&unnamed) == "Current Tattoos - Unnamed Actor" &&
               currentTattoosTitle(nullptr) == "Current Tattoos",
        "expected unnamed and missing targets never to use a Player title");
}

void actorTargetStatusAndActionAvailabilityRequireResolvedIdentity() {
    using namespace stui::native;
    const ActorTarget actor;
    expect(actorTargetStatusLabel(true, nullptr) == "Resolving target..." &&
               actorTargetStatusLabel(true, &actor) == "Resolving target...",
        "expected resolution status to take precedence over an identity");
    expect(actorTargetStatusLabel(false, nullptr) == "No valid crosshair Actor",
        "expected explicit missing-target status");
    expect(actorTargetStatusLabel(false, &actor).empty(),
        "expected no target error for a resolved Actor");
    expect(actorTargetActionsEnabled(false, &actor), "expected resolved Actor actions");
    expect(!actorTargetActionsEnabled(true, &actor) &&
               !actorTargetActionsEnabled(true, nullptr) &&
               !actorTargetActionsEnabled(false, nullptr),
        "expected slot and mutation actions disabled without a resolved Actor");
}

void editAppearanceHeaderAppendsLivePreviewStatusToActorIdentity() {
    using namespace stui::native;
    const ActorTarget player;
    const ActorTarget lydia{ActorTargetKind::crosshair, 0xA2C8E, "Lydia"};
    expect(formatActorTargetIdentityWithPreviewStatus(
               false, &player, LivePreviewStatus::pending) ==
               "Player [0x00000014] - Preview pending...",
        "expected pending preview status after the Player identity");
    expect(formatActorTargetIdentityWithPreviewStatus(
               false, &lydia, LivePreviewStatus::applied) ==
               "Lydia [0x000A2C8E] - Preview applied",
        "expected applied preview status after the crosshair target identity");
    expect(formatActorTargetIdentityWithPreviewStatus(
               false, &player, LivePreviewStatus::clean) ==
               "Player [0x00000014]",
        "expected clean preview state not to add redundant text");
    expect(formatActorTargetIdentityWithPreviewStatus(
               true, &player, LivePreviewStatus::pending) ==
               "Resolving target... - Preview pending...",
        "expected target resolution status to remain visible with preview status");
}

void actorTargetControlsRespectMutationAndCrosshairMode() {
    using namespace stui::native;
    for (const auto kind : {ActorTargetKind::player, ActorTargetKind::crosshair}) {
        for (const bool resolving : {false, true}) {
            const auto idle = actorTargetControlPresentation(kind, resolving, false);
            expect(idle.playerEnabled && idle.crosshairEnabled,
                "expected explicit target selection while no mutation is in flight");
            expect(idle.refreshVisible == (kind == ActorTargetKind::crosshair),
                "expected Refresh only in Crosshair mode");
            expect(idle.refreshEnabled == (kind == ActorTargetKind::crosshair && !resolving),
                "expected Refresh to wait for existing target resolution");
            const auto busy = actorTargetControlPresentation(kind, resolving, true);
            expect(!busy.playerEnabled && !busy.crosshairEnabled && !busy.refreshEnabled,
                "expected every target control disabled during mutation");
            expect(busy.refreshVisible == (kind == ActorTargetKind::crosshair),
                "expected mutation not to change Refresh visibility");
        }
    }
}

void actorTargetHeaderIntentsStopFramesOnlyAfterAcceptedChanges() {
    using namespace stui::native;
    NativeCatalogBrowserModel catalog([] { return nullptr; });
    NativeSlotWorkflowModel workflow(catalog);
    workflow.start();
    const auto initial = workflow.takeSlotQuery();
    expect(initial.has_value(), "expected initial query");
    workflow.completeSlotQuery(initial->generation, stui::core::TattooSlots{
        .actorFormId = 0x14, .area = stui::core::TattooArea::body,
        .configuredCount = 1,
        .slots = {{.index = 0, .occupancy = stui::core::SlotOccupancy::slaveTats,
            .tattoo = stui::core::TattooEntry{.runtimeHandle = 73, .slot = 0}}},
    });
    expect(workflow.selectSlot(0) && workflow.beginEditAppearance(),
        "expected active edit session before target input");
    expect(!applyActorTargetHeaderIntent(workflow, ActorTargetHeaderIntent::none) &&
               !applyActorTargetHeaderIntent(workflow, ActorTargetHeaderIntent::refresh),
        "expected no-op and hidden Refresh to keep the current frame");
    expect(workflow.editAppearance(), "expected no-op header to preserve edit session");
    expect(applyActorTargetHeaderIntent(workflow, ActorTargetHeaderIntent::crosshair),
        "expected accepted Crosshair input to end the old frame");
    expect(!workflow.editAppearance() && !workflow.slots() && !workflow.actorTarget() &&
               workflow.screen() == SlotWorkflowScreen::currentSlots,
        "expected accepted target input to invalidate all captured actor presentation");
    expect(!applyActorTargetHeaderIntent(workflow, ActorTargetHeaderIntent::refresh),
        "expected resolving Refresh input rejected");
    auto resolution = workflow.takeActorTargetRequest();
    expect(resolution.has_value(), "expected explicit Crosshair resolution");
    workflow.completeActorTargetResolution(resolution->generation,
        ActorTarget{ActorTargetKind::crosshair, 0xA2C8E, "Lydia"});
    expect(applyActorTargetHeaderIntent(workflow, ActorTargetHeaderIntent::refresh) &&
               !workflow.actorTarget(),
        "expected Refresh to invalidate the previous NPC identity");
    expect(applyActorTargetHeaderIntent(workflow, ActorTargetHeaderIntent::player) &&
               workflow.actorTarget() && workflow.actorTarget()->formId == 0x14,
        "expected explicit Player input to restore exactly Player");
    const auto playerQuery = workflow.takeSlotQuery();
    expect(playerQuery.has_value(), "expected query after explicit Player selection");
    workflow.completeSlotQuery(playerQuery->generation, stui::core::TattooSlots{
        .actorFormId = 0x14, .area = stui::core::TattooArea::body,
        .configuredCount = 1,
        .slots = {{.index = 0, .occupancy = stui::core::SlotOccupancy::slaveTats,
            .tattoo = stui::core::TattooEntry{.runtimeHandle = 73, .slot = 0}}},
    });
    expect(workflow.toggleSlotLock(0), "expected pending mutation fixture");
    for (const auto intent : {ActorTargetHeaderIntent::player,
             ActorTargetHeaderIntent::crosshair, ActorTargetHeaderIntent::refresh}) {
        expect(!applyActorTargetHeaderIntent(workflow, intent),
            "expected header input rejected while mutation is pending");
    }
    expect(workflow.actorTarget() && workflow.actorTarget()->formId == 0x14 && workflow.slots(),
        "expected rejected header input to preserve the mutation Actor and snapshot");
}

float returnVersionThree() {
    return 3.13F;
}

struct FakeWindow {
    bool open{};
    bool blocking{true};
};

FakeWindow g_window;
std::string g_itemPath;
stui::native::MenuCallback g_itemCallback{};

void addSectionItem(const char* path, stui::native::MenuCallback callback) {
    g_itemPath = path;
    g_itemCallback = callback;
}

void* addWindow(stui::native::MenuCallback) {
    return &g_window;
}

void setWindowOpen(void* window, bool open) noexcept {
    static_cast<FakeWindow*>(window)->open = open;
}

bool isWindowOpen(const void* window) noexcept {
    return static_cast<const FakeWindow*>(window)->open;
}

void setWindowBlocking(void* window, bool blocking) noexcept {
    static_cast<FakeWindow*>(window)->blocking = blocking;
}

stui::native::MenuFrameworkBindings completeBindings() {
    return {
        &returnVersionThree,
        &addSectionItem,
        &addWindow,
        &setWindowOpen,
        &isWindowOpen,
        &setWindowBlocking,
    };
}

void rejectsIncompleteExportTable() {
    stui::native::MenuFrameworkBindings bindings{};
    bindings.getVersion = &returnVersionThree;

    const stui::native::OfficialMenuFrameworkAdapter adapter(bindings);

    expect(!adapter.available(), "expected incomplete export table rejection");
}

void translatesSectionAndNonPausingWindowState() {
    g_window = {};
    g_window.blocking = true;
    g_itemPath.clear();
    g_itemCallback = nullptr;
    stui::native::OfficialMenuFrameworkAdapter adapter(completeBindings());

    expect(adapter.available(), "expected complete bindings");
    expect(adapter.setSection("SlaveTatsUI").has_value(), "expected section setup");
    auto window = adapter.addWindow(nullptr, false);
    expect(window.has_value(), "expected opaque window token");
    expect(adapter.addSectionItem("Tattoo Browser", nullptr).has_value(),
           "expected browser item registration");
    adapter.setWindowOpen(*window, true);

    expect(g_itemPath == "SlaveTatsUI/Tattoo Browser", "expected composed item path");
    expect(!g_window.blocking, "expected non-pausing window");
    expect(adapter.isWindowOpen(*window), "expected translated open state");
}

void defaultAdapterIsUnavailableWithoutLoadedFramework() {
    const stui::native::OfficialMenuFrameworkAdapter adapter;

    expect(!adapter.available(), "expected absent runtime framework to remain optional");
    expect(adapter.version() == 0.0F, "expected zero version without loaded framework");
}

void foundationLayoutAnchorsFortyPercentPanelToRightEdge() {
    const auto layout = stui::native::OfficialMenuFrameworkAdapter::calculateFoundationLayout(
        {10.0F, 20.0F}, {1000.0F, 800.0F});

    expect(layout.position.x == 590.0F && layout.position.y == 40.0F,
           "expected right-edge position with margin");
    expect(layout.size.width == 400.0F && layout.size.height == 760.0F,
           "expected forty-percent full-height side panel");
}

void thumbnailPresentationFitsWithoutStretchingAndLabelsFailures() {
    const auto wide = stui::native::fitCatalogThumbnail(400, 200, 180.0F, 140.0F);
    expect(wide.width == 180.0F && wide.height == 90.0F,
           "expected wide texture fitted without stretching");

    const auto tall = stui::native::fitCatalogThumbnail(100, 400, 180.0F, 140.0F);
    expect(tall.width == 35.0F && tall.height == 140.0F,
           "expected tall texture fitted without stretching");

    expect(stui::native::catalogThumbnailStatusLabel(
               stui::native::NativeThumbnailStatus::loading) == "Loading",
           "expected explicit loading label");
    expect(stui::native::catalogThumbnailStatusLabel(
               stui::native::NativeThumbnailStatus::missing) == "Missing",
           "expected explicit missing label");
    expect(stui::native::catalogThumbnailStatusLabel(
               stui::native::NativeThumbnailStatus::broken) == "Broken",
           "expected explicit broken label");
    expect(stui::native::catalogThumbnailStatusLabel(
               stui::native::NativeThumbnailStatus::ready).empty(),
           "expected ready texture without a status label");
    expect(stui::native::catalogThumbnailStatusLabel(
               stui::native::NativeThumbnailStatus::placeholder).empty(),
           "expected placeholder without a status label");
}

void thumbnailViewLookupMapsDuplicatePathVariantsToOneView() {
    const std::vector<stui::native::NativeThumbnailView> views{
        {.texturePath = "textures/actors/character/slavetats/a.dds"},
        {.texturePath = "textures/actors/character/slavetats/b.dds"},
    };

    expect(stui::native::findCatalogThumbnailViewIndex(
               "textures/actors/character/slavetats/a.dds", views) == 0,
           "expected first entry to map to the first thumbnail view");
    expect(stui::native::findCatalogThumbnailViewIndex(
               "TEXTURES\\ACTORS\\CHARACTER\\SLAVETATS\\A.DDS", views) == 0,
           "expected slash and case variant to reuse the first thumbnail view");
    expect(stui::native::findCatalogThumbnailViewIndex(
               "textures/actors/character/slavetats/b.dds", views) == 1,
           "expected third entry to map to the second unique thumbnail view");
}

void thumbnailGridGroupsTwoCardsIntoEachRow() {
    const auto first = stui::native::catalogCardGridPosition(0, 2);
    const auto second = stui::native::catalogCardGridPosition(1, 2);
    const auto third = stui::native::catalogCardGridPosition(2, 2);

    expect(first.row == 0 && first.column == 0,
        "expected first card in row zero column zero");
    expect(second.row == 0 && second.column == 1,
        "expected second card beside first card in row zero");
    expect(third.row == 1 && third.column == 0,
        "expected third card at the start of row one");
}

void thumbnailCardWidgetsHaveStableUniqueIds() {
    expect(stui::native::catalogCardWidgetId("Thumbnail", "source-a.json", 7) ==
               "Thumbnail##source-a.json:7",
        "expected thumbnail ID derived from stable catalog identity");
    expect(stui::native::catalogCardWidgetId("Thumbnail", "source-a.json", 7) !=
               stui::native::catalogCardWidgetId("Thumbnail", "source-b.json", 7),
        "expected equal source indices from different files to remain unique");
}

void favoriteButtonsUseDistinctStarGlyphs() {
    expect(stui::native::catalogFavoriteButtonGlyph(false) == "\xE2\x98\x86",
        "expected unselected favorite action to use an outline star glyph");
    expect(stui::native::catalogFavoriteButtonGlyph(true) == "\xE2\x98\x85",
        "expected selected favorite action to use a filled star glyph");
}

void browserGridUsesRemainingHeightWithoutVerticalScrolling() {
    const auto collapsed = stui::native::calculateCatalogBrowserGridLayout(
        700.0F, 40.0F, 60.0F, 3);
    expect(collapsed.gridHeight == 660.0F,
        "expected footer height reserved below the collapsed-filter grid");
    expect(collapsed.rowHeight == 220.0F && collapsed.thumbnailHeight == 160.0F,
        "expected three equal image-first rows with collapsed filters");

    const auto filterOverlay = stui::native::calculateCatalogBrowserGridLayout(
        700.0F, 40.0F, 60.0F, 3);
    expect(filterOverlay.gridHeight == collapsed.gridHeight &&
            filterOverlay.thumbnailHeight == collapsed.thumbnailHeight,
        "expected filter overlay to preserve the thumbnail grid height");

    const auto constrained = stui::native::calculateCatalogBrowserGridLayout(
        80.0F, 40.0F, 60.0F, 3);
    expect(constrained.thumbnailHeight == 0.0F,
        "expected small windows to clamp thumbnail height instead of going negative");
}

void catalogBadgeAnchorsInsideThumbnailTopRightCorner() {
    const auto badge = stui::native::calculateCatalogBadgeLayout(
        200.0F, 40.0F, 16.0F, 6.0F, 3.0F, 4.0F);

    expect(badge.x == 144.0F && badge.y == 4.0F,
        "expected area badge anchored four pixels from the thumbnail top-right");
    expect(badge.width == 52.0F && badge.height == 22.0F,
        "expected area badge padding around its text");
    expect(badge.textX == 150.0F && badge.textY == 7.0F,
        "expected area text inset inside the badge background");
}

void thumbnailCardsReserveNoPersistentMetadataRow() {
    const float metadataHeight =
        stui::native::calculateCatalogCardMetadataHeight(24.0F, 4.0F);
    expect(metadataHeight == 0.0F,
        "expected thumbnail cards to reserve no persistent name row");

    const auto layout = stui::native::calculateCatalogBrowserGridLayout(
        700.0F, 40.0F, metadataHeight, 3);
    expect(layout.thumbnailHeight == 220.0F,
        "expected each thumbnail to use the full available row height");
}

void footerControlAlignsToRightContentEdge() {
    expect(stui::native::calculateRightAlignedControlX(300.0F, 64.0F) == 236.0F,
        "expected close button aligned to the footer right edge");
    expect(stui::native::calculateRightAlignedControlX(40.0F, 64.0F) == 0.0F,
        "expected constrained footer alignment clamped inside its cell");
}

void unifiedFooterKeepsCloseAtRightEdge() {
    const auto layout = stui::native::calculateUnifiedFooterLayout(300.0F, 64.0F);

    expect(layout.actionWidth == 236.0F,
        "expected workflow actions to stop before the fixed Close column");
    expect(layout.closeWidth == 64.0F,
        "expected Close column to preserve its requested width");
    expect(layout.closeX == 236.0F,
        "expected Close to start at the same right-aligned position on every screen");

    const auto constrained = stui::native::calculateUnifiedFooterLayout(40.0F, 64.0F);
    expect(constrained.actionWidth == 0.0F && constrained.closeWidth == 40.0F &&
            constrained.closeX == 0.0F,
        "expected constrained unified footer to keep Close inside available content");
}

void pinnedFooterReservesTheBottomRowAfterShortContent() {
    expect(stui::native::calculatePinnedFooterY(220.0F, 180.0F, 24.0F) == 376.0F,
        "expected a short content region to place its footer at the bottom row");
    expect(stui::native::calculatePinnedFooterY(220.0F, 12.0F, 24.0F) == 220.0F,
        "expected an overflowing footer never to move above the current cursor");
}

void pickerFooterActionsStayRightAlignedInNavigationOrder() {
    const auto layout = stui::native::calculatePickerFooterActionLayout(
        200.0F,
        60.0F,
        50.0F,
        8.0F);

    expect(layout.groupWidth == 118.0F,
        "expected Cancel and Close widths plus one spacing interval");
    expect(layout.cancelX == 82.0F,
        "expected footer action group aligned to the right edge");
    expect(layout.closeX == 150.0F,
        "expected Close after Cancel in navigation order");
}

void tattooColorComponentsPreserveRgbChannelOrder() {
    const auto components = stui::native::tattooColorComponents(0x804020);

    expect(components.red == 128.0F / 255.0F &&
            components.green == 64.0F / 255.0F &&
            components.blue == 32.0F / 255.0F,
        "expected 0xRRGGBB unpacked without swapping color channels");
    expect(stui::native::tattooColorValue({1.0F, 128.0F / 255.0F, 0.0F}) ==
            0xFF8000,
        "expected RGB picker components rounded into 0xRRGGBB");

    const auto glow = stui::native::tattooColorComponents(0x102030);
    expect(stui::native::tattooColorValue(glow) == 0x102030,
        "expected glow RGB components to round-trip without swapping channels");
}

void editAppearanceUsesSessionStateForSaveAndThumbnailPresentation() {
    const stui::native::AppearanceEditSession unchangedSession{
        .texturePath = "textures/tattoos/unchanged.dds",
        .original = {.color = 0x123456, .alpha = 0.25F},
        .edited = {.color = 0x123456, .alpha = 0.25F},
    };
    const stui::native::AppearanceEditSession changedSession{
        .texturePath = "textures/tattoos/edited.dds",
        .glowTexture = "textures/tattoos/edited_g.dds",
        .bump = "textures/tattoos/edited_n.dds",
        .original = {
            .color = 0x123456,
            .alpha = 0.25F,
            .glow = 0x102030,
            .glossiness = 2.5F,
            .specularStrength = 1.25F,
            .emissiveMult = 3.0F,
        },
        .edited = {
            .color = 0x804020,
            .alpha = 0.75F,
            .glow = 0x102030,
            .glossiness = 2.5F,
            .specularStrength = 1.25F,
            .emissiveMult = 3.0F,
        },
    };
    auto materialChangedSession = changedSession;
    materialChangedSession.edited = materialChangedSession.original;
    materialChangedSession.edited.glow = 0x203040;

    expect(stui::native::isAppearanceSaveEnabled(
               stui::native::SlotWorkflowScreen::editAppearance,
               &unchangedSession),
        "unchanged appearance must allow Save to exit without writing");
    expect(stui::native::isAppearanceSaveEnabled(
               stui::native::SlotWorkflowScreen::editAppearance,
               &changedSession),
        "changed appearance must enable Save");
    expect(stui::native::isAppearanceSaveEnabled(
               stui::native::SlotWorkflowScreen::editAppearance,
               &materialChangedSession),
        "material-only edits must enable Save");
    expect(!stui::native::isAppearanceSaveEnabled(
               stui::native::SlotWorkflowScreen::savingAppearance,
               &changedSession),
        "saving appearance must disable duplicate Save");

    expect(stui::native::appearanceTextureMetadata(changedSession.glowTexture) ==
               "textures/tattoos/edited_g.dds" &&
               stui::native::appearanceTextureMetadata(changedSession.bump) ==
               "textures/tattoos/edited_n.dds" &&
               stui::native::appearanceTextureMetadata("") == "None",
        "expected advanced texture metadata to remain read-only and show None when absent");

    const auto thumbnail = stui::native::editAppearanceThumbnailPresentation(&changedSession);
    expect(thumbnail && thumbnail->color.red == 128.0F / 255.0F &&
               thumbnail->color.green == 64.0F / 255.0F &&
               thumbnail->color.blue == 32.0F / 255.0F,
        "expected edit thumbnail tint to use the session edited RGB color");
    expect(thumbnail->alpha == 0.75F,
        "expected edit thumbnail tint to use the session edited alpha");
    expect(thumbnail->texturePath == "textures/tattoos/edited.dds",
        "expected edit thumbnail to use the session texture path");

    expect(!stui::native::editAppearanceThumbnailPresentation(nullptr),
        "expected cancelled edit sessions to stop thumbnail presentation safely");

    auto currentFrameSession = changedSession;
    currentFrameSession.edited = {.color = 0x204080, .alpha = 0.5F};
    const auto currentFrameThumbnail =
        stui::native::editAppearanceThumbnailPresentation(&currentFrameSession);
    expect(currentFrameThumbnail &&
               currentFrameThumbnail->color.red == 32.0F / 255.0F &&
               currentFrameThumbnail->color.green == 64.0F / 255.0F &&
               currentFrameThumbnail->color.blue == 128.0F / 255.0F &&
               currentFrameThumbnail->alpha == 0.5F,
        "expected thumbnail presentation to refresh from the current frame edit values");

    const auto save = stui::native::appearanceSavePresentation(
        stui::native::SlotWorkflowScreen::editAppearance,
        &changedSession);
    expect(save.label == "Save" && save.enabled,
        "expected full-update edits to present an enabled Save action");
    expect(stui::native::isAppearanceEditingEnabled(
               stui::native::SlotWorkflowScreen::editAppearance,
               &changedSession),
        "expected full-update appearance controls to remain enabled");

    auto retrySession = changedSession;
    retrySession.mode = stui::core::UpdateTattooAppearanceMode::synchronizeOnly;
    const auto retry = stui::native::appearanceSavePresentation(
        stui::native::SlotWorkflowScreen::editAppearance,
        &retrySession);
    expect(retry.label == "Save" && retry.enabled,
        "expected Save label to stay separate from operation retry");
    expect(!stui::native::isAppearanceEditingEnabled(
               stui::native::SlotWorkflowScreen::editAppearance,
               &retrySession),
        "expected synchronize-only retry to disable color and alpha controls");

    const auto savingRetry = stui::native::appearanceSavePresentation(
        stui::native::SlotWorkflowScreen::savingAppearance,
        &retrySession);
    expect(savingRetry.label == "Save" && !savingRetry.enabled,
        "expected saving appearance to disable duplicate Save");
}

void editAppearanceLayoutHidesEmptyMetadataAndUsesExpandedRanges() {
    using namespace stui::native;
    expect(!shouldShowAppearanceTextureMetadata("") &&
               shouldShowAppearanceTextureMetadata("textures/tattoos/glow.dds"),
        "expected empty texture metadata to stay hidden");

    const auto spacious = calculateEditAppearanceThumbnailLayout(400.0F, 300.0F, 30.0F);
    expect(spacious.size == 160.0F && spacious.xOffset == 120.0F,
        "expected a centered 160-pixel square thumbnail when space permits");
    const auto narrow = calculateEditAppearanceThumbnailLayout(120.0F, 300.0F, 30.0F);
    expect(narrow.size == 120.0F && narrow.xOffset == 0.0F,
        "expected thumbnail size to fit narrow content without overflow");
    const auto shortLayout = calculateEditAppearanceThumbnailLayout(400.0F, 100.0F, 30.0F);
    expect(shortLayout.size == 70.0F && shortLayout.xOffset == 165.0F,
        "expected thumbnail size to preserve footer space");

    const auto ranges = editAppearanceControlRanges();
    expect(ranges.glossinessMax == 1000.0F && ranges.specularStrengthMax == 100.0F,
        "expected expanded material slider ranges");
}

void editAppearanceRendererOrchestrationOrdersInputAndCancel() {
    stui::native::NativeCatalogBrowserModel catalog([] { return nullptr; });
    stui::native::NativeSlotWorkflowModel workflow(catalog);
    workflow.start();
    const auto query = workflow.takeSlotQuery();
    expect(query.has_value(), "expected initial slot query for renderer fixture");
    workflow.completeSlotQuery(query->generation, stui::core::TattooSlots{
        .actorFormId = 0x14,
        .area = stui::core::TattooArea::body,
        .configuredCount = 1,
        .slots = {{
            .index = 0,
            .occupancy = stui::core::SlotOccupancy::slaveTats,
            .tattoo = stui::core::TattooEntry{
                .runtimeHandle = 73,
                .texturePath = "textures/tattoos/current-frame.dds",
                .slot = 0,
                .color = 0x123456,
                .alpha = 0.25F,
                .glow = 0x102030,
                .glossiness = 2.5F,
                .specularStrength = 1.25F,
                .emissiveMult = 3.0F,
            },
        }},
    });
    expect(workflow.selectSlot(0) && workflow.beginEditAppearance(),
        "expected renderer fixture to open an edit session");

    std::vector<std::string> events;
    std::optional<stui::native::AppearanceThumbnailPresentation> thumbnail;
    stui::native::orchestrateEditAppearanceFrame(
        workflow,
        {
            .appearanceChanged = true,
            .color = 0x204080,
            .alpha = 0.5F,
            .glow = 0x406080,
            .glossiness = 3.5F,
            .specularStrength = 2.25F,
            .emissiveMult = 4.0F,
        },
        [&events] { events.emplace_back("teardown"); },
        [&events, &thumbnail](const auto& presentation) {
            events.emplace_back("thumbnail");
            thumbnail = presentation;
        });

    expect(events == std::vector<std::string>{"thumbnail"},
        "expected an active edit frame to continue into thumbnail presentation");
    expect(thumbnail &&
               thumbnail->texturePath == "textures/tattoos/current-frame.dds" &&
               thumbnail->color.red == 32.0F / 255.0F &&
               thumbnail->color.green == 64.0F / 255.0F &&
               thumbnail->color.blue == 128.0F / 255.0F &&
               thumbnail->alpha == 0.5F,
        "expected current-frame input applied before thumbnail presentation query");
    const auto* editedSession = workflow.editAppearance();
    expect(editedSession && editedSession->edited.glow == 0x406080 &&
               editedSession->edited.glossiness == 3.5F &&
               editedSession->edited.specularStrength == 2.25F &&
               editedSession->edited.emissiveMult == 4.0F,
        "expected frame interaction to forward every editable material value without mutation");

    events.clear();
    thumbnail.reset();
    bool sessionResetBeforeTeardown = false;
    stui::native::orchestrateEditAppearanceFrame(
        workflow,
        {.intent = stui::native::EditAppearanceIntent::cancel},
        [&workflow, &events, &sessionResetBeforeTeardown] {
            sessionResetBeforeTeardown = workflow.editAppearance() == nullptr;
            events.emplace_back("teardown");
        },
        [&events](const auto&) { events.emplace_back("continuation"); });

    expect(workflow.screen() == stui::native::SlotWorkflowScreen::slotActions &&
               !workflow.editAppearance(),
        "expected Cancel to reset the edit session and return to Slot Actions");
    expect(sessionResetBeforeTeardown &&
               events == std::vector<std::string>{"teardown"},
        "expected teardown before any post-Cancel renderer continuation");
}

void livePreviewPresentationMatchesTransactionState() {
    using namespace stui::native;
    expect(livePreviewStatusLabel(LivePreviewStatus::clean).empty(), "clean has no status");
    expect(livePreviewStatusLabel(LivePreviewStatus::pending) == "Preview pending...", "pending label");
    expect(livePreviewStatusLabel(LivePreviewStatus::updating) == "Updating preview...", "updating label");
    expect(livePreviewStatusLabel(LivePreviewStatus::applied) == "Preview applied", "applied label");
    expect(livePreviewStatusLabel(LivePreviewStatus::restoring) == "Restoring original appearance...", "restore label");
    expect(livePreviewStatusLabel(LivePreviewStatus::previewError).empty() &&
        livePreviewStatusLabel(LivePreviewStatus::restoreError).empty(), "errors use service messages");
    const auto absent = livePreviewPresentation(SlotWorkflowScreen::currentSlots, nullptr);
    expect(!absent.save.visible && !absent.cancel.visible && !absent.close.visible && !absent.retry.visible,
        "no session has no editor actions");
    AppearanceEditSession session;
    for (auto status : {LivePreviewStatus::clean, LivePreviewStatus::pending,
             LivePreviewStatus::updating, LivePreviewStatus::applied}) {
        session.status = status;
        const auto ui = livePreviewPresentation(SlotWorkflowScreen::editAppearance, &session);
        expect(ui.save.visible && ui.save.enabled && ui.cancel.enabled && ui.close.enabled && !ui.retry.visible,
            "editor allows one Save, Cancel or Close intent even while preview runs");
    }
    for (auto status : {LivePreviewStatus::previewError, LivePreviewStatus::restoreError}) {
        session.status = status;
        session.exitIntent = status == LivePreviewStatus::restoreError ? AppearanceExitIntent::close : AppearanceExitIntent::none;
        for (auto mode : {stui::core::UpdateTattooAppearanceMode::updateAndSynchronize,
                 stui::core::UpdateTattooAppearanceMode::synchronizeOnly}) {
            session.mode = mode;
            const auto ui = livePreviewPresentation(SlotWorkflowScreen::editAppearance, &session);
            const auto expected = status == LivePreviewStatus::restoreError
                ? (mode == stui::core::UpdateTattooAppearanceMode::synchronizeOnly ? "Retry Restore Sync" : "Retry Restore")
                : (mode == stui::core::UpdateTattooAppearanceMode::synchronizeOnly ? "Retry Preview Sync" : "Retry Preview");
            expect(ui.retry.visible && ui.retry.enabled && ui.retryLabel == expected && !ui.save.enabled,
                "retry matches failed purpose and preserves sync-only mode");
            expect(ui.cancel.enabled == (status == LivePreviewStatus::previewError) &&
                ui.close.enabled == (status == LivePreviewStatus::previewError), "restore error retains exit intent");
        }
    }
    for (auto intent : {AppearanceExitIntent::save, AppearanceExitIntent::cancel, AppearanceExitIntent::close}) {
        session.exitIntent = intent;
        session.status = intent == AppearanceExitIntent::save ? LivePreviewStatus::updating : LivePreviewStatus::restoring;
        const auto ui = livePreviewPresentation(SlotWorkflowScreen::editAppearance, &session);
        expect(!ui.save.enabled && !ui.cancel.enabled && !ui.close.enabled && !ui.retry.enabled,
            "accepted exit intent disables duplicate actions");
        expect(!isAppearanceEditingEnabled(SlotWorkflowScreen::editAppearance, &session), "exit blocks edits");
    }
}

struct LivePreviewRendererFixture {
    stui::native::NativeCatalogBrowserModel catalog{[] { return nullptr; }};
    stui::native::NativeSlotWorkflowModel workflow{catalog};

    LivePreviewRendererFixture() {
        using namespace stui::core;
        workflow.start();
        const auto query = workflow.takeSlotQuery();
        workflow.completeSlotQuery(query->generation, TattooSlots{
            .actorFormId = 0x14, .area = TattooArea::body, .configuredCount = 1,
            .slots = {{.index = 0, .occupancy = SlotOccupancy::slaveTats,
                .tattoo = TattooEntry{.runtimeHandle = 73, .texturePath = "preview.dds", .slot = 0}}}});
        expect(workflow.selectSlot(0) && workflow.beginEditAppearance(), "editor fixture");
        workflow.setEditedAppearance(0x123456, 0.5F);
    }
};

void livePreviewFrameRoutesOneIntentAndDefersClose() {
    using namespace stui::native;
    using namespace stui::core;
    using namespace std::chrono_literals;
    for (auto intent : {EditAppearanceIntent::save, EditAppearanceIntent::cancel, EditAppearanceIntent::close,
             EditAppearanceIntent::retry}) {
        LivePreviewRendererFixture fixture;
        auto& workflow = fixture.workflow;
        workflow.advanceLivePreview(std::chrono::steady_clock::time_point{});
        workflow.advanceLivePreview(std::chrono::steady_clock::time_point{} + 1000ms);
        const auto targetControls = actorTargetControlPresentation(
            workflow.selectedTargetKind(), workflow.isActorTargetResolutionInFlight(), workflow.isMutationInFlight());
        expect(!targetControls.playerEnabled && !targetControls.crosshairEnabled && !targetControls.refreshEnabled,
            "real preview ownership disables Actor header actions");
        expect(!applyActorTargetHeaderIntent(workflow, ActorTargetHeaderIntent::crosshair), "preview blocks target transition");
        workflow.selectArea(TattooArea::hands);
        workflow.backToSlots();
        expect(workflow.selectedArea() == TattooArea::body && workflow.screen() == SlotWorkflowScreen::editAppearance &&
            !workflow.selectSlot(0) && !workflow.requestRemove() && !workflow.toggleSelectedSlotLock(),
            "preview ownership blocks navigation and unrelated mutations");
        const auto preview = workflow.takeAppearanceRequest();
        expect(preview.has_value(), "preview fixture");
        if (intent == EditAppearanceIntent::retry) {
            workflow.completeAppearanceUpdate(preview->generation, std::unexpected(ServiceError{
                ServiceErrorCode::synchronizeFailed, "sync failed"}));
        } else {
            workflow.completeAppearanceUpdate(preview->generation, UpdateTattooAppearanceSuccess{});
        }
        int teardowns = 0;
        int continuations = 0;
        orchestrateEditAppearanceFrame(workflow, {.intent = intent}, [&] { ++teardowns; },
            [&](const auto& thumbnail) {
                expect(workflow.editAppearance() && thumbnail.texturePath == "preview.dds", "only live session references are rendered");
                ++continuations;
            });
        int closes = 0;
        if (intent == EditAppearanceIntent::save) {
            expect(!workflow.editAppearance() && !workflow.takeAppearanceRequest() && workflow.takeSlotQuery(),
                "Save commits applied preview with refresh and no duplicate write");
            expect(teardowns == 1 && continuations == 0, "Save invalidation ends frame before scoped rendering");
            expect(!dispatchMenuCloseRequest(workflow, [&] { ++closes; }), "Save never closes menu");
        } else {
            expect(teardowns == 0 && continuations == 1, "pending operation retains valid presentation");
            expect(!applyEditAppearanceIntent(workflow, intent), "duplicate input is rejected");
            const auto operation = workflow.takeAppearanceRequest();
            expect(operation && operation->purpose == (intent == EditAppearanceIntent::retry
                    ? AppearanceOperationPurpose::preview : AppearanceOperationPurpose::restore), "exact intent purpose");
            expect(operation->request.color == (intent == EditAppearanceIntent::retry ? 0x123456 : 0xFFFFFF), "exact edited or original value");
            expect(operation->request.mode == (intent == EditAppearanceIntent::retry
                    ? UpdateTattooAppearanceMode::synchronizeOnly : UpdateTattooAppearanceMode::updateAndSynchronize), "retry preserves sync-only");
            expect(!dispatchMenuCloseRequest(workflow, [&] { ++closes; }), "no close before restore success");
            workflow.completeAppearanceUpdate(operation->generation, UpdateTattooAppearanceSuccess{});
            expect(dispatchMenuCloseRequest(workflow, [&] { ++closes; }) == (intent == EditAppearanceIntent::close),
                "only Close emits menu callback after restore success");
        }
        expect(!dispatchMenuCloseRequest(workflow, [&] { ++closes; }) && closes == (intent == EditAppearanceIntent::close ? 1 : 0),
            "menu close is consumable exactly once");
    }
}

void livePreviewExitInputHandlesPendingAndInFlightFrames() {
    using namespace stui::native;
    using namespace stui::core;
    using namespace std::chrono_literals;
    for (bool inFlight : {false, true}) {
        for (auto intent : {EditAppearanceIntent::save, EditAppearanceIntent::cancel, EditAppearanceIntent::close}) {
            LivePreviewRendererFixture fixture;
            auto& workflow = fixture.workflow;
            std::optional<SlotAppearanceTicket> preview;
            if (inFlight) {
                workflow.advanceLivePreview(std::chrono::steady_clock::time_point{});
                workflow.advanceLivePreview(std::chrono::steady_clock::time_point{} + 1000ms);
                preview = workflow.takeAppearanceRequest();
                expect(preview.has_value(), "in-flight fixture");
            }
            expect(!applyEditAppearanceIntent(workflow, EditAppearanceIntent::none) &&
                !applyEditAppearanceIntent(workflow, EditAppearanceIntent::retry), "unavailable inputs cannot emit work");
            int teardowns = 0;
            int continuations = 0;
            orchestrateEditAppearanceFrame(workflow, {.intent = intent}, [&] { ++teardowns; },
                [&](const auto&) { ++continuations; });
            expect(!applyEditAppearanceIntent(workflow, intent), "pending exit rejects duplicate input");
            if (!inFlight && intent != EditAppearanceIntent::save) {
                expect(teardowns == 1 && continuations == 0 && !workflow.editAppearance() &&
                    !workflow.takeAppearanceRequest(), "Cancel and Close before preview end frame without runtime writes");
            } else {
                expect(teardowns == 0 && continuations == 1, "active operation keeps editor frame alive");
                if (inFlight) {
                    expect(!workflow.takeAppearanceRequest(), "exit waits for exact active preview");
                    workflow.completeAppearanceUpdate(preview->generation, UpdateTattooAppearanceSuccess{});
                }
                const auto operation = workflow.takeAppearanceRequest();
                if (inFlight && intent == EditAppearanceIntent::save) {
                    expect(!operation && !workflow.editAppearance(), "Save reuses successful active preview");
                } else {
                    expect(operation && operation->purpose == (intent == EditAppearanceIntent::save
                            ? AppearanceOperationPurpose::commit : AppearanceOperationPurpose::restore), "intent schedules only matching work");
                    workflow.completeAppearanceUpdate(operation->generation, UpdateTattooAppearanceSuccess{});
                }
            }
            int closes = 0;
            expect(dispatchMenuCloseRequest(workflow, [&] { ++closes; }) == (intent == EditAppearanceIntent::close),
                "only Close intent closes after completion");
            expect(!dispatchMenuCloseRequest(workflow, [&] { ++closes; }), "duplicate close dispatch is empty");
        }
    }
}

void livePreviewRetryInputPreservesFailedPurposeAndMode() {
    using namespace stui::native;
    using namespace stui::core;
    using namespace std::chrono_literals;
    for (bool restore : {false, true}) {
        for (auto error : {ServiceErrorCode::synchronizeFailed, ServiceErrorCode::actorNotFound}) {
            LivePreviewRendererFixture fixture;
            auto& workflow = fixture.workflow;
            workflow.advanceLivePreview(std::chrono::steady_clock::time_point{});
            workflow.advanceLivePreview(std::chrono::steady_clock::time_point{} + 1000ms);
            auto operation = workflow.takeAppearanceRequest();
            expect(operation.has_value(), "preview retry fixture");
            if (restore) {
                workflow.completeAppearanceUpdate(operation->generation, UpdateTattooAppearanceSuccess{});
                expect(applyEditAppearanceIntent(workflow, EditAppearanceIntent::close), "Close starts restore");
                operation = workflow.takeAppearanceRequest();
                expect(operation.has_value(), "restore retry fixture");
            }
            workflow.completeAppearanceUpdate(operation->generation, std::unexpected(ServiceError{error, "operation failed"}));
            expect(applyEditAppearanceIntent(workflow, EditAppearanceIntent::retry), "error offers matching retry input");
            expect(!applyEditAppearanceIntent(workflow, EditAppearanceIntent::retry), "retry input cannot duplicate pending work");
            const auto retry = workflow.takeAppearanceRequest();
            expect(retry && retry->purpose == operation->purpose && retry->request.actorFormId == 0x14 &&
                retry->request.runtimeHandle == 73 && retry->request.color == (restore ? 0xFFFFFF : 0x123456),
                "retry retains purpose and exact appearance identity");
            expect(retry->request.mode == (error == ServiceErrorCode::synchronizeFailed
                    ? UpdateTattooAppearanceMode::synchronizeOnly : UpdateTattooAppearanceMode::updateAndSynchronize),
                "sync retry cannot repeat a completed write");
            int closes = 0;
            expect(!dispatchMenuCloseRequest(workflow, [&] { ++closes; }), "failed/pending restore does not close");
            workflow.completeAppearanceUpdate(retry->generation, UpdateTattooAppearanceSuccess{});
            if (restore) {
                expect(!dispatchMenuCloseRequest(workflow, {}), "missing callback does not lose close request");
            }
            expect(dispatchMenuCloseRequest(workflow, [&] { ++closes; }) == restore,
                "successful restore retry preserves requested Close");
            expect(!dispatchMenuCloseRequest(workflow, [&] { ++closes; }), "retry closes at most once");
        }
    }
}

void currentSlotColorSwatchUsesOwnedTattooColorAtBottomRight() {
    const stui::core::TattooSlot owned{
        .index = 2,
        .occupancy = stui::core::SlotOccupancy::slaveTats,
        .tattoo = stui::core::TattooEntry{.color = 0x123456},
    };

    const auto swatch = stui::native::calculateSlotColorSwatch(
        owned, 200.0F, 120.0F, 16.0F, 6.0F);

    expect(swatch.has_value(), "expected owned Current Slot to expose a color swatch");
    expect(swatch->x == 178.0F && swatch->y == 98.0F && swatch->size == 16.0F,
        "expected color swatch anchored inside the thumbnail bottom-right corner");
    expect(swatch->fillColor == 0xFF563412U,
        "expected 0xRRGGBB tattoo color packed for ImGui without swapped channels");
    expect(swatch->borderColor == 0xFF202020U,
        "expected a dark opaque outline around the color swatch");
}

void currentSlotColorSwatchSkipsEmptyAndExternalSlots() {
    const stui::core::TattooSlot empty{};
    const stui::core::TattooSlot external{
        .index = 3,
        .occupancy = stui::core::SlotOccupancy::external,
        .tattoo = stui::core::TattooEntry{.color = 0xABCDEF},
    };

    expect(!stui::native::calculateSlotColorSwatch(
                empty, 200.0F, 120.0F, 16.0F, 6.0F).has_value(),
        "expected empty Current Slot to omit the color swatch");
    expect(!stui::native::calculateSlotColorSwatch(
                external, 200.0F, 120.0F, 16.0F, 6.0F).has_value(),
        "expected external Current Slot to omit the color swatch");
}

void catalogTooltipIncludesInUseSlotDetails() {
    expect(stui::native::formatCatalogTattooTooltip("Corruption", {}) == "Corruption",
        "expected unused tattoo tooltip to contain only its name");
    expect(stui::native::formatCatalogTattooTooltip("Corruption", {2, 5}) ==
            "Corruption\nIn use: Slots 2, 5",
        "expected In Use tooltip to list every matching Current Slot");
}

void pageInputKeepsPendingEditsUntilEnterOrFocusLoss() {
    stui::native::CatalogBrowserPageInputState state;
    state.synchronize(1, 5);
    expect(state.pendingPageNumber() == 2, "expected one-based committed page value");

    state.pendingPageNumber() = 4;
    expect(!state.finishFrame(true, false, false).has_value(),
           "expected active edit not to commit on a keystroke");
    state.synchronize(1, 5);
    expect(state.pendingPageNumber() == 4,
           "expected pending edit to survive the next frame");

    const auto enterCommit = state.finishFrame(true, true, false);
    expect(enterCommit == 4, "expected Enter to commit the edited one-based value");
    state.synchronize(3, 5);
    expect(state.pendingPageNumber() == 4,
           "expected committed model page to resynchronize the input");

    state.pendingPageNumber() = 5;
    expect(!state.finishFrame(true, false, false).has_value(),
           "expected second pending edit not to commit early");
    const auto focusLossCommit = state.finishFrame(false, false, true);
    expect(focusLossCommit == 5,
           "expected focus loss to commit the edited one-based value");

    state.synchronize(0, 5);
    state.pendingPageNumber() = -7;
    expect(state.finishFrame(false, true, false) == 0,
           "expected invalid low input to reach model clamping as zero");
    state.synchronize(0, 0);
    state.pendingPageNumber() = 3;
    expect(!state.finishFrame(false, true, false).has_value(),
           "expected empty pagination not to commit");
}

void classifiesEmptyCatalogSeparatelyFromNoMatches() {
    const stui::repository::TattooPage validEmptyCatalog{
        .totalEntries = 0,
        .matchedEntries = 0,
        .pageCount = 0,
    };
    const auto emptyState =
        stui::native::classifyCatalogBrowserEmptyState(true, validEmptyCatalog);
    expect(emptyState == stui::native::CatalogBrowserEmptyState::emptyCatalog,
           "expected a valid zero-entry snapshot to be an empty catalog");
    expect(stui::native::catalogBrowserEmptyMessage(emptyState) ==
               "The tattoo catalog is empty. Refresh the catalog to browse tattoos.",
           "expected explicit empty-catalog message");

    const stui::repository::TattooPage noMatches{
        .totalEntries = 3,
        .matchedEntries = 0,
        .pageCount = 0,
    };
    const auto noMatchState =
        stui::native::classifyCatalogBrowserEmptyState(true, noMatches);
    expect(noMatchState == stui::native::CatalogBrowserEmptyState::noMatches,
           "expected filtered zero matches from a non-empty catalog");
    expect(stui::native::catalogBrowserEmptyMessage(noMatchState) ==
               "No tattoos match the current filters.",
           "expected explicit no-match message");

    expect(stui::native::classifyCatalogBrowserEmptyState(false, noMatches) ==
               stui::native::CatalogBrowserEmptyState::emptyCatalog,
           "expected no snapshot to remain an empty-catalog state");

    const auto noAppliedMatches =
        stui::native::classifyCatalogBrowserEmptyState(true, noMatches, true);
    expect(noAppliedMatches == stui::native::CatalogBrowserEmptyState::noAppliedMatches,
           "expected Applied-only empty results to have a distinct state");
    expect(stui::native::catalogBrowserEmptyMessage(noAppliedMatches) ==
               "No applied tattoos match the current filters.",
           "expected Applied-only empty state guidance");

    const auto noFavoriteMatches =
        stui::native::classifyCatalogBrowserEmptyState(true, noMatches, false, true);
    expect(noFavoriteMatches == stui::native::CatalogBrowserEmptyState::noFavoriteMatches &&
            stui::native::catalogBrowserEmptyMessage(noFavoriteMatches) ==
                "No favorite tattoos match the current filters.",
        "expected Favorites-only empty state guidance");
    const auto noFavoriteAppliedMatches =
        stui::native::classifyCatalogBrowserEmptyState(true, noMatches, true, true);
    expect(noFavoriteAppliedMatches == stui::native::CatalogBrowserEmptyState::noFavoriteAppliedMatches &&
            stui::native::catalogBrowserEmptyMessage(noFavoriteAppliedMatches) ==
                "No favorite applied tattoos match the current filters.",
        "expected combined filter empty state guidance");
}

void sourceOptionsDistinguishDuplicatePackNamesAndPreserveIds() {
    const std::vector<stui::repository::TattooSourceOption> sources{
        {.sourceId = "source-a.json", .packName = "Shared Pack"},
        {.sourceId = "source-b.json", .packName = "Shared Pack"},
    };

    const auto options = stui::native::buildCatalogBrowserSourceOptions(sources);
    expect(options.size() == 2, "expected one presentation option per source");
    expect(options[0].label == "Shared Pack (source-a.json)" &&
               options[1].label == "Shared Pack (source-b.json)",
           "expected duplicate pack names to include distinct source IDs");
    expect(options[0].sourceId == "source-a.json" &&
               options[1].sourceId == "source-b.json",
           "expected selection payloads to preserve exact source IDs");
}

void slotCardsExposeAddReplaceAndDisabledTreatments() {
    expect(stui::native::slotCardTreatment(stui::core::SlotOccupancy::empty) ==
               stui::native::SlotCardTreatment::add,
        "expected empty slot Add treatment");
    expect(stui::native::slotCardTreatment(stui::core::SlotOccupancy::slaveTats) ==
               stui::native::SlotCardTreatment::replace,
        "expected SlaveTats slot Replace treatment");
    expect(stui::native::slotCardTreatment(stui::core::SlotOccupancy::external) ==
               stui::native::SlotCardTreatment::disabled,
        "expected external slot disabled treatment");
}

void slotPaginationClampsToSixCardPages() {
    const auto first = stui::native::calculateSlotPage(12, 0, 6);
    expect(first.pageIndex == 0 && first.pageCount == 2 && first.begin == 0 &&
            first.end == 6,
        "expected first BODY page to contain six slots");

    const auto clamped = stui::native::calculateSlotPage(13, 9, 6);
    expect(clamped.pageIndex == 2 && clamped.pageCount == 3 &&
            clamped.begin == 12 && clamped.end == 13,
        "expected requested page clamped to the final partial page");

    const auto empty = stui::native::calculateSlotPage(0, 3, 6);
    expect(empty.pageIndex == 0 && empty.pageCount == 0 && empty.begin == 0 &&
            empty.end == 0,
        "expected empty slot collection to have safe zero bounds");
}

void slotAreaLabelsMatchSlaveTatsAreaNames() {
    expect(stui::native::slotAreaLabel(stui::core::TattooArea::body) == "BODY",
        "expected BODY tab label");
    expect(stui::native::slotAreaLabel(stui::core::TattooArea::face) == "FACE",
        "expected FACE tab label");
    expect(stui::native::slotAreaLabel(stui::core::TattooArea::hands) == "HANDS",
        "expected HANDS tab label");
    expect(stui::native::slotAreaLabel(stui::core::TattooArea::feet) == "FEET",
        "expected FEET tab label");
}

void visibleSlotPathsIncludeOnlyOwnedCardsOnTheCurrentPage() {
    const stui::core::TattooSlots slots{
        .actorFormId = 0x14,
        .area = stui::core::TattooArea::body,
        .configuredCount = 8,
        .slots = {
            {.index = 0, .occupancy = stui::core::SlotOccupancy::empty},
            {.index = 1, .occupancy = stui::core::SlotOccupancy::external},
            {.index = 2,
                .occupancy = stui::core::SlotOccupancy::slaveTats,
                .tattoo = stui::core::TattooEntry{.texturePath = "owned/a.dds"}},
            {.index = 3,
                .occupancy = stui::core::SlotOccupancy::slaveTats,
                .tattoo = stui::core::TattooEntry{.texturePath = ""}},
            {.index = 4,
                .occupancy = stui::core::SlotOccupancy::slaveTats,
                .tattoo = stui::core::TattooEntry{.texturePath = "owned/b.dds"}},
            {.index = 5, .occupancy = stui::core::SlotOccupancy::empty},
            {.index = 6,
                .occupancy = stui::core::SlotOccupancy::slaveTats,
                .tattoo = stui::core::TattooEntry{.texturePath = "next/c.dds"}},
            {.index = 7,
                .occupancy = stui::core::SlotOccupancy::slaveTats,
                .tattoo = stui::core::TattooEntry{.texturePath = "next/d.dds"}},
        },
    };

    const auto firstPage = stui::native::collectVisibleSlotTexturePaths(slots, 0, 6);
    expect(firstPage == std::vector<std::string>{"owned/a.dds", "owned/b.dds"},
        "expected only owned non-empty paths from the visible slot page");
    const auto secondPage = stui::native::collectVisibleSlotTexturePaths(slots, 1, 6);
    expect(secondPage == std::vector<std::string>{"next/c.dds", "next/d.dds"},
        "expected slot path collection to follow the selected page");
}

void pickerAndPreviewHelpersExposeExactTargetIntent() {
    const stui::native::ActorTarget player;
    expect(stui::native::formatSlotTargetLabel(
               &player, stui::core::TattooArea::body, 2) == "Player / BODY / Slot 2",
        "expected exact Player BODY target label");
    const stui::native::ActorTarget lydia{
        stui::native::ActorTargetKind::crosshair, 0xA2C8E, "Lydia"};
    expect(stui::native::formatSlotTargetLabel(
               &lydia, stui::core::TattooArea::face, 3) == "Lydia / FACE / Slot 3",
        "expected NPC context to match the active Actor without repeating the form ID");
    const stui::native::ActorTarget unnamed{
        stui::native::ActorTargetKind::crosshair, 0x1234, ""};
    expect(stui::native::formatSlotTargetLabel(
               &unnamed, stui::core::TattooArea::hands, 1) == "Unnamed Actor / HANDS / Slot 1",
        "expected empty Actor names normalized consistently with the shared header");
    expect(stui::native::formatSlotTargetLabel(
               nullptr, stui::core::TattooArea::feet, 0) == "No active Actor / FEET / Slot 0",
        "expected absent Actor context never to fall back to Player");
    expect(stui::native::previewApplyButtonLabel(2) == "Apply to Slot 2",
        "expected Apply action to name the target slot");
    expect(stui::native::previewApplyButtonLabel(2, true) == "Retry Slot 2",
        "expected failed Apply action to identify a retry");
    expect(stui::native::isPreviewApplyEnabled(
               stui::native::SlotWorkflowScreen::preview, true, true),
        "expected complete Preview target to enable Apply");
    expect(!stui::native::isPreviewApplyEnabled(
               stui::native::SlotWorkflowScreen::applying, true, true),
        "expected Applying state to suppress duplicate Apply");
    expect(!stui::native::isPreviewApplyEnabled(
               stui::native::SlotWorkflowScreen::preview, false, true),
        "expected missing slot target to disable Apply");
}

void removeHelpersRequireExplicitTargetConfirmation() {
    expect(stui::native::removeButtonLabel(
               2, stui::native::RemoveButtonState::initial) == "Remove from Slot 2",
        "expected Remove action to name the target slot");
    expect(stui::native::removeButtonLabel(
               2, stui::native::RemoveButtonState::retryRemove) == "Retry Remove Slot 2",
        "expected failed Remove action to identify a retry");
    expect(stui::native::removeButtonLabel(
               2, stui::native::RemoveButtonState::retrySynchronization) ==
               "Retry Sync Slot 2",
        "expected post-mutation failure to retry synchronization only");
    expect(stui::native::isRemoveConfirmationEnabled(
               stui::native::SlotWorkflowScreen::removeConfirmation, true),
        "expected complete Remove confirmation to enable mutation");
    expect(!stui::native::isRemoveConfirmationEnabled(
               stui::native::SlotWorkflowScreen::slotActions, true),
        "expected Slot Actions alone not to enable Remove mutation");
    expect(!stui::native::isRemoveConfirmationEnabled(
               stui::native::SlotWorkflowScreen::removeConfirmation, false),
        "expected missing slot target to disable Remove");
}

void domainPresentationUsesDefaultFallback() {
    expect(stui::native::domainPresentationLabel("custom") == "custom",
        "expected explicit domain presentation preserved");
    expect(stui::native::domainPresentationLabel("") == "default",
        "expected empty applied domain presented as default");
    expect(stui::native::domainThumbnailBadgeLabel("default").empty(),
        "expected default domain omitted from thumbnail badges");
    expect(stui::native::domainThumbnailBadgeLabel("Default").empty(),
        "expected default domain matching to ignore ASCII case");
    expect(stui::native::domainThumbnailBadgeLabel("").empty(),
        "expected missing domain omitted from thumbnail badges");
    expect(stui::native::domainThumbnailBadgeLabel("custom") == "custom",
        "expected non-default domain retained in thumbnail badges");
}

void domainOptionsStartWithAllDomains() {
    const auto options = stui::native::buildCatalogBrowserDomainOptions({"custom", "default"});
    expect(options == std::vector<std::string>{"All Domains", "custom", "default"},
        "expected All Domains before discovered domain options");
}

void lockActionPresentationKeepsAppearanceEditable() {
    const auto unlocked = stui::native::slotLockActionPresentation(false);
    expect(unlocked.iconCodepoint == 0xF09C && unlocked.tooltip == "Lock tattoo" &&
            unlocked.mutationsEnabled,
        "expected unlocked slot to offer the Font Awesome unlock icon and Lock action");

    const auto locked = stui::native::slotLockActionPresentation(true);
    expect(locked.iconCodepoint == 0xF023 && locked.tooltip == "Unlock tattoo" &&
            !locked.mutationsEnabled,
        "expected locked slot to offer the Font Awesome lock icon and Unlock action");
}

void pickerVisiblePathsFollowOnlyTheSixRenderedCards() {
    const stui::repository::TattooPage page{
        .entries = {
            {.texturePath = "picker/a.dds"},
            {.texturePath = "picker/b.dds"},
            {.texturePath = ""},
            {.texturePath = "picker/d.dds"},
            {.texturePath = "picker/e.dds"},
            {.texturePath = "picker/f.dds"},
            {.texturePath = "picker/not-visible.dds"},
        },
    };

    const auto paths = stui::native::collectPickerTexturePaths(page);
    expect(stui::native::pickerVisibleCardCount(page) == 6,
        "expected renderer and thumbnail collector to share a six-card bound");
    expect(paths == std::vector<std::string>{
                        "picker/a.dds",
                        "picker/b.dds",
                        "picker/d.dds",
                        "picker/e.dds",
                        "picker/f.dds"},
        "expected thumbnail work only for non-empty paths in six visible Picker cards");
}

void nullSnapshotModelHasSafeEmptyPageWithoutImGui() {
    stui::native::NativeCatalogBrowserModel model([] { return nullptr; });
    model.refresh();

    const auto& page = model.page();
    expect(page.entries.empty(), "expected no entries for a null snapshot");
    expect(page.totalEntries == 0 && page.matchedEntries == 0,
           "expected an empty catalog page");
    expect(page.pageCount == 0 && page.pageSize == 6,
           "expected safe six-item empty page metadata");

    void (*render)(
        stui::native::NativeSlotWorkflowModel&,
        stui::native::NativeSlotWorkflowRuntime&,
        stui::native::NativeCatalogBrowserModel&,
        stui::native::NativeThumbnailRuntime&,
        const std::function<void()>&) =
        &stui::native::OfficialMenuFrameworkAdapter::renderFoundation;
    (void)render;
}

}  // namespace

int main() {
    try {
        actorTargetIdentityUsesFixedWidthUppercaseFormId();
        std::cout << "PASS actor target identity and Current Slots title\n";
        actorTargetStatusAndActionAvailabilityRequireResolvedIdentity();
        std::cout << "PASS actor target status and action availability\n";
        editAppearanceHeaderAppendsLivePreviewStatusToActorIdentity();
        std::cout << "PASS Edit Appearance header appends Preview status\n";
        actorTargetControlsRespectMutationAndCrosshairMode();
        std::cout << "PASS actor target control presentation\n";
        actorTargetHeaderIntentsStopFramesOnlyAfterAcceptedChanges();
        std::cout << "PASS actor target header intent frame safety\n";
        rejectsIncompleteExportTable();
        std::cout << "PASS rejects incomplete export table\n";
        translatesSectionAndNonPausingWindowState();
        std::cout << "PASS translates section and non-pausing window state\n";
        defaultAdapterIsUnavailableWithoutLoadedFramework();
        std::cout << "PASS default adapter is unavailable without loaded framework\n";
        foundationLayoutAnchorsFortyPercentPanelToRightEdge();
        std::cout << "PASS foundation layout anchors panel to right edge\n";
        thumbnailPresentationFitsWithoutStretchingAndLabelsFailures();
        std::cout << "PASS thumbnail presentation fits and labels failures\n";
        thumbnailViewLookupMapsDuplicatePathVariantsToOneView();
        std::cout << "PASS thumbnail view lookup maps duplicate path variants\n";
        thumbnailGridGroupsTwoCardsIntoEachRow();
        std::cout << "PASS thumbnail grid groups two cards into each row\n";
        thumbnailCardWidgetsHaveStableUniqueIds();
        std::cout << "PASS thumbnail card widgets have stable unique IDs\n";
        favoriteButtonsUseDistinctStarGlyphs();
        std::cout << "PASS favorite buttons use distinct star glyphs\n";
        browserGridUsesRemainingHeightWithoutVerticalScrolling();
        std::cout << "PASS browser grid uses remaining height without scrolling\n";
        catalogBadgeAnchorsInsideThumbnailTopRightCorner();
        std::cout << "PASS catalog badge anchors inside thumbnail top-right\n";
        thumbnailCardsReserveNoPersistentMetadataRow();
        std::cout << "PASS thumbnail cards reserve no persistent metadata row\n";
        footerControlAlignsToRightContentEdge();
        std::cout << "PASS footer control aligns to right content edge\n";
        unifiedFooterKeepsCloseAtRightEdge();
        std::cout << "PASS unified footer keeps Close at right edge\n";
        pinnedFooterReservesTheBottomRowAfterShortContent();
        std::cout << "PASS pinned footer reserves the bottom row\n";
        pickerFooterActionsStayRightAlignedInNavigationOrder();
        std::cout << "PASS Picker footer actions stay right-aligned\n";
        tattooColorComponentsPreserveRgbChannelOrder();
        std::cout << "PASS tattoo color components preserve RGB channel order\n";
        editAppearanceUsesSessionStateForSaveAndThumbnailPresentation();
        std::cout << "PASS Edit Appearance uses session save and thumbnail state\n";
        editAppearanceLayoutHidesEmptyMetadataAndUsesExpandedRanges();
        std::cout << "PASS Edit Appearance layout and expanded ranges\n";
        editAppearanceRendererOrchestrationOrdersInputAndCancel();
        std::cout << "PASS Edit Appearance renderer orders input and Cancel\n";
        livePreviewPresentationMatchesTransactionState();
        std::cout << "PASS Live Preview labels and action presentation\n";
        livePreviewFrameRoutesOneIntentAndDefersClose();
        std::cout << "PASS Live Preview routing and deferred Close\n";
        livePreviewExitInputHandlesPendingAndInFlightFrames();
        std::cout << "PASS Live Preview pending and in-flight frame safety\n";
        livePreviewRetryInputPreservesFailedPurposeAndMode();
        std::cout << "PASS Live Preview retry purpose and mode\n";
        currentSlotColorSwatchUsesOwnedTattooColorAtBottomRight();
        std::cout << "PASS Current Slot color swatch uses owned tattoo color\n";
        currentSlotColorSwatchSkipsEmptyAndExternalSlots();
        std::cout << "PASS Current Slot color swatch skips empty and external slots\n";
        catalogTooltipIncludesInUseSlotDetails();
        std::cout << "PASS catalog tooltip includes In Use Slot details\n";
        pageInputKeepsPendingEditsUntilEnterOrFocusLoss();
        std::cout << "PASS page input keeps pending edits until commit\n";
        classifiesEmptyCatalogSeparatelyFromNoMatches();
        std::cout << "PASS classifies empty catalog separately from no matches\n";
        sourceOptionsDistinguishDuplicatePackNamesAndPreserveIds();
        std::cout << "PASS source options distinguish duplicate pack names\n";
        slotCardsExposeAddReplaceAndDisabledTreatments();
        std::cout << "PASS slot cards expose intended treatments\n";
        slotPaginationClampsToSixCardPages();
        std::cout << "PASS slot pagination clamps to six-card pages\n";
        slotAreaLabelsMatchSlaveTatsAreaNames();
        std::cout << "PASS slot area labels match SlaveTats areas\n";
        visibleSlotPathsIncludeOnlyOwnedCardsOnTheCurrentPage();
        std::cout << "PASS visible slot paths include only owned cards\n";
        pickerAndPreviewHelpersExposeExactTargetIntent();
        std::cout << "PASS picker and Preview helpers expose exact target intent\n";
        removeHelpersRequireExplicitTargetConfirmation();
        std::cout << "PASS Remove helpers require explicit target confirmation\n";
        domainPresentationUsesDefaultFallback();
        std::cout << "PASS domain presentation uses default fallback\n";
        domainOptionsStartWithAllDomains();
        std::cout << "PASS Domain options start with All Domains\n";
        lockActionPresentationKeepsAppearanceEditable();
        std::cout << "PASS Lock actions preserve Edit Appearance access\n";
        pickerVisiblePathsFollowOnlyTheSixRenderedCards();
        std::cout << "PASS Picker visible paths follow six rendered cards\n";
        nullSnapshotModelHasSafeEmptyPageWithoutImGui();
        std::cout << "PASS null snapshot model has safe empty page without ImGui\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
