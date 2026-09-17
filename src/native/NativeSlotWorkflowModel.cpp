#include "native/NativeSlotWorkflowModel.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace stui::native {
namespace {

constexpr std::uint32_t kPlayerFormId = 0x14;

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

void NativeSlotWorkflowModel::selectArea(core::TattooArea area) {
    if (area == m_selectedArea) {
        return;
    }

    m_selectedArea = area;
    m_screen = SlotWorkflowScreen::currentSlots;
    m_targetSlot.reset();
    m_previewTattoo.reset();
    m_previewAppearance.reset();
    m_pendingAppearance.reset();
    m_activeAppearanceGeneration.reset();
    m_pendingLock.reset();
    m_activeLockGeneration.reset();
    m_editAppearance.reset();
    m_error.reset();
    clampSelectedPage();
    if (m_started && !selectedState().slots) {
        scheduleSlotQuery(area);
    }
}

void NativeSlotWorkflowModel::refreshSelectedArea() {
    if (!m_started) {
        return;
    }
    scheduleSlotQuery(m_selectedArea);
}

void NativeSlotWorkflowModel::previousSlotPage() {
    auto& state = selectedState();
    if (state.pageIndex > 0) {
        --state.pageIndex;
    }
}

void NativeSlotWorkflowModel::nextSlotPage() {
    auto& state = selectedState();
    const std::size_t pageCount = slotPageCount();
    if (pageCount > 0 && state.pageIndex + 1 < pageCount) {
        ++state.pageIndex;
    }
}

void NativeSlotWorkflowModel::setSlotPageNumber(std::size_t oneBasedPage) {
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
    if (!current) {
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
        m_pendingLock || m_activeLockGeneration) {
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
        m_pendingLock || m_activeLockGeneration) {
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
    if (m_screen != SlotWorkflowScreen::removeConfirmation || !m_targetSlot) {
        return false;
    }

    const auto mode = m_error &&
            m_error->code == core::ServiceErrorCode::synchronizeFailed
        ? core::RemoveTattooMode::synchronizeOnly
        : core::RemoveTattooMode::removeAndSynchronize;
    const std::uint64_t generation = nextGeneration();
    m_pendingRemove = SlotRemoveTicket{
        .generation = generation,
        .request = core::RemoveTattooRequest{
            .actorFormId = kPlayerFormId,
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
    const auto targetArea = areaName(m_selectedArea);
    if (!equalsFoldedASCII(m_catalog.filter().area, targetArea)) {
        m_catalog.setArea(std::string(targetArea));
    }
    m_screen = SlotWorkflowScreen::picker;
}

void NativeSlotWorkflowModel::backToSlots() {
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

bool NativeSlotWorkflowModel::confirmApply() {
    if (m_screen != SlotWorkflowScreen::preview || !m_targetSlot || !m_previewTattoo ||
        !m_previewAppearance ||
        !equalsFoldedASCII(m_previewTattoo->area, areaName(m_selectedArea))) {
        return false;
    }

    const std::uint64_t generation = nextGeneration();
    m_pendingApply = SlotApplyTicket{
        .generation = generation,
        .request = core::ApplyTattooRequest{
            .actorFormId = kPlayerFormId,
            .area = m_selectedArea,
            .slot = *m_targetSlot,
            .domain = m_previewTattoo->domain,
            .section = m_previewTattoo->section,
            .name = m_previewTattoo->name,
            .color = m_previewAppearance->color,
            .alpha = m_previewAppearance->alpha,
        },
    };
    m_activeApplyGeneration = generation;
    m_error.reset();
    m_screen = SlotWorkflowScreen::applying;
    return true;
}

bool NativeSlotWorkflowModel::beginEditAppearance() {
    if (m_screen != SlotWorkflowScreen::slotActions || !m_targetSlot ||
        m_pendingLock || m_activeLockGeneration) {
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
        .area = m_selectedArea,
        .slot = found->index,
        .runtimeHandle = found->tattoo->runtimeHandle,
        .texturePath = found->tattoo->texturePath,
        .glowTexture = found->tattoo->glowTexture,
        .bump = found->tattoo->bump,
        .original = appearance,
        .edited = appearance,
    };
    m_error.reset();
    m_screen = SlotWorkflowScreen::editAppearance;
    return true;
}

bool NativeSlotWorkflowModel::toggleSelectedSlotLock() {
    if (m_screen != SlotWorkflowScreen::slotActions || !m_targetSlot ||
        m_pendingLock || m_activeLockGeneration) {
        return false;
    }
    const auto* current = slots();
    if (!current) {
        return false;
    }
    const auto found = std::ranges::find_if(current->slots, [this](const core::TattooSlot& slot) {
        return slot.index == *m_targetSlot;
    });
    if (found == current->slots.end() || found->occupancy != core::SlotOccupancy::slaveTats ||
        !found->tattoo || found->tattoo->runtimeHandle == 0) {
        return false;
    }
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
    m_error.reset();
    return true;
}

void NativeSlotWorkflowModel::setEditedAppearance(std::int32_t color, float alpha) noexcept {
    if (m_screen != SlotWorkflowScreen::editAppearance || !m_editAppearance ||
        m_editAppearance->mode == core::UpdateTattooAppearanceMode::synchronizeOnly) {
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
        m_editAppearance->mode == core::UpdateTattooAppearanceMode::synchronizeOnly) {
        return;
    }

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
}

void NativeSlotWorkflowModel::cancelEditAppearance() {
    if (m_screen != SlotWorkflowScreen::editAppearance) {
        return;
    }

    m_editAppearance.reset();
    m_error.reset();
    m_screen = SlotWorkflowScreen::slotActions;
}

bool NativeSlotWorkflowModel::confirmAppearanceUpdate() {
    if (!canSaveAppearance() || !m_editAppearance) {
        return false;
    }

    const std::uint64_t generation = nextGeneration();
    m_pendingAppearance = SlotAppearanceTicket{
        .generation = generation,
        .request = core::UpdateTattooAppearanceRequest{
            .actorFormId = m_editAppearance->actorFormId,
            .runtimeHandle = m_editAppearance->runtimeHandle,
            .color = m_editAppearance->edited.color,
            .alpha = m_editAppearance->edited.alpha,
            .glow = m_editAppearance->edited.glow,
            .glossiness = m_editAppearance->edited.glossiness,
            .specularStrength = m_editAppearance->edited.specularStrength,
            .emissiveMult = m_editAppearance->edited.emissiveMult,
            .mode = m_editAppearance->mode,
        },
    };
    m_activeAppearanceGeneration = generation;
    m_error.reset();
    m_screen = SlotWorkflowScreen::savingAppearance;
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
    return ticket;
}

std::optional<SlotRemoveTicket> NativeSlotWorkflowModel::takeRemoveRequest() {
    auto ticket = std::move(m_pendingRemove);
    m_pendingRemove.reset();
    return ticket;
}

std::optional<SlotAppearanceTicket> NativeSlotWorkflowModel::takeAppearanceRequest() {
    auto ticket = std::move(m_pendingAppearance);
    m_pendingAppearance.reset();
    return ticket;
}

std::optional<SlotLockTicket> NativeSlotWorkflowModel::takeLockRequest() {
    auto ticket = std::move(m_pendingLock);
    m_pendingLock.reset();
    return ticket;
}

void NativeSlotWorkflowModel::completeSlotQuery(
    std::uint64_t generation,
    core::TattooSlotsResult result) {
    if (!m_activeSlotQueryGeneration || generation != *m_activeSlotQueryGeneration) {
        return;
    }

    m_activeSlotQueryGeneration.reset();
    if (!result) {
        m_error = std::move(result.error());
        return;
    }

    auto completed = std::move(result.value());
    m_areaStates[areaIndex(completed.area)].slots = std::move(completed);
    m_error.reset();
    clampSelectedPage();
}

void NativeSlotWorkflowModel::completeApply(
    std::uint64_t generation,
    core::ApplyTattooResult result) {
    if (!m_activeApplyGeneration || generation != *m_activeApplyGeneration) {
        return;
    }

    m_activeApplyGeneration.reset();
    if (!result) {
        m_error = std::move(result.error());
        m_screen = SlotWorkflowScreen::preview;
        return;
    }

    m_error.reset();
    m_previewTattoo.reset();
    m_previewAppearance.reset();
    m_targetSlot.reset();
    m_screen = SlotWorkflowScreen::currentSlots;
    scheduleSlotQuery(m_selectedArea);
}

void NativeSlotWorkflowModel::completeRemove(
    std::uint64_t generation,
    core::RemoveTattooResult result) {
    if (!m_activeRemoveGeneration || generation != *m_activeRemoveGeneration) {
        return;
    }

    m_activeRemoveGeneration.reset();
    if (!result) {
        m_error = std::move(result.error());
        m_screen = SlotWorkflowScreen::removeConfirmation;
        return;
    }

    m_error.reset();
    m_targetSlot.reset();
    m_screen = SlotWorkflowScreen::currentSlots;
    scheduleSlotQuery(m_selectedArea);
}

void NativeSlotWorkflowModel::completeAppearanceUpdate(
    std::uint64_t generation,
    core::UpdateTattooAppearanceResult result) {
    if (!m_activeAppearanceGeneration || generation != *m_activeAppearanceGeneration) {
        return;
    }

    m_activeAppearanceGeneration.reset();
    if (!result) {
        m_error = std::move(result.error());
        if (m_editAppearance) {
            const bool mustSynchronizeOnly =
                m_editAppearance->mode == core::UpdateTattooAppearanceMode::synchronizeOnly ||
                m_error->code == core::ServiceErrorCode::synchronizeFailed;
            m_editAppearance->mode = mustSynchronizeOnly
                ? core::UpdateTattooAppearanceMode::synchronizeOnly
                : core::UpdateTattooAppearanceMode::updateAndSynchronize;
        }
        m_screen = SlotWorkflowScreen::editAppearance;
        return;
    }

    m_error.reset();
    m_editAppearance.reset();
    m_targetSlot.reset();
    m_screen = SlotWorkflowScreen::currentSlots;
    scheduleSlotQuery(m_selectedArea);
}

void NativeSlotWorkflowModel::completeLockStateChange(
    std::uint64_t generation,
    core::SetTattooLockedResult result) {
    if (!m_activeLockGeneration || generation != *m_activeLockGeneration) {
        return;
    }
    m_activeLockGeneration.reset();
    if (!result) {
        m_error = std::move(result.error());
        m_screen = SlotWorkflowScreen::slotActions;
        return;
    }
    m_error.reset();
    m_targetSlot.reset();
    m_previewTattoo.reset();
    m_previewAppearance.reset();
    m_screen = SlotWorkflowScreen::currentSlots;
    scheduleSlotQuery(m_selectedArea);
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
        m_editAppearance->edited != m_editAppearance->original;
}

bool NativeSlotWorkflowModel::isLockStateChangeInFlight() const noexcept {
    return m_pendingLock.has_value() || m_activeLockGeneration.has_value();
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
    const std::uint64_t generation = nextGeneration();
    m_pendingSlotQuery = SlotQueryTicket{
        .generation = generation,
        .actorFormId = kPlayerFormId,
        .area = area,
    };
    m_activeSlotQueryGeneration = generation;
    m_error.reset();
}

void NativeSlotWorkflowModel::clampSelectedPage() noexcept {
    auto& state = selectedState();
    const std::size_t pageCount = slotPageCount();
    state.pageIndex = pageCount == 0 ? 0 : std::min(state.pageIndex, pageCount - 1);
}

}  // namespace stui::native
