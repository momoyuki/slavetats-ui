#include "native/NativeSlotWorkflowModel.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace stui::native {
namespace {

std::string_view areaName(core::TattooArea area) noexcept {
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

char foldASCII(char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value + ('a' - 'A'));
    }
    return value;
}

bool equalsFoldedASCII(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
        if (foldASCII(left[index]) != foldASCII(right[index])) {
            return false;
        }
    }
    return true;
}

std::string trimASCII(std::string value) {
    const auto whitespace = [](const char character) {
        return character == ' ' || character == '\t' || character == '\r' ||
            character == '\n' || character == '\f' || character == '\v';
    };
    const auto first = std::find_if_not(value.begin(), value.end(), whitespace);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), whitespace).base();
    return first < last ? std::string(first, last) : std::string{};
}

}  // namespace

NativeSlotWorkflowModel::NativeSlotWorkflowModel(NativeCatalogBrowserModel& catalog) noexcept :
    m_catalog(catalog) {}

void NativeSlotWorkflowModel::start() {
    if (m_started) {
        return;
    }

    m_started = true;
    scheduleSlotQuery(m_selectedArea);
}

void NativeSlotWorkflowModel::resetSession() {
    if (!isMutationInFlight()) {
        (void)selectPlayerTarget();
    }
}

bool NativeSlotWorkflowModel::selectPlayerTarget() {
    if (isMutationInFlight()) {
        return false;
    }
    invalidateActorState();
    m_selectedTargetKind = ActorTargetKind::player;
    m_actorTarget = ActorTarget{};
    if (m_started) {
        scheduleSlotQuery(m_selectedArea);
    }
    return true;
}

bool NativeSlotWorkflowModel::selectCrosshairTarget() {
    if (isMutationInFlight()) {
        return false;
    }
    invalidateActorState();
    m_selectedTargetKind = ActorTargetKind::crosshair;
    m_pendingActorTarget = ActorTargetResolutionTicket{m_targetGeneration};
    m_activeActorTargetGeneration = m_targetGeneration;
    return true;
}

bool NativeSlotWorkflowModel::refreshCrosshairTarget() {
    return m_selectedTargetKind == ActorTargetKind::crosshair && selectCrosshairTarget();
}

std::optional<ActorTargetResolutionTicket> NativeSlotWorkflowModel::takeActorTargetRequest() {
    auto ticket = std::move(m_pendingActorTarget);
    m_pendingActorTarget.reset();
    return ticket;
}

void NativeSlotWorkflowModel::completeActorTargetResolution(
    std::uint64_t generation,
    ActorTargetResult result) {
    if (!m_activeActorTargetGeneration || generation != *m_activeActorTargetGeneration ||
        generation != m_targetGeneration) {
        return;
    }
    m_activeActorTargetGeneration.reset();
    m_pendingActorTarget.reset();
    if (!result) {
        m_error = std::move(result.error());
        return;
    }
    if (result->kind != ActorTargetKind::crosshair || result->formId == 0) {
        m_error = core::ServiceError{
            core::ServiceErrorCode::actorNotFound, "No valid crosshair Actor"};
        return;
    }
    if (result->displayName.empty()) {
        result->displayName = "Unnamed Actor";
    }
    m_actorTarget = std::move(*result);
    m_error.reset();
    if (m_started) {
        scheduleSlotQuery(m_selectedArea);
    }
}

ActorTargetKind NativeSlotWorkflowModel::selectedTargetKind() const noexcept {
    return m_selectedTargetKind;
}

const ActorTarget* NativeSlotWorkflowModel::actorTarget() const noexcept {
    return m_actorTarget ? &*m_actorTarget : nullptr;
}

bool NativeSlotWorkflowModel::isActorTargetResolutionInFlight() const noexcept {
    return m_activeActorTargetGeneration.has_value();
}

void NativeSlotWorkflowModel::selectArea(core::TattooArea area) {
    if (area == m_selectedArea || hasAppearanceTransactionWork()) {
        return;
    }

    m_selectedArea = area;
    m_screen = SlotWorkflowScreen::currentSlots;
    m_targetSlot.reset();
    m_previewTattoo.reset();
    m_previewAppearance.reset();
    m_applyRequiresSynchronizationOnly = false;
    m_pendingAppearance.reset();
    m_activeAppearanceGeneration.reset();
    m_pendingLock.reset();
    m_activeLockGeneration.reset();
    m_lockOriginScreen.reset();
    m_editAppearance.reset();
    m_error.reset();
    clampSelectedPage();
    if (m_started && !selectedState().slots) {
        scheduleSlotQuery(area);
    }
}

void NativeSlotWorkflowModel::refreshSelectedArea() {
    if (!m_started || m_editAppearance || hasAppearanceTransactionWork()) {
        return;
    }
    scheduleSlotQuery(m_selectedArea);
}

void NativeSlotWorkflowModel::previousSlotPage() {
    if (hasAppearanceTransactionWork()) {
        return;
    }
    auto& state = selectedState();
    if (state.pageIndex > 0) {
        --state.pageIndex;
    }
}

void NativeSlotWorkflowModel::nextSlotPage() {
    if (hasAppearanceTransactionWork()) {
        return;
    }
    auto& state = selectedState();
    const std::size_t pageCount = slotPageCount();
    if (pageCount > 0 && state.pageIndex + 1 < pageCount) {
        ++state.pageIndex;
    }
}

void NativeSlotWorkflowModel::setSlotPageNumber(std::size_t oneBasedPage) {
    if (hasAppearanceTransactionWork()) {
        return;
    }
    auto& state = selectedState();
    const std::size_t pageCount = slotPageCount();
    if (pageCount == 0) {
        state.pageIndex = 0;
        return;
    }

    const std::size_t requested = oneBasedPage == 0 ? 0 : oneBasedPage - 1;
    state.pageIndex = std::min(requested, pageCount - 1);
}

bool NativeSlotWorkflowModel::selectSlot(std::int32_t slot) {
    const auto* current = slots();
    if (!hasActorSnapshot() || isMutationInFlight()) {
        return false;
    }

    const auto found = std::ranges::find_if(current->slots, [slot](const core::TattooSlot& candidate) {
        return candidate.index == slot;
    });
    if (found == current->slots.end() || found->occupancy == core::SlotOccupancy::external) {
        return false;
    }

    m_targetSlot = slot;
    m_previewTattoo.reset();
    m_previewAppearance = PreviewTattooAppearance{};
    m_editAppearance.reset();
    if (found->occupancy == core::SlotOccupancy::slaveTats && found->tattoo) {
        m_previewAppearance = PreviewTattooAppearance{
            .color = std::clamp(found->tattoo->color, 0, 0xFFFFFF),
            .alpha = std::clamp(found->tattoo->alpha, 0.0F, 1.0F),
        };
    }
    m_error.reset();
    if (found->occupancy == core::SlotOccupancy::slaveTats) {
        m_screen = SlotWorkflowScreen::slotActions;
        return true;
    }

    openPicker();
    return true;
}

bool NativeSlotWorkflowModel::replaceSelectedSlot() {
    if (m_screen != SlotWorkflowScreen::slotActions || !m_targetSlot ||
        m_pendingLock || m_activeLockGeneration || hasAppearanceTransactionWork()) {
        return false;
    }

    const auto* current = slots();
    if (!current) {
        return false;
    }
    const auto found = std::ranges::find_if(current->slots, [this](const core::TattooSlot& slot) {
        return slot.index == *m_targetSlot;
    });
    if (found == current->slots.end() || !found->tattoo || found->tattoo->locked) {
        return false;
    }

    openPicker();
    return true;
}

bool NativeSlotWorkflowModel::requestRemove() {
    if (m_screen != SlotWorkflowScreen::slotActions || !m_targetSlot ||
        m_pendingLock || m_activeLockGeneration || hasAppearanceTransactionWork()) {
        return false;
    }

    const auto* current = slots();
    if (!current) {
        return false;
    }
    const auto found = std::ranges::find_if(current->slots, [this](const core::TattooSlot& slot) {
        return slot.index == *m_targetSlot;
    });
    if (found == current->slots.end() || !found->tattoo || found->tattoo->locked) {
        return false;
    }

    m_error.reset();
    m_screen = SlotWorkflowScreen::removeConfirmation;
    return true;
}

void NativeSlotWorkflowModel::cancelRemove() {
    if (m_screen != SlotWorkflowScreen::removeConfirmation) {
        return;
    }

    m_error.reset();
    m_screen = SlotWorkflowScreen::slotActions;
}

bool NativeSlotWorkflowModel::confirmRemove() {
    if (m_screen != SlotWorkflowScreen::removeConfirmation || !m_targetSlot ||
        !hasActorSnapshot() || isMutationInFlight()) {
        return false;
    }

    const auto mode = m_removeRequiresSynchronizationOnly || (m_error &&
            m_error->code == core::ServiceErrorCode::synchronizeFailed)
        ? core::RemoveTattooMode::synchronizeOnly
        : core::RemoveTattooMode::removeAndSynchronize;
    m_removeRequiresSynchronizationOnly = mode == core::RemoveTattooMode::synchronizeOnly;
    const std::uint64_t generation = nextGeneration();
    m_pendingRemove = SlotRemoveTicket{
        .generation = generation,
        .request = core::RemoveTattooRequest{
            .actorFormId = m_actorTarget->formId,
            .area = m_selectedArea,
            .slot = *m_targetSlot,
            .mode = mode,
        },
    };
    m_activeRemoveGeneration = generation;
    m_error.reset();
    m_screen = SlotWorkflowScreen::removing;
    return true;
}

void NativeSlotWorkflowModel::openPicker() {
    updateAppliedTattooIdentities();
    const auto targetArea = areaName(m_selectedArea);
    if (!equalsFoldedASCII(m_catalog.filter().area, targetArea)) {
        m_catalog.setArea(std::string(targetArea));
    }
    m_screen = SlotWorkflowScreen::picker;
}

void NativeSlotWorkflowModel::backToSlots() {
    if (hasAppearanceTransactionWork()) {
        return;
    }
    m_screen = SlotWorkflowScreen::currentSlots;
    m_targetSlot.reset();
    m_previewTattoo.reset();
    m_previewAppearance.reset();
    m_editAppearance.reset();
    m_error.reset();
}

void NativeSlotWorkflowModel::selectTattoo(const repository::TattooDefinition& tattoo) {
    if (m_screen != SlotWorkflowScreen::picker || !m_targetSlot ||
        !equalsFoldedASCII(tattoo.area, areaName(m_selectedArea))) {
        return;
    }

    m_previewTattoo = tattoo;
    m_applyRequiresSynchronizationOnly = false;
    m_error.reset();
    m_screen = SlotWorkflowScreen::preview;
}

void NativeSlotWorkflowModel::setPreviewAppearance(
    std::int32_t color,
    float alpha) noexcept {
    if (m_screen != SlotWorkflowScreen::preview || !m_previewAppearance) {
        return;
    }

    m_previewAppearance->color = std::clamp(color, 0, 0xFFFFFF);
    m_previewAppearance->alpha = std::clamp(alpha, 0.0F, 1.0F);
}

void NativeSlotWorkflowModel::cancelPreview() {
    if (m_screen != SlotWorkflowScreen::preview) {
        return;
    }

    backToSlots();
}

void NativeSlotWorkflowModel::backToPicker() {
    if (m_screen != SlotWorkflowScreen::preview) {
        return;
    }

    m_previewTattoo.reset();
    m_error.reset();
    openPicker();
}

bool NativeSlotWorkflowModel::confirmApply() {
    if (m_screen != SlotWorkflowScreen::preview || !m_targetSlot || !m_previewTattoo ||
        !m_previewAppearance || !hasActorSnapshot() || isMutationInFlight() ||
        !equalsFoldedASCII(m_previewTattoo->area, areaName(m_selectedArea))) {
        return false;
    }

    const std::uint64_t generation = nextGeneration();
    m_pendingApply = SlotApplyTicket{
        .generation = generation,
        .request = core::ApplyTattooRequest{
            .actorFormId = m_actorTarget->formId,
            .area = m_selectedArea,
            .slot = *m_targetSlot,
            .domain = m_previewTattoo->domain,
            .section = m_previewTattoo->section,
            .name = m_previewTattoo->name,
            .color = m_previewAppearance->color,
            .alpha = m_previewAppearance->alpha,
            .mode = m_applyRequiresSynchronizationOnly
                ? core::ApplyTattooMode::synchronizeOnly
                : core::ApplyTattooMode::applyAndSynchronize,
        },
        .recentIdentity = repository::recentTattooIdentity(
            *m_previewTattoo, m_selectedArea),
    };
    m_activeApplyGeneration = generation;
    m_error.reset();
    m_screen = SlotWorkflowScreen::applying;
    return true;
}

bool NativeSlotWorkflowModel::beginEditAppearance() {
    if (m_screen != SlotWorkflowScreen::slotActions || !m_targetSlot ||
        !hasActorSnapshot() || isMutationInFlight()) {
        return false;
    }

    const auto* currentSlots = slots();
    if (!currentSlots) {
        return false;
    }

    const auto found = std::ranges::find_if(currentSlots->slots, [this](const core::TattooSlot& candidate) {
        return candidate.index == *m_targetSlot;
    });
    if (found == currentSlots->slots.end() || found->occupancy != core::SlotOccupancy::slaveTats ||
        !found->tattoo || found->tattoo->runtimeHandle == 0) {
        return false;
    }

    const TattooAppearance appearance{
        .color = found->tattoo->color,
        .alpha = found->tattoo->alpha,
        .glow = found->tattoo->glow,
        .glossiness = found->tattoo->glossiness,
        .specularStrength = found->tattoo->specularStrength,
        .emissiveMult = found->tattoo->emissiveMult,
    };
    m_editAppearance = AppearanceEditSession{
        .actorFormId = currentSlots->actorFormId,
        .targetGeneration = m_targetGeneration,
        .area = m_selectedArea,
        .slot = found->index,
        .runtimeHandle = found->tattoo->runtimeHandle,
        .texturePath = found->tattoo->texturePath,
        .glowTexture = found->tattoo->glowTexture,
        .bump = found->tattoo->bump,
        .original = appearance,
        .edited = appearance,
    };
    // An older refresh must not replace the snapshot captured by this edit session.
    m_pendingSlotQuery.reset();
    m_activeSlotQueryGeneration.reset();
    m_error.reset();
    m_selectedAppearancePreset.reset();
    m_screen = SlotWorkflowScreen::editAppearance;
    return true;
}

bool NativeSlotWorkflowModel::toggleSlotLock(std::int32_t slot) {
    if (m_screen != SlotWorkflowScreen::currentSlots) {
        return false;
    }
    return queueSlotLockToggle(slot, SlotWorkflowScreen::currentSlots);
}

bool NativeSlotWorkflowModel::toggleSelectedSlotLock() {
    if (m_screen != SlotWorkflowScreen::slotActions || !m_targetSlot) {
        return false;
    }
    return queueSlotLockToggle(*m_targetSlot, SlotWorkflowScreen::slotActions);
}

bool NativeSlotWorkflowModel::queueSlotLockToggle(
    std::int32_t slot,
    SlotWorkflowScreen originScreen) {
    if (!hasActorSnapshot() || isMutationInFlight()) {
        return false;
    }
    const auto* current = slots();
    if (!current) {
        return false;
    }
    const auto found = std::ranges::find_if(current->slots, [slot](const core::TattooSlot& candidate) {
        return candidate.index == slot;
    });
    if (found == current->slots.end() || found->occupancy != core::SlotOccupancy::slaveTats ||
        !found->tattoo || found->tattoo->runtimeHandle == 0) {
        return false;
    }
    m_targetSlot = slot;
    const std::uint64_t generation = nextGeneration();
    m_pendingLock = SlotLockTicket{
        .generation = generation,
        .request = core::SetTattooLockedRequest{
            .actorFormId = current->actorFormId,
            .runtimeHandle = found->tattoo->runtimeHandle,
            .locked = !found->tattoo->locked,
        },
    };
    m_activeLockGeneration = generation;
    m_lockOriginScreen = originScreen;
    m_error.reset();
    return true;
}

void NativeSlotWorkflowModel::setEditedAppearance(std::int32_t color, float alpha) noexcept {
    if (m_screen != SlotWorkflowScreen::editAppearance || !m_editAppearance ||
        m_editAppearance->mode == core::UpdateTattooAppearanceMode::synchronizeOnly ||
        m_editAppearance->exitIntent != AppearanceExitIntent::none) {
        return;
    }

    setEditedAppearance(
        color,
        alpha,
        m_editAppearance->edited.glow,
        m_editAppearance->edited.glossiness,
        m_editAppearance->edited.specularStrength,
        m_editAppearance->edited.emissiveMult);
}

void NativeSlotWorkflowModel::setEditedAppearance(
    std::int32_t color,
    float alpha,
    std::int32_t glow,
    float glossiness,
    float specularStrength,
    float emissiveMult) noexcept {
    if (m_screen != SlotWorkflowScreen::editAppearance || !m_editAppearance ||
        m_editAppearance->mode == core::UpdateTattooAppearanceMode::synchronizeOnly ||
        m_editAppearance->exitIntent != AppearanceExitIntent::none) {
        return;
    }

    const auto previous = m_editAppearance->edited;
    m_editAppearance->edited.color = std::clamp(color, 0, 0xFFFFFF);
    m_editAppearance->edited.alpha = std::clamp(alpha, 0.0F, 1.0F);
    m_editAppearance->edited.glow = std::clamp(glow, 0, 0xFFFFFF);
    if (std::isfinite(glossiness) && glossiness >= 0.0F) {
        m_editAppearance->edited.glossiness = glossiness;
    }
    if (std::isfinite(specularStrength) && specularStrength >= 0.0F) {
        m_editAppearance->edited.specularStrength = specularStrength;
    }
    if (std::isfinite(emissiveMult) && emissiveMult >= 0.0F) {
        m_editAppearance->edited.emissiveMult = emissiveMult;
    }
    if (previous != m_editAppearance->edited) {
        ++m_editAppearance->editRevision;
        if (!m_activeAppearanceGeneration) {
            m_error.reset();
            updateLivePreviewStatus();
        }
    }
}

bool NativeSlotWorkflowModel::loadAppearancePreset(
    const runtime::AppearancePreset& preset) {
    if (m_screen != SlotWorkflowScreen::editAppearance || !m_editAppearance ||
        m_editAppearance->mode == core::UpdateTattooAppearanceMode::synchronizeOnly ||
        m_editAppearance->exitIntent != AppearanceExitIntent::none) {
        return false;
    }
    setEditedAppearance(
        static_cast<std::int32_t>(preset.color),
        preset.alpha,
        static_cast<std::int32_t>(preset.glow),
        preset.glossiness,
        preset.specularStrength,
        preset.emissiveMult);
    return true;
}

const runtime::AppearancePresetList& NativeSlotWorkflowModel::appearancePresets() const noexcept {
    return m_appearancePresets;
}

std::optional<std::size_t> NativeSlotWorkflowModel::selectedAppearancePreset() const noexcept {
    return m_selectedAppearancePreset;
}

void NativeSlotWorkflowModel::selectAppearancePreset(
    const std::optional<std::size_t> index) {
    m_selectedAppearancePreset = index && *index < m_appearancePresets.size()
        ? index : std::nullopt;
}

void NativeSlotWorkflowModel::advanceLivePreview(std::chrono::steady_clock::time_point now) {
    if (!m_editAppearance) {
        return;
    }
    auto& session = *m_editAppearance;
    if (session.observedRevision != session.editRevision) {
        session.observedRevision = session.editRevision;
        session.latestEditTime = now;
    }
    if (m_activeAppearanceGeneration || session.exitIntent != AppearanceExitIntent::none ||
        session.status != LivePreviewStatus::pending ||
        !session.latestEditTime || now - *session.latestEditTime < std::chrono::milliseconds(1000)) {
        return;
    }
    (void)queueAppearanceOperation(AppearanceOperationPurpose::preview, session.edited);
}

LivePreviewStatus NativeSlotWorkflowModel::livePreviewStatus() const noexcept {
    return m_editAppearance ? m_editAppearance->status : LivePreviewStatus::clean;
}

void NativeSlotWorkflowModel::updateLivePreviewStatus() noexcept {
    auto& session = *m_editAppearance;
    const auto& current = session.lastPreviewed ? *session.lastPreviewed : session.original;
    session.status = session.edited != current ? LivePreviewStatus::pending
        : session.lastPreviewed ? LivePreviewStatus::applied : LivePreviewStatus::clean;
}

void NativeSlotWorkflowModel::cancelEditAppearance() {
    (void)requestAppearanceRollback(AppearanceExitIntent::cancel);
}

bool NativeSlotWorkflowModel::confirmAppearanceUpdate() {
    if (!m_editAppearance || m_screen != SlotWorkflowScreen::editAppearance ||
        !hasActorSnapshot() || m_editAppearance->actorFormId != m_actorTarget->formId ||
        m_editAppearance->targetGeneration != m_targetGeneration ||
        m_editAppearance->exitIntent == AppearanceExitIntent::cancel ||
        m_editAppearance->exitIntent == AppearanceExitIntent::close) {
        return false;
    }
    auto& session = *m_editAppearance;
    if (session.exitIntent == AppearanceExitIntent::save &&
        session.status != LivePreviewStatus::previewError) {
        return false;
    }
    session.exitIntent = AppearanceExitIntent::save;
    if (m_activeAppearanceGeneration) {
        return true;
    }
    if (session.status == LivePreviewStatus::previewError) {
        return retryLivePreviewOperation();
    }
    continueAppearanceExit();
    return true;
}

bool NativeSlotWorkflowModel::requestEditAppearanceClose() {
    return requestAppearanceRollback(AppearanceExitIntent::close);
}

bool NativeSlotWorkflowModel::takeMenuCloseRequest() {
    return std::exchange(m_menuCloseRequested, false);
}

bool NativeSlotWorkflowModel::requestAppearanceRollback(AppearanceExitIntent intent) {
    if (!m_editAppearance || m_screen != SlotWorkflowScreen::editAppearance ||
        m_editAppearance->exitIntent == AppearanceExitIntent::cancel ||
        m_editAppearance->exitIntent == AppearanceExitIntent::close ||
        (m_activeAppearanceGeneration &&
            m_editAppearance->exitIntent != AppearanceExitIntent::none)) {
        return false;
    }
    m_editAppearance->exitIntent = intent;
    if (!m_activeAppearanceGeneration) {
        continueAppearanceExit();
    }
    return true;
}

void NativeSlotWorkflowModel::continueAppearanceExit() {
    auto& session = *m_editAppearance;
    if (session.exitIntent == AppearanceExitIntent::cancel ||
        session.exitIntent == AppearanceExitIntent::close) {
        if (!session.appearanceWritten) {
            finishAppearanceSession(false);
            return;
        }
        session.mode = core::UpdateTattooAppearanceMode::updateAndSynchronize;
        (void)queueAppearanceOperation(AppearanceOperationPurpose::restore, session.original);
    } else if (session.exitIntent == AppearanceExitIntent::save) {
        if ((session.lastPreviewed && session.edited == *session.lastPreviewed) ||
            (!session.appearanceWritten && session.edited == session.original)) {
            finishAppearanceSession(true);
            return;
        }
        session.mode = core::UpdateTattooAppearanceMode::updateAndSynchronize;
        (void)queueAppearanceOperation(AppearanceOperationPurpose::commit, session.edited);
    }
}

void NativeSlotWorkflowModel::finishAppearanceSession(bool saved) {
    const auto area = m_editAppearance->area;
    m_menuCloseRequested = m_editAppearance->exitIntent == AppearanceExitIntent::close;
    m_editAppearance.reset();
    m_selectedAppearancePreset.reset();
    m_pendingAppearance.reset();
    m_error.reset();
    m_screen = saved ? SlotWorkflowScreen::currentSlots : SlotWorkflowScreen::slotActions;
    if (saved) {
        m_targetSlot.reset();
        scheduleSlotQuery(area);
    }
}

bool NativeSlotWorkflowModel::retryLivePreviewOperation() {
    if (!m_editAppearance || m_activeAppearanceGeneration ||
        (m_editAppearance->status != LivePreviewStatus::previewError &&
            m_editAppearance->status != LivePreviewStatus::restoreError)) {
        return false;
    }
    return queueAppearanceOperation(
        m_editAppearance->activePurpose, m_editAppearance->operationAppearance);
}

bool NativeSlotWorkflowModel::queueAppearanceOperation(
    AppearanceOperationPurpose purpose,
    const TattooAppearance& appearance) {
    if (!m_editAppearance || m_activeAppearanceGeneration || m_outstandingMutationGeneration ||
        !hasActorSnapshot() || m_editAppearance->targetGeneration != m_targetGeneration ||
        m_editAppearance->actorFormId != m_actorTarget->formId) {
        return false;
    }
    const std::uint64_t generation = nextGeneration();
    m_pendingAppearance = SlotAppearanceTicket{
        .generation = generation,
        .request = core::UpdateTattooAppearanceRequest{
            .actorFormId = m_editAppearance->actorFormId,
            .runtimeHandle = m_editAppearance->runtimeHandle,
            .color = appearance.color,
            .alpha = appearance.alpha,
            .glow = appearance.glow,
            .glossiness = appearance.glossiness,
            .specularStrength = appearance.specularStrength,
            .emissiveMult = appearance.emissiveMult,
            .mode = m_editAppearance->mode,
        },
        .purpose = purpose,
    };
    m_editAppearance->activePurpose = purpose;
    m_editAppearance->operationAppearance = appearance;
    m_editAppearance->status = purpose == AppearanceOperationPurpose::restore
        ? LivePreviewStatus::restoring : LivePreviewStatus::updating;
    m_activeAppearanceGeneration = generation;
    m_error.reset();
    m_screen = purpose == AppearanceOperationPurpose::commit
        ? SlotWorkflowScreen::savingAppearance : SlotWorkflowScreen::editAppearance;
    return true;
}

std::optional<SlotQueryTicket> NativeSlotWorkflowModel::takeSlotQuery() {
    auto ticket = std::move(m_pendingSlotQuery);
    m_pendingSlotQuery.reset();
    return ticket;
}

std::optional<SlotApplyTicket> NativeSlotWorkflowModel::takeApplyRequest() {
    auto ticket = std::move(m_pendingApply);
    m_pendingApply.reset();
    if (ticket) {
        m_outstandingMutationGeneration = ticket->generation;
        m_activeApplyRecentIdentity = ticket->recentIdentity;
    }
    return ticket;
}

std::optional<SlotRemoveTicket> NativeSlotWorkflowModel::takeRemoveRequest() {
    auto ticket = std::move(m_pendingRemove);
    m_pendingRemove.reset();
    if (ticket) {
        m_outstandingMutationGeneration = ticket->generation;
    }
    return ticket;
}

std::optional<SlotAppearanceTicket> NativeSlotWorkflowModel::takeAppearanceRequest() {
    auto ticket = std::move(m_pendingAppearance);
    m_pendingAppearance.reset();
    if (ticket) {
        m_outstandingMutationGeneration = ticket->generation;
    }
    return ticket;
}

std::optional<SlotLockTicket> NativeSlotWorkflowModel::takeLockRequest() {
    auto ticket = std::move(m_pendingLock);
    m_pendingLock.reset();
    if (ticket) {
        m_outstandingMutationGeneration = ticket->generation;
    }
    return ticket;
}

bool NativeSlotWorkflowModel::requestFavorite(
    const repository::TattooDefinition& tattoo,
    const bool enabled) {
    if (m_activeFavoriteRequestId || m_pendingFavorite) {
        return false;
    }
    const auto identity = repository::favoriteIdentity(tattoo);
    m_favoriteError.reset();
    m_failedFavorite.reset();
    m_pendingFavorite = FavoriteTicket{
        .requestId = ++m_favoriteRequestId,
        .identity = identity,
        .enabled = enabled,
    };
    return true;
}

bool NativeSlotWorkflowModel::retryFavorite() {
    if (!m_failedFavorite || m_activeFavoriteRequestId || m_pendingFavorite) {
        return false;
    }
    auto retry = *m_failedFavorite;
    retry.requestId = ++m_favoriteRequestId;
    m_favoriteError.reset();
    m_pendingFavorite = std::move(retry);
    return true;
}

void NativeSlotWorkflowModel::initializeRecentTattoos() {
    if (m_pendingRecentTattoo || m_activeRecentTattooRequestId) {
        return;
    }
    m_recentTattooError.reset();
    m_failedRecentTattoo.reset();
    m_pendingRecentTattoo = RecentTattooTicket{
        .requestId = ++m_recentTattooRequestId,
        .kind = RecentTattooRequestKind::load,
    };
}

bool NativeSlotWorkflowModel::retryRecentTattoo() {
    if (!m_failedRecentTattoo || m_pendingRecentTattoo ||
        m_activeRecentTattooRequestId) {
        return false;
    }
    auto retry = *m_failedRecentTattoo;
    retry.requestId = ++m_recentTattooRequestId;
    m_recentTattooError.reset();
    m_pendingRecentTattoo = std::move(retry);
    return true;
}

void NativeSlotWorkflowModel::initializeAppearancePresets() {
    if (appearancePresetPending()) {
        return;
    }
    m_appearancePresetError.reset();
    m_failedAppearancePreset.reset();
    (void)queueAppearancePresetRequest(AppearancePresetTicket{
        .kind = AppearancePresetRequestKind::load,
    });
}

bool NativeSlotWorkflowModel::requestCreateAppearancePreset(std::string name) {
    if (!m_editAppearance || m_screen != SlotWorkflowScreen::editAppearance ||
        appearancePresetPending() || m_appearancePresetOverwriteConfirmation ||
        m_appearancePresetDeleteConfirmation) {
        return false;
    }
    name = trimASCII(std::move(name));
    if (name.empty()) {
        m_appearancePresetError = runtime::ConfigError{
            .message = "Appearance preset name must not be empty."};
        return false;
    }
    runtime::AppearancePreset preset{
        .name = std::move(name),
        .color = static_cast<std::uint32_t>(m_editAppearance->edited.color),
        .alpha = m_editAppearance->edited.alpha,
        .glow = static_cast<std::uint32_t>(m_editAppearance->edited.glow),
        .emissiveMult = m_editAppearance->edited.emissiveMult,
        .glossiness = m_editAppearance->edited.glossiness,
        .specularStrength = m_editAppearance->edited.specularStrength,
    };
    const auto duplicate = std::ranges::find_if(m_appearancePresets,
        [&](const runtime::AppearancePreset& existing) {
            return equalsFoldedASCII(existing.name, preset.name);
        });
    m_appearancePresetError.reset();
    if (duplicate != m_appearancePresets.end()) {
        m_appearancePresetOverwriteConfirmation = std::move(preset);
        return true;
    }
    if (m_appearancePresets.size() >= runtime::kAppearancePresetLimit) {
        m_appearancePresetError = runtime::ConfigError{
            .message = "Appearance preset limit reached."};
        return false;
    }
    return queueAppearancePresetRequest(AppearancePresetTicket{
        .kind = AppearancePresetRequestKind::create,
        .preset = std::move(preset),
    });
}

bool NativeSlotWorkflowModel::confirmAppearancePresetOverwrite() {
    if (!m_appearancePresetOverwriteConfirmation || appearancePresetPending()) {
        return false;
    }
    auto preset = std::move(*m_appearancePresetOverwriteConfirmation);
    m_appearancePresetOverwriteConfirmation.reset();
    return queueAppearancePresetRequest(AppearancePresetTicket{
        .kind = AppearancePresetRequestKind::overwrite,
        .preset = std::move(preset),
    });
}

bool NativeSlotWorkflowModel::requestRenameAppearancePreset(std::string newName) {
    if (!m_selectedAppearancePreset || *m_selectedAppearancePreset >= m_appearancePresets.size() ||
        appearancePresetPending() || m_appearancePresetOverwriteConfirmation ||
        m_appearancePresetDeleteConfirmation) {
        return false;
    }
    newName = trimASCII(std::move(newName));
    if (newName.empty()) {
        m_appearancePresetError = runtime::ConfigError{
            .message = "Appearance preset name must not be empty."};
        return false;
    }
    const auto selected = *m_selectedAppearancePreset;
    for (std::size_t index = 0; index < m_appearancePresets.size(); ++index) {
        if (index != selected && equalsFoldedASCII(m_appearancePresets[index].name, newName)) {
            m_appearancePresetError = runtime::ConfigError{
                .message = "Appearance preset name already exists."};
            return false;
        }
    }
    m_appearancePresetError.reset();
    return queueAppearancePresetRequest(AppearancePresetTicket{
        .kind = AppearancePresetRequestKind::rename,
        .preset = runtime::AppearancePreset{.name = std::move(newName)},
        .existingName = m_appearancePresets[selected].name,
    });
}

bool NativeSlotWorkflowModel::requestDeleteAppearancePreset() {
    if (!m_selectedAppearancePreset || *m_selectedAppearancePreset >= m_appearancePresets.size() ||
        appearancePresetPending() || m_appearancePresetOverwriteConfirmation) {
        return false;
    }
    m_appearancePresetDeleteConfirmation = true;
    m_appearancePresetError.reset();
    return true;
}

bool NativeSlotWorkflowModel::confirmAppearancePresetDelete() {
    if (!m_appearancePresetDeleteConfirmation || !m_selectedAppearancePreset ||
        *m_selectedAppearancePreset >= m_appearancePresets.size() || appearancePresetPending()) {
        return false;
    }
    const auto name = m_appearancePresets[*m_selectedAppearancePreset].name;
    m_appearancePresetDeleteConfirmation = false;
    return queueAppearancePresetRequest(AppearancePresetTicket{
        .kind = AppearancePresetRequestKind::erase,
        .existingName = name,
    });
}

void NativeSlotWorkflowModel::cancelAppearancePresetConfirmation() noexcept {
    m_appearancePresetOverwriteConfirmation.reset();
    m_appearancePresetDeleteConfirmation = false;
}

bool NativeSlotWorkflowModel::retryAppearancePreset() {
    if (!m_failedAppearancePreset || appearancePresetPending()) {
        return false;
    }
    auto retry = *m_failedAppearancePreset;
    retry.requestId = 0;
    m_appearancePresetError.reset();
    return queueAppearancePresetRequest(std::move(retry));
}

bool NativeSlotWorkflowModel::queueAppearancePresetRequest(AppearancePresetTicket ticket) {
    if (m_pendingAppearancePreset || m_activeAppearancePresetRequestId) {
        return false;
    }
    ticket.requestId = ++m_appearancePresetRequestId;
    m_pendingAppearancePreset = std::move(ticket);
    return true;
}

std::optional<FavoriteTicket> NativeSlotWorkflowModel::takeFavoriteRequest() {
    auto ticket = std::move(m_pendingFavorite);
    m_pendingFavorite.reset();
    if (ticket) {
        m_activeFavoriteRequestId = ticket->requestId;
        m_activeFavorite = *ticket;
    }
    return ticket;
}

std::optional<RecentTattooTicket> NativeSlotWorkflowModel::takeRecentTattooRequest() {
    auto ticket = std::move(m_pendingRecentTattoo);
    m_pendingRecentTattoo.reset();
    if (ticket) {
        m_activeRecentTattooRequestId = ticket->requestId;
        m_activeRecentTattoo = *ticket;
    }
    return ticket;
}

std::optional<AppearancePresetTicket> NativeSlotWorkflowModel::takeAppearancePresetRequest() {
    auto ticket = std::move(m_pendingAppearancePreset);
    m_pendingAppearancePreset.reset();
    if (ticket) {
        m_activeAppearancePresetRequestId = ticket->requestId;
        m_activeAppearancePreset = *ticket;
    }
    return ticket;
}

void NativeSlotWorkflowModel::completeSlotQuery(
    std::uint64_t generation,
    core::TattooSlotsResult result) {
    if (!m_activeSlotQueryGeneration || generation != *m_activeSlotQueryGeneration ||
        generation <= m_targetGeneration || !m_actorTarget) {
        return;
    }

    m_activeSlotQueryGeneration.reset();
    if (!result) {
        m_error = std::move(result.error());
        return;
    }

    auto completed = std::move(result.value());
    if (completed.actorFormId != m_actorTarget->formId) {
        m_error = core::ServiceError{
            core::ServiceErrorCode::slotQueryFailed, "Slot snapshot belongs to a different Actor"};
        return;
    }
    const auto completedArea = completed.area;
    m_areaStates[areaIndex(completedArea)].slots = std::move(completed);
    if (completedArea == m_selectedArea) {
        updateAppliedTattooIdentities();
    }
    m_error.reset();
    clampSelectedPage();
}

void NativeSlotWorkflowModel::completeApply(
    std::uint64_t generation,
    core::ApplyTattooResult result) {
    finishOutstandingMutation(generation);
    if (!m_activeApplyGeneration || generation != *m_activeApplyGeneration ||
        generation <= m_targetGeneration) {
        return;
    }

    m_activeApplyGeneration.reset();
    if (!result) {
        m_error = std::move(result.error());
        m_applyRequiresSynchronizationOnly =
            m_error->code == core::ServiceErrorCode::synchronizeFailed ||
            m_error->mutationSideEffect == core::MutationSideEffect::mayHaveOccurred;
        m_screen = SlotWorkflowScreen::preview;
        return;
    }

    if (m_activeApplyRecentIdentity) {
        enqueueRecentTattoo(std::move(*m_activeApplyRecentIdentity));
    }
    m_activeApplyRecentIdentity.reset();
    m_error.reset();
    m_applyRequiresSynchronizationOnly = false;
    m_previewTattoo.reset();
    m_previewAppearance.reset();
    m_targetSlot.reset();
    m_screen = SlotWorkflowScreen::currentSlots;
    scheduleSlotQuery(m_selectedArea);
}

void NativeSlotWorkflowModel::completeRemove(
    std::uint64_t generation,
    core::RemoveTattooResult result) {
    finishOutstandingMutation(generation);
    if (!m_activeRemoveGeneration || generation != *m_activeRemoveGeneration ||
        generation <= m_targetGeneration) {
        return;
    }

    m_activeRemoveGeneration.reset();
    if (!result) {
        m_error = std::move(result.error());
        if (m_error->code == core::ServiceErrorCode::synchronizeFailed ||
            m_removeRequiresSynchronizationOnly) {
            m_removeRequiresSynchronizationOnly = true;
        }
        m_screen = SlotWorkflowScreen::removeConfirmation;
        return;
    }

    m_error.reset();
    m_removeRequiresSynchronizationOnly = false;
    m_targetSlot.reset();
    m_screen = SlotWorkflowScreen::currentSlots;
    scheduleSlotQuery(m_selectedArea);
}

void NativeSlotWorkflowModel::completeAppearanceUpdate(
    std::uint64_t generation,
    core::UpdateTattooAppearanceResult result) {
    finishOutstandingMutation(generation);
    if (!m_activeAppearanceGeneration || generation != *m_activeAppearanceGeneration ||
        generation <= m_targetGeneration || !m_editAppearance ||
        m_editAppearance->targetGeneration != m_targetGeneration || !m_actorTarget ||
        m_editAppearance->actorFormId != m_actorTarget->formId) {
        return;
    }

    m_activeAppearanceGeneration.reset();
    m_pendingAppearance.reset();
    auto& session = *m_editAppearance;
    const auto purpose = session.activePurpose;
    if (!result) {
        m_error = std::move(result.error());
        if (session.mode == core::UpdateTattooAppearanceMode::synchronizeOnly ||
            m_error->code == core::ServiceErrorCode::synchronizeFailed) {
            session.mode = core::UpdateTattooAppearanceMode::synchronizeOnly;
            session.appearanceWritten = true;
        } else if (m_error->mutationSideEffect ==
            core::MutationSideEffect::mayHaveOccurred) {
            session.appearanceWritten = true;
        }
        session.status = purpose == AppearanceOperationPurpose::restore
            ? LivePreviewStatus::restoreError : LivePreviewStatus::previewError;
        m_screen = SlotWorkflowScreen::editAppearance;
        if (purpose != AppearanceOperationPurpose::restore &&
            (session.exitIntent == AppearanceExitIntent::cancel ||
                session.exitIntent == AppearanceExitIntent::close)) {
            continueAppearanceExit();
        }
        return;
    }

    m_error.reset();
    session.mode = core::UpdateTattooAppearanceMode::updateAndSynchronize;
    session.appearanceWritten = true;
    if (purpose == AppearanceOperationPurpose::preview) {
        session.lastPreviewed = session.operationAppearance;
        if (session.exitIntent == AppearanceExitIntent::none) {
            updateLivePreviewStatus();
        } else {
            continueAppearanceExit();
        }
        return;
    }
    finishAppearanceSession(purpose == AppearanceOperationPurpose::commit);
}

void NativeSlotWorkflowModel::completeLockStateChange(
    std::uint64_t generation,
    core::SetTattooLockedResult result) {
    finishOutstandingMutation(generation);
    if (!m_activeLockGeneration || generation != *m_activeLockGeneration ||
        generation <= m_targetGeneration) {
        return;
    }
    m_activeLockGeneration.reset();
    const auto originScreen = m_lockOriginScreen.value_or(SlotWorkflowScreen::slotActions);
    m_lockOriginScreen.reset();
    if (!result) {
        m_error = std::move(result.error());
        m_screen = originScreen;
        if (originScreen == SlotWorkflowScreen::currentSlots) {
            m_targetSlot.reset();
        }
        return;
    }
    m_error.reset();
    m_targetSlot.reset();
    m_previewTattoo.reset();
    m_previewAppearance.reset();
    m_screen = SlotWorkflowScreen::currentSlots;
    scheduleSlotQuery(m_selectedArea);
}

void NativeSlotWorkflowModel::completeFavorite(
    const std::uint64_t requestId,
    runtime::FavoriteResult result) {
    if (!m_activeFavoriteRequestId || requestId != *m_activeFavoriteRequestId) {
        return;
    }
    m_activeFavoriteRequestId.reset();
    if (!result) {
        m_favoriteError = std::move(result.error());
        m_failedFavorite = std::move(m_activeFavorite);
        return;
    }
    m_activeFavorite.reset();
    m_catalog.setFavoriteIdentities(std::move(*result));
    m_favoriteError.reset();
    m_failedFavorite.reset();
}

void NativeSlotWorkflowModel::completeRecentTattoo(
    const std::uint64_t requestId,
    runtime::RecentTattooResult result) {
    if (!m_activeRecentTattooRequestId || requestId != *m_activeRecentTattooRequestId) {
        return;
    }
    m_activeRecentTattooRequestId.reset();
    if (!result) {
        m_recentTattooError = std::move(result.error());
        m_failedRecentTattoo = std::move(m_activeRecentTattoo);
        return;
    }
    m_catalog.setRecentTattooIdentities(std::move(*result));
    m_activeRecentTattoo.reset();
    m_failedRecentTattoo.reset();
    m_recentTattooError.reset();
    queueNextRecentTattoo();
}

void NativeSlotWorkflowModel::completeAppearancePreset(
    const std::uint64_t requestId,
    runtime::AppearancePresetResult result) {
    if (!m_activeAppearancePresetRequestId ||
        requestId != *m_activeAppearancePresetRequestId) {
        return;
    }
    m_activeAppearancePresetRequestId.reset();
    if (!result) {
        m_appearancePresetError = std::move(result.error());
        m_failedAppearancePreset = std::move(m_activeAppearancePreset);
        return;
    }

    const auto completed = std::move(m_activeAppearancePreset);
    m_appearancePresets = std::move(*result);
    m_activeAppearancePreset.reset();
    m_failedAppearancePreset.reset();
    m_appearancePresetError.reset();
    m_selectedAppearancePreset.reset();
    if (completed && completed->kind != AppearancePresetRequestKind::load &&
        completed->kind != AppearancePresetRequestKind::erase && completed->preset) {
        const auto found = std::ranges::find_if(m_appearancePresets,
            [&](const runtime::AppearancePreset& preset) {
                return equalsFoldedASCII(preset.name, completed->preset->name);
            });
        if (found != m_appearancePresets.end()) {
            m_selectedAppearancePreset = static_cast<std::size_t>(
                std::distance(m_appearancePresets.begin(), found));
        }
    }
}

SlotWorkflowScreen NativeSlotWorkflowModel::screen() const noexcept {
    return m_screen;
}

core::TattooArea NativeSlotWorkflowModel::selectedArea() const noexcept {
    return m_selectedArea;
}

const core::TattooSlots* NativeSlotWorkflowModel::slots() const noexcept {
    const auto& value = selectedState().slots;
    return value ? &*value : nullptr;
}

std::size_t NativeSlotWorkflowModel::slotPageIndex() const noexcept {
    return selectedState().pageIndex;
}

std::size_t NativeSlotWorkflowModel::slotPageCount() const noexcept {
    const auto* current = slots();
    if (!current || current->slots.empty()) {
        return 0;
    }
    return current->slots.size() / kPageSize +
        (current->slots.size() % kPageSize != 0 ? 1 : 0);
}

std::optional<std::int32_t> NativeSlotWorkflowModel::targetSlot() const noexcept {
    return m_targetSlot;
}

const repository::TattooDefinition* NativeSlotWorkflowModel::previewTattoo() const noexcept {
    return m_previewTattoo ? &*m_previewTattoo : nullptr;
}

const PreviewTattooAppearance* NativeSlotWorkflowModel::previewAppearance() const noexcept {
    return m_previewAppearance ? &*m_previewAppearance : nullptr;
}

const AppearanceEditSession* NativeSlotWorkflowModel::editAppearance() const noexcept {
    return m_editAppearance ? &*m_editAppearance : nullptr;
}

bool NativeSlotWorkflowModel::canSaveAppearance() const noexcept {
    return m_screen == SlotWorkflowScreen::editAppearance && m_editAppearance &&
        hasActorSnapshot() &&
        m_editAppearance->actorFormId == m_actorTarget->formId &&
        m_editAppearance->targetGeneration == m_targetGeneration &&
        (m_editAppearance->exitIntent == AppearanceExitIntent::none ||
            (m_editAppearance->exitIntent == AppearanceExitIntent::save &&
                m_editAppearance->status == LivePreviewStatus::previewError)) &&
        (m_editAppearance->edited != m_editAppearance->original ||
            m_editAppearance->appearanceWritten ||
            m_editAppearance->status == LivePreviewStatus::previewError);
}

bool NativeSlotWorkflowModel::isLockStateChangeInFlight() const noexcept {
    return m_pendingLock.has_value() || m_activeLockGeneration.has_value();
}

void NativeSlotWorkflowModel::setAppliedOnly(bool value) {
    updateAppliedTattooIdentities();
    m_catalog.setAppliedOnly(value);
}

bool NativeSlotWorkflowModel::appliedOnly() const noexcept {
    return m_catalog.appliedOnly();
}

void NativeSlotWorkflowModel::setFavoritesOnly(const bool value) {
    m_catalog.setFavoritesOnly(value);
}

bool NativeSlotWorkflowModel::favoritesOnly() const noexcept {
    return m_catalog.favoritesOnly();
}

void NativeSlotWorkflowModel::setRecentlyUsedOnly(const bool value) {
    m_catalog.setRecentlyUsedOnly(value);
}

bool NativeSlotWorkflowModel::recentlyUsedOnly() const noexcept {
    return m_catalog.recentlyUsedOnly();
}

bool NativeSlotWorkflowModel::recentlyUsedPending() const noexcept {
    return m_pendingRecentTattoo.has_value() || m_activeRecentTattooRequestId.has_value();
}

const runtime::ConfigError* NativeSlotWorkflowModel::recentlyUsedError() const noexcept {
    return m_recentTattooError ? &*m_recentTattooError : nullptr;
}

bool NativeSlotWorkflowModel::appearancePresetPending() const noexcept {
    return m_pendingAppearancePreset.has_value() ||
        m_activeAppearancePresetRequestId.has_value();
}

const runtime::ConfigError* NativeSlotWorkflowModel::appearancePresetError() const noexcept {
    return m_appearancePresetError ? &*m_appearancePresetError : nullptr;
}

bool NativeSlotWorkflowModel::appearancePresetRetryAvailable() const noexcept {
    return m_failedAppearancePreset.has_value() && !appearancePresetPending();
}

const runtime::AppearancePreset*
NativeSlotWorkflowModel::appearancePresetOverwriteConfirmation() const noexcept {
    return m_appearancePresetOverwriteConfirmation ?
        &*m_appearancePresetOverwriteConfirmation : nullptr;
}

bool NativeSlotWorkflowModel::appearancePresetDeleteConfirmation() const noexcept {
    return m_appearancePresetDeleteConfirmation;
}

bool NativeSlotWorkflowModel::favoritePending() const noexcept {
    return m_pendingFavorite.has_value() || m_activeFavoriteRequestId.has_value();
}

const runtime::ConfigError* NativeSlotWorkflowModel::favoriteError() const noexcept {
    return m_favoriteError ? &*m_favoriteError : nullptr;
}

void NativeSlotWorkflowModel::enqueueRecentTattoo(
    repository::RecentTattooIdentity identity) {
    if (!m_recentTattooQueue.empty() && m_recentTattooQueue.back() == identity) {
        return;
    }
    m_recentTattooQueue.push_back(std::move(identity));
    queueNextRecentTattoo();
}

void NativeSlotWorkflowModel::queueNextRecentTattoo() {
    if (m_pendingRecentTattoo || m_activeRecentTattooRequestId ||
        m_failedRecentTattoo || m_recentTattooQueue.empty()) {
        return;
    }
    auto identity = std::move(m_recentTattooQueue.front());
    m_recentTattooQueue.pop_front();
    m_pendingRecentTattoo = RecentTattooTicket{
        .requestId = ++m_recentTattooRequestId,
        .kind = RecentTattooRequestKind::record,
        .identity = std::move(identity),
    };
}

void NativeSlotWorkflowModel::updateAppliedTattooIdentities() {
    std::vector<repository::TattooIdentity> identities;
    const auto* currentSlots = slots();
    if (currentSlots) {
        for (const auto& slot : currentSlots->slots) {
            if (slot.occupancy == core::SlotOccupancy::slaveTats && slot.tattoo) {
                identities.push_back(repository::TattooIdentity{
                    .section = slot.tattoo->section,
                    .name = slot.tattoo->name,
                });
            }
        }
    }
    m_catalog.setAppliedTattooIdentities(std::move(identities));
}

std::vector<std::int32_t> NativeSlotWorkflowModel::inUseSlots(
    const repository::TattooDefinition& tattoo) const {
    std::vector<std::int32_t> matches;
    const auto* currentSlots = slots();
    if (!currentSlots) {
        return matches;
    }

    for (const auto& slot : currentSlots->slots) {
        if (slot.occupancy == core::SlotOccupancy::slaveTats && slot.tattoo &&
            slot.tattoo->section == tattoo.section && slot.tattoo->name == tattoo.name) {
            matches.push_back(slot.index);
        }
    }
    return matches;
}

const core::ServiceError* NativeSlotWorkflowModel::error() const noexcept {
    return m_error ? &*m_error : nullptr;
}

std::size_t NativeSlotWorkflowModel::areaIndex(core::TattooArea area) noexcept {
    switch (area) {
    case core::TattooArea::body:
        return 0;
    case core::TattooArea::face:
        return 1;
    case core::TattooArea::hands:
        return 2;
    case core::TattooArea::feet:
        return 3;
    }
    return 0;
}

NativeSlotWorkflowModel::AreaState& NativeSlotWorkflowModel::selectedState() noexcept {
    return m_areaStates[areaIndex(m_selectedArea)];
}

const NativeSlotWorkflowModel::AreaState& NativeSlotWorkflowModel::selectedState() const noexcept {
    return m_areaStates[areaIndex(m_selectedArea)];
}

std::uint64_t NativeSlotWorkflowModel::nextGeneration() noexcept {
    return ++m_generation;
}

void NativeSlotWorkflowModel::scheduleSlotQuery(core::TattooArea area) {
    if (!m_actorTarget || isActorTargetResolutionInFlight()) {
        return;
    }
    const std::uint64_t generation = nextGeneration();
    m_pendingSlotQuery = SlotQueryTicket{
        .generation = generation,
        .actorFormId = m_actorTarget->formId,
        .area = area,
    };
    m_activeSlotQueryGeneration = generation;
    m_error.reset();
}

void NativeSlotWorkflowModel::invalidateActorState() {
    m_targetGeneration = nextGeneration();
    m_actorTarget.reset();
    m_pendingActorTarget.reset();
    m_activeActorTargetGeneration.reset();
    m_areaStates = {};
    m_screen = SlotWorkflowScreen::currentSlots;
    m_targetSlot.reset();
    m_previewTattoo.reset();
    m_previewAppearance.reset();
    m_editAppearance.reset();
    m_error.reset();
    m_pendingSlotQuery.reset();
    m_activeSlotQueryGeneration.reset();
    m_pendingApply.reset();
    m_activeApplyGeneration.reset();
    m_activeApplyRecentIdentity.reset();
    m_applyRequiresSynchronizationOnly = false;
    m_pendingRemove.reset();
    m_activeRemoveGeneration.reset();
    m_removeRequiresSynchronizationOnly = false;
    m_pendingAppearance.reset();
    m_activeAppearanceGeneration.reset();
    m_pendingLock.reset();
    m_activeLockGeneration.reset();
    m_lockOriginScreen.reset();
}

bool NativeSlotWorkflowModel::isMutationInFlight() const noexcept {
    return hasAppearanceTransactionWork() || m_outstandingMutationGeneration ||
        m_pendingApply || m_activeApplyGeneration ||
        m_pendingRemove || m_activeRemoveGeneration ||
        m_pendingAppearance || m_activeAppearanceGeneration ||
        m_pendingLock || m_activeLockGeneration;
}

bool NativeSlotWorkflowModel::hasAppearanceTransactionWork() const noexcept {
    return m_menuCloseRequested || (m_editAppearance &&
        (m_editAppearance->edited != m_editAppearance->original ||
            m_editAppearance->appearanceWritten || m_activeAppearanceGeneration ||
            m_editAppearance->status == LivePreviewStatus::previewError ||
            m_editAppearance->status == LivePreviewStatus::restoreError ||
            m_editAppearance->exitIntent != AppearanceExitIntent::none));
}

bool NativeSlotWorkflowModel::hasActorSnapshot() const noexcept {
    const auto* current = slots();
    return m_actorTarget && !isActorTargetResolutionInFlight() && current &&
        current->actorFormId == m_actorTarget->formId;
}

void NativeSlotWorkflowModel::finishOutstandingMutation(std::uint64_t generation) noexcept {
    if (m_outstandingMutationGeneration == generation) {
        m_outstandingMutationGeneration.reset();
    }
}

void NativeSlotWorkflowModel::clampSelectedPage() noexcept {
    auto& state = selectedState();
    const std::size_t pageCount = slotPageCount();
    state.pageIndex = pageCount == 0 ? 0 : std::min(state.pageIndex, pageCount - 1);
}

}  // namespace stui::native
