#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace stui::core {

struct TattooEntry {
    std::int32_t runtimeHandle{0};
    std::string sourceId;
    std::string sourceFile;
    std::string packName;
    std::string domain{"default"};
    std::string section;
    std::string name;
    std::string texturePath;
    std::string area;
    std::int32_t slot{-1};
    std::int32_t color{0x000000};
    bool locked{false};
    float alpha{1.0F};
    std::int32_t glow{0};
    float glossiness{0.0F};
    float specularStrength{0.0F};
    std::string bump;
    std::string glowTexture;
    float emissiveMult{1.0F};

    bool operator==(const TattooEntry&) const = default;
};

enum class TattooArea {
    body,
    face,
    hands,
    feet,
};

enum class SlotOccupancy {
    empty,
    slaveTats,
    external,
};

struct TattooSlot {
    std::int32_t index{-1};
    SlotOccupancy occupancy{SlotOccupancy::empty};
    std::optional<TattooEntry> tattoo;
};

struct TattooSlots {
    std::uint32_t actorFormId{};
    TattooArea area{TattooArea::body};
    std::int32_t configuredCount{};
    std::vector<TattooSlot> slots;
};

enum class ApplyTattooMode {
    applyAndSynchronize,
    synchronizeOnly,
};

struct ApplyTattooAppearanceOverrides {
    std::optional<std::int32_t> glow;
    std::optional<float> glossiness;
    std::optional<float> specularStrength;
    std::optional<float> emissiveMult;
    std::optional<std::string> glowTexture;
    std::optional<std::string> bump;

    bool operator==(const ApplyTattooAppearanceOverrides&) const = default;
};

struct ApplyTattooRequest {
    std::uint32_t actorFormId{};
    TattooArea area{TattooArea::body};
    std::int32_t slot{-1};
    std::string domain{"default"};
    std::string section;
    std::string name;
    std::int32_t color{0x000000};
    float alpha{1.0F};
    ApplyTattooMode mode{ApplyTattooMode::applyAndSynchronize};
    std::optional<ApplyTattooAppearanceOverrides> appearance;

    bool operator==(const ApplyTattooRequest&) const = default;
};

struct ApplyTattooSuccess {
    std::uint32_t actorFormId{};
    TattooArea area{TattooArea::body};
    std::int32_t slot{-1};
    std::string section;
    std::string name;
};

enum class RemoveTattooMode {
    removeAndSynchronize,
    synchronizeOnly,
};

struct RemoveTattooRequest {
    std::uint32_t actorFormId{};
    TattooArea area{TattooArea::body};
    std::int32_t slot{-1};
    RemoveTattooMode mode{RemoveTattooMode::removeAndSynchronize};
};

struct RemoveTattooSuccess {
    std::uint32_t actorFormId{};
    TattooArea area{TattooArea::body};
    std::int32_t slot{-1};
};

enum class UpdateTattooAppearanceMode {
    updateAndSynchronize,
    synchronizeOnly,
};

struct UpdateTattooAppearanceRequest {
    std::uint32_t actorFormId{};
    std::int32_t runtimeHandle{};
    std::int32_t color{0x000000};
    float alpha{1.0F};
    std::int32_t glow{0};
    float glossiness{0.0F};
    float specularStrength{0.0F};
    float emissiveMult{1.0F};
    std::string glowTexture;
    std::string bump;
    UpdateTattooAppearanceMode mode{
        UpdateTattooAppearanceMode::updateAndSynchronize};
};

struct UpdateTattooAppearanceSuccess {
    std::uint32_t actorFormId{};
    std::int32_t runtimeHandle{};
};

struct SetTattooLockedRequest {
    std::uint32_t actorFormId{};
    std::int32_t runtimeHandle{};
    bool locked{};
};

struct SetTattooLockedSuccess {
    std::uint32_t actorFormId{};
    std::int32_t runtimeHandle{};
    bool locked{};
};

using CatalogPreviewSessionId = std::uint64_t;

struct CatalogPreviewContext {
    std::uint32_t actorFormId{};
    std::uint64_t generation{};
    TattooArea area{TattooArea::body};
    std::int32_t slot{-1};

    bool operator==(const CatalogPreviewContext&) const = default;
};

using CatalogPreviewBeginRequest = CatalogPreviewContext;

struct CatalogPreviewBeginSuccess {
    CatalogPreviewSessionId sessionId{};
};

struct CatalogPreviewPaintRequest {
    CatalogPreviewSessionId sessionId{};
    CatalogPreviewContext context;
    // Empty candidate clears the hover without ending the browser session.
    std::optional<TattooEntry> candidate;

    bool operator==(const CatalogPreviewPaintRequest&) const = default;
};

struct CatalogPreviewPaintSuccess {};

struct CatalogPreviewRestoreRequest {
    CatalogPreviewSessionId sessionId{};
    CatalogPreviewContext context;

    bool operator==(const CatalogPreviewRestoreRequest&) const = default;
};

struct CatalogPreviewRestoreSuccess {};

struct CatalogPreviewCommitRequest {
    CatalogPreviewSessionId sessionId{};
    CatalogPreviewContext context;
    ApplyTattooRequest apply;
};

enum class ServiceErrorCode {
    slaveTatsUnavailable,
    jContainersUnavailable,
    queryAvailableFailed,
    actorNotFound,
    invalidArea,
    invalidSlot,
    externalSlot,
    slotQueryFailed,
    tattooNotFound,
    applyFailed,
    removeFailed,
    synchronizeFailed,
    staleTattooHandle,
    updateFailed,
    lockFailed,
    previewUnavailable,
    previewFailed,
    previewConflict,
};

enum class MutationSideEffect {
    none,
    mayHaveOccurred,
};

struct ServiceError {
    ServiceErrorCode code;
    std::string message;
    MutationSideEffect mutationSideEffect{MutationSideEffect::none};
};

using TattooQueryResult = std::expected<std::vector<TattooEntry>, ServiceError>;
using TattooSlotsResult = std::expected<TattooSlots, ServiceError>;
using ApplyTattooResult = std::expected<ApplyTattooSuccess, ServiceError>;
using RemoveTattooResult = std::expected<RemoveTattooSuccess, ServiceError>;
using UpdateTattooAppearanceResult =
    std::expected<UpdateTattooAppearanceSuccess, ServiceError>;
using SetTattooLockedResult = std::expected<SetTattooLockedSuccess, ServiceError>;
using CatalogPreviewBeginResult = std::expected<CatalogPreviewBeginSuccess, ServiceError>;
using CatalogPreviewPaintResult = std::expected<CatalogPreviewPaintSuccess, ServiceError>;
using CatalogPreviewRestoreResult = std::expected<CatalogPreviewRestoreSuccess, ServiceError>;
using CatalogPreviewCommitResult = ApplyTattooResult;

}  // namespace stui::core
