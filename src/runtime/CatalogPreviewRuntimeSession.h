#pragma once

#include "core/TattooModels.h"
#include "runtime/CatalogPreviewVisualSession.h"

#include <functional>
#include <memory>
#include <optional>

namespace stui::runtime {

using CatalogPreviewSlotQuery = std::function<core::TattooSlotsResult(
    std::uint32_t, core::TattooArea)>;
using CatalogPreviewApply = std::function<core::ApplyTattooResult(
    const core::ApplyTattooRequest&)>;

class CatalogPreviewRuntimeSession {
public:
    CatalogPreviewRuntimeSession(
        CatalogPreviewSlotQuery query,
        CatalogPreviewVisualBindings visualBindings,
        CatalogPreviewApply apply = {});

    core::CatalogPreviewBeginResult begin(const core::CatalogPreviewBeginRequest& request);
    core::CatalogPreviewPaintResult paint(const core::CatalogPreviewPaintRequest& request);
    core::CatalogPreviewRestoreResult restore(const core::CatalogPreviewRestoreRequest& request);
    core::CatalogPreviewCommitResult commit(const core::CatalogPreviewCommitRequest& request);
    void invalidate() noexcept;

private:
    struct SlotFingerprint {
        core::SlotOccupancy occupancy{core::SlotOccupancy::empty};
        std::optional<core::TattooEntry> tattoo;

        bool operator==(const SlotFingerprint&) const = default;
    };

    [[nodiscard]] std::expected<SlotFingerprint, core::ServiceError> validateSlot(
        const core::CatalogPreviewContext& context) const;
    [[nodiscard]] bool matchesActive(
        core::CatalogPreviewSessionId sessionId,
        const core::CatalogPreviewContext& context) const noexcept;
    [[nodiscard]] static CatalogPreviewVisualContext visualContext(
        const core::CatalogPreviewContext& context) noexcept;

    CatalogPreviewSlotQuery m_query;
    CatalogPreviewVisualBindings m_visualBindings;
    CatalogPreviewApply m_apply;
    std::unique_ptr<CatalogPreviewVisualSession> m_visual;
    std::optional<core::CatalogPreviewContext> m_context;
    std::optional<SlotFingerprint> m_originalSlot;
    core::CatalogPreviewSessionId m_sessionId{};
    core::CatalogPreviewSessionId m_nextSessionId{};
    std::optional<core::CatalogPreviewCommitRequest> m_synchronizationRetry;
};

}  // namespace stui::runtime
