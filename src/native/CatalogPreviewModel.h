#pragma once
#include "core/TattooModels.h"
#include "repository/TattooSourceParser.h"
#include <cstdint>
#include <optional>
namespace stui::native {
struct CatalogPreviewContext {
    std::uint32_t actorFormId{};
    std::uint64_t generation{};
    core::TattooArea area{core::TattooArea::body};
    std::int32_t slot{-1};
    bool operator==(const CatalogPreviewContext&) const = default;
};
struct CatalogPreviewPaint {
    CatalogPreviewContext context;
    std::uint64_t operation{},revision{};
    // No candidate means restore the exact runtime-owned original snapshot.
    std::optional<core::TattooEntry> candidate;
};
struct CatalogPreviewCommit {
    CatalogPreviewContext context;
    std::uint64_t operation{};
    core::TattooEntry candidate;
    bool synchronizeOnly{};
};
[[nodiscard]] core::TattooEntry catalogPreviewCandidate(
    const repository::TattooDefinition& definition);
[[nodiscard]] core::CatalogPreviewCommitRequest catalogPreviewCommitRequest(
    const CatalogPreviewCommit& commit, core::CatalogPreviewSessionId sessionId);
class CatalogPreviewModel {
public:
    bool begin(CatalogPreviewContext context,bool capable);
    void hover(std::optional<core::TattooEntry> candidate);
    bool pin();
    void setInk(std::int32_t color);
    void setGlow(std::int32_t color);
    void setGlossy(bool enabled);
    void undo();
    void close();
    void retry();
    std::optional<CatalogPreviewPaint> takePaint();
    void completePaint(const CatalogPreviewPaint& paint,bool success);
    bool requestSave();
    std::optional<CatalogPreviewCommit> takeCommit();
    void completeCommit(const CatalogPreviewCommit& commit,bool success,bool mutationWritten);
    [[nodiscard]] std::optional<core::TattooEntry> saveCandidate() const;
    [[nodiscard]] bool restorePending() const noexcept;
    [[nodiscard]] bool open() const noexcept {return m_open;}
    [[nodiscard]] bool closing() const noexcept {return m_closing;}
    [[nodiscard]] std::optional<core::TattooEntry> pinnedCandidate() const;
    [[nodiscard]] std::optional<core::TattooEntry> displayCandidate() const;
    [[nodiscard]] bool failed() const noexcept {return m_failed || m_commitFailed;}
private:
    core::TattooEntry effective(core::TattooEntry value) const;
    void updateDesired(bool force=false);
    CatalogPreviewContext m_context;
    bool m_open{},m_closing{},m_dirty{},m_pending{},m_failed{};
    bool m_written{},m_commitFailed{};
    std::uint64_t m_revision{},m_operation{},m_acknowledged{};
    std::optional<CatalogPreviewPaint> m_inFlight;
    std::optional<CatalogPreviewCommit> m_commitInFlight;
    std::optional<core::TattooEntry> m_saveIntent;
    std::optional<core::TattooEntry> m_hover,m_pin,m_desired;
    std::optional<std::int32_t> m_ink,m_glow;
    std::optional<bool> m_glossy;
};
} // namespace stui::native
