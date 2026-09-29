#pragma once

#include "core/TattooModels.h"
#include "native/ActorTarget.h"
#include "native/NativeCatalogBrowserModel.h"
#include "runtime/AppearancePresetStore.h"
#include "runtime/FavoriteStore.h"
#include "runtime/RecentTattooStore.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace stui::native {

enum class SlotWorkflowScreen {
    currentSlots,
    slotActions,
    picker,
    preview,
    applying,
    editAppearance,
    savingAppearance,
    removeConfirmation,
    removing,
};

struct ActorTargetResolutionTicket {
    std::uint64_t generation{};
};

struct SlotQueryTicket {
    std::uint64_t generation{};
    std::uint32_t actorFormId{};
    core::TattooArea area{core::TattooArea::body};
};

struct SlotApplyTicket {
    std::uint64_t generation{};
    core::ApplyTattooRequest request;
    repository::RecentTattooIdentity recentIdentity;
};

struct SlotRemoveTicket {
    std::uint64_t generation{};
    core::RemoveTattooRequest request;
};

struct PreviewTattooAppearance {
    std::int32_t color{0xFFFFFF};
    float alpha{1.0F};

    bool operator==(const PreviewTattooAppearance&) const = default;
};

struct TattooAppearance {
    std::int32_t color{0xFFFFFF};
    float alpha{1.0F};
    std::int32_t glow{};
    float glossiness{};
    float specularStrength{};
    float emissiveMult{1.0F};

    bool operator==(const TattooAppearance&) const = default;
};

enum class AppearanceOperationPurpose { preview, commit, restore };

enum class LivePreviewStatus {
    clean, pending, updating, applied, restoring, previewError, restoreError
};

enum class AppearanceExitIntent { none, save, cancel, close };

struct AppearanceEditSession {
    std::uint32_t actorFormId{};
    std::uint64_t targetGeneration{};
    core::TattooArea area{core::TattooArea::body};
    std::int32_t slot{-1};
    std::int32_t runtimeHandle{};
    std::string texturePath;
    std::string glowTexture;
    std::string bump;
    TattooAppearance original;
    TattooAppearance edited;
    std::optional<TattooAppearance> lastPreviewed;
    std::uint64_t editRevision{};
    std::uint64_t observedRevision{};
    std::optional<std::chrono::steady_clock::time_point> latestEditTime;
    LivePreviewStatus status{LivePreviewStatus::clean};
    AppearanceExitIntent exitIntent{AppearanceExitIntent::none};
    AppearanceOperationPurpose activePurpose{AppearanceOperationPurpose::preview};
    TattooAppearance operationAppearance;
    bool appearanceWritten{};
    core::UpdateTattooAppearanceMode mode{
        core::UpdateTattooAppearanceMode::updateAndSynchronize};
};

struct SlotAppearanceTicket {
    std::uint64_t generation{};
    core::UpdateTattooAppearanceRequest request;
    AppearanceOperationPurpose purpose{AppearanceOperationPurpose::commit};
};

struct SlotLockTicket {
    std::uint64_t generation{};
    core::SetTattooLockedRequest request;
};

struct FavoriteTicket {
    std::uint64_t requestId{};
    repository::FavoriteIdentity identity;
    bool enabled{};
};

enum class RecentTattooRequestKind { load, record };

struct RecentTattooTicket {
    std::uint64_t requestId{};
    RecentTattooRequestKind kind{RecentTattooRequestKind::record};
    std::optional<repository::RecentTattooIdentity> identity;
};

enum class AppearancePresetRequestKind { load, create, overwrite, rename, erase };

struct AppearancePresetTicket {
    std::uint64_t requestId{};
    AppearancePresetRequestKind kind{AppearancePresetRequestKind::load};
    std::optional<runtime::AppearancePreset> preset;
    std::string existingName;
};

class NativeSlotWorkflowModel {
public:
    static constexpr std::size_t kPageSize = 6;

    explicit NativeSlotWorkflowModel(NativeCatalogBrowserModel& catalog) noexcept;

    void start();
    void resetSession();
    [[nodiscard]] bool selectPlayerTarget();
    [[nodiscard]] bool selectCrosshairTarget();
    [[nodiscard]] bool refreshCrosshairTarget();
    [[nodiscard]] std::optional<ActorTargetResolutionTicket> takeActorTargetRequest();
    void completeActorTargetResolution(std::uint64_t generation, ActorTargetResult result);
    [[nodiscard]] ActorTargetKind selectedTargetKind() const noexcept;
    [[nodiscard]] const ActorTarget* actorTarget() const noexcept;
    [[nodiscard]] bool isActorTargetResolutionInFlight() const noexcept;
    [[nodiscard]] bool isMutationInFlight() const noexcept;
    void selectArea(core::TattooArea area);
    void refreshSelectedArea();
    void previousSlotPage();
    void nextSlotPage();
    void setSlotPageNumber(std::size_t oneBasedPage);
    [[nodiscard]] bool selectSlot(std::int32_t slot);
    [[nodiscard]] bool replaceSelectedSlot();
    [[nodiscard]] bool requestRemove();
    void cancelRemove();
    [[nodiscard]] bool confirmRemove();
    void backToSlots();
    void selectTattoo(const repository::TattooDefinition& tattoo);
    void setPreviewAppearance(std::int32_t color, float alpha) noexcept;
    void cancelPreview();
    void backToPicker();
    [[nodiscard]] bool confirmApply();
    [[nodiscard]] bool beginEditAppearance();
    [[nodiscard]] bool toggleSlotLock(std::int32_t slot);
    [[nodiscard]] bool toggleSelectedSlotLock();
    void setEditedAppearance(std::int32_t color, float alpha) noexcept;
    void setEditedAppearance(
        std::int32_t color,
        float alpha,
        std::int32_t glow,
        float glossiness,
        float specularStrength,
        float emissiveMult) noexcept;
    [[nodiscard]] bool loadAppearancePreset(const runtime::AppearancePreset& preset);
    [[nodiscard]] const runtime::AppearancePresetList& appearancePresets() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedAppearancePreset() const noexcept;
    void selectAppearancePreset(std::optional<std::size_t> index);
    void cancelEditAppearance();
    [[nodiscard]] bool confirmAppearanceUpdate();
    void advanceLivePreview(std::chrono::steady_clock::time_point now);
    [[nodiscard]] LivePreviewStatus livePreviewStatus() const noexcept;
    [[nodiscard]] bool requestEditAppearanceClose();
    [[nodiscard]] bool takeMenuCloseRequest();
    [[nodiscard]] bool retryLivePreviewOperation();
    [[nodiscard]] bool requestFavorite(const repository::TattooDefinition& tattoo, bool enabled);
    [[nodiscard]] bool retryFavorite();
    void initializeRecentTattoos();
    [[nodiscard]] bool retryRecentTattoo();
    void initializeAppearancePresets();
    [[nodiscard]] bool requestCreateAppearancePreset(std::string name);
    [[nodiscard]] bool confirmAppearancePresetOverwrite();
    [[nodiscard]] bool requestRenameAppearancePreset(std::string newName);
    [[nodiscard]] bool requestDeleteAppearancePreset();
    [[nodiscard]] bool confirmAppearancePresetDelete();
    void cancelAppearancePresetConfirmation() noexcept;
    [[nodiscard]] bool retryAppearancePreset();

    [[nodiscard]] std::optional<SlotQueryTicket> takeSlotQuery();
    [[nodiscard]] std::optional<SlotApplyTicket> takeApplyRequest();
    [[nodiscard]] std::optional<SlotRemoveTicket> takeRemoveRequest();
    [[nodiscard]] std::optional<SlotAppearanceTicket> takeAppearanceRequest();
    [[nodiscard]] std::optional<SlotLockTicket> takeLockRequest();
    [[nodiscard]] std::optional<FavoriteTicket> takeFavoriteRequest();
    [[nodiscard]] std::optional<RecentTattooTicket> takeRecentTattooRequest();
    [[nodiscard]] std::optional<AppearancePresetTicket> takeAppearancePresetRequest();
    void completeSlotQuery(std::uint64_t generation, core::TattooSlotsResult result);
    void completeApply(std::uint64_t generation, core::ApplyTattooResult result);
    void completeRemove(std::uint64_t generation, core::RemoveTattooResult result);
    void completeAppearanceUpdate(
        std::uint64_t generation,
        core::UpdateTattooAppearanceResult result);
    void completeLockStateChange(std::uint64_t generation, core::SetTattooLockedResult result);
    void completeFavorite(std::uint64_t requestId, runtime::FavoriteResult result);
    void completeRecentTattoo(std::uint64_t requestId, runtime::RecentTattooResult result);
    void completeAppearancePreset(
        std::uint64_t requestId,
        runtime::AppearancePresetResult result);

    [[nodiscard]] SlotWorkflowScreen screen() const noexcept;
    [[nodiscard]] core::TattooArea selectedArea() const noexcept;
    [[nodiscard]] const core::TattooSlots* slots() const noexcept;
    [[nodiscard]] std::size_t slotPageIndex() const noexcept;
    [[nodiscard]] std::size_t slotPageCount() const noexcept;
    [[nodiscard]] std::optional<std::int32_t> targetSlot() const noexcept;
    [[nodiscard]] const repository::TattooDefinition* previewTattoo() const noexcept;
    [[nodiscard]] const PreviewTattooAppearance* previewAppearance() const noexcept;
    [[nodiscard]] const AppearanceEditSession* editAppearance() const noexcept;
    [[nodiscard]] bool canSaveAppearance() const noexcept;
    [[nodiscard]] bool isLockStateChangeInFlight() const noexcept;
    void setAppliedOnly(bool value);
    [[nodiscard]] bool appliedOnly() const noexcept;
    void setFavoritesOnly(bool value);
    [[nodiscard]] bool favoritesOnly() const noexcept;
    [[nodiscard]] bool favoritePending() const noexcept;
    [[nodiscard]] const runtime::ConfigError* favoriteError() const noexcept;
    void setRecentlyUsedOnly(bool value);
    [[nodiscard]] bool recentlyUsedOnly() const noexcept;
    [[nodiscard]] bool recentlyUsedPending() const noexcept;
    [[nodiscard]] const runtime::ConfigError* recentlyUsedError() const noexcept;
    [[nodiscard]] bool appearancePresetPending() const noexcept;
    [[nodiscard]] const runtime::ConfigError* appearancePresetError() const noexcept;
    [[nodiscard]] const runtime::AppearancePreset* appearancePresetOverwriteConfirmation() const noexcept;
    [[nodiscard]] bool appearancePresetDeleteConfirmation() const noexcept;
    [[nodiscard]] std::vector<std::int32_t> inUseSlots(
        const repository::TattooDefinition& tattoo) const;
    [[nodiscard]] const core::ServiceError* error() const noexcept;

private:
    struct AreaState {
        std::optional<core::TattooSlots> slots;
        std::size_t pageIndex{};
    };

    [[nodiscard]] static std::size_t areaIndex(core::TattooArea area) noexcept;
    [[nodiscard]] AreaState& selectedState() noexcept;
    [[nodiscard]] const AreaState& selectedState() const noexcept;
    [[nodiscard]] std::uint64_t nextGeneration() noexcept;
    void invalidateActorState();
    [[nodiscard]] bool hasActorSnapshot() const noexcept;
    void finishOutstandingMutation(std::uint64_t generation) noexcept;
    void scheduleSlotQuery(core::TattooArea area);
    void clampSelectedPage() noexcept;
    void openPicker();
    void updateAppliedTattooIdentities();
    void enqueueRecentTattoo(repository::RecentTattooIdentity identity);
    void queueNextRecentTattoo();
    [[nodiscard]] bool queueAppearanceOperation(
        AppearanceOperationPurpose purpose,
        const TattooAppearance& appearance);
    void updateLivePreviewStatus() noexcept;
    [[nodiscard]] bool hasAppearanceTransactionWork() const noexcept;
    [[nodiscard]] bool requestAppearanceRollback(AppearanceExitIntent intent);
    void continueAppearanceExit();
    void finishAppearanceSession(bool saved);
    [[nodiscard]] bool queueSlotLockToggle(
        std::int32_t slot,
        SlotWorkflowScreen originScreen);
    [[nodiscard]] bool queueAppearancePresetRequest(AppearancePresetTicket ticket);

    NativeCatalogBrowserModel& m_catalog;
    ActorTargetKind m_selectedTargetKind{ActorTargetKind::player};
    std::optional<ActorTarget> m_actorTarget{ActorTarget{}};
    std::optional<ActorTargetResolutionTicket> m_pendingActorTarget;
    std::optional<std::uint64_t> m_activeActorTargetGeneration;
    std::uint64_t m_targetGeneration{};
    // Runtime work outlives UI navigation that discards its presentation state.
    std::optional<std::uint64_t> m_outstandingMutationGeneration;
    std::array<AreaState, 4> m_areaStates;
    SlotWorkflowScreen m_screen{SlotWorkflowScreen::currentSlots};
    core::TattooArea m_selectedArea{core::TattooArea::body};
    std::optional<std::int32_t> m_targetSlot;
    std::optional<repository::TattooDefinition> m_previewTattoo;
    std::optional<PreviewTattooAppearance> m_previewAppearance;
    std::optional<core::ServiceError> m_error;
    std::optional<SlotQueryTicket> m_pendingSlotQuery;
    std::optional<SlotApplyTicket> m_pendingApply;
    std::optional<SlotRemoveTicket> m_pendingRemove;
    std::optional<SlotAppearanceTicket> m_pendingAppearance;
    std::optional<SlotLockTicket> m_pendingLock;
    std::optional<FavoriteTicket> m_pendingFavorite;
    std::optional<std::uint64_t> m_activeSlotQueryGeneration;
    std::optional<std::uint64_t> m_activeApplyGeneration;
    std::optional<repository::RecentTattooIdentity> m_activeApplyRecentIdentity;
    std::optional<std::uint64_t> m_activeRemoveGeneration;
    std::optional<std::uint64_t> m_activeAppearanceGeneration;
    std::optional<std::uint64_t> m_activeLockGeneration;
    std::optional<SlotWorkflowScreen> m_lockOriginScreen;
    std::optional<std::uint64_t> m_activeFavoriteRequestId;
    std::optional<FavoriteTicket> m_activeFavorite;
    std::optional<FavoriteTicket> m_failedFavorite;
    std::optional<runtime::ConfigError> m_favoriteError;
    std::uint64_t m_favoriteRequestId{};
    std::optional<RecentTattooTicket> m_pendingRecentTattoo;
    std::optional<std::uint64_t> m_activeRecentTattooRequestId;
    std::optional<RecentTattooTicket> m_activeRecentTattoo;
    std::optional<RecentTattooTicket> m_failedRecentTattoo;
    std::optional<runtime::ConfigError> m_recentTattooError;
    std::deque<repository::RecentTattooIdentity> m_recentTattooQueue;
    std::uint64_t m_recentTattooRequestId{};
    std::optional<AppearancePresetTicket> m_pendingAppearancePreset;
    std::optional<std::uint64_t> m_activeAppearancePresetRequestId;
    std::optional<AppearancePresetTicket> m_activeAppearancePreset;
    std::optional<AppearancePresetTicket> m_failedAppearancePreset;
    std::optional<runtime::AppearancePreset> m_appearancePresetOverwriteConfirmation;
    bool m_appearancePresetDeleteConfirmation{};
    std::optional<runtime::ConfigError> m_appearancePresetError;
    std::uint64_t m_appearancePresetRequestId{};
    bool m_applyRequiresSynchronizationOnly{};
    bool m_removeRequiresSynchronizationOnly{};
    std::optional<AppearanceEditSession> m_editAppearance;
    runtime::AppearancePresetList m_appearancePresets;
    std::optional<std::size_t> m_selectedAppearancePreset;
    bool m_menuCloseRequested{};
    std::uint64_t m_generation{};
    bool m_started{false};
};

}  // namespace stui::native
