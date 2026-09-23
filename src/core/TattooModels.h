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
    std::int32_t color{0xFFFFFF};
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

struct ApplyTattooRequest {
    std::uint32_t actorFormId{};
    TattooArea area{TattooArea::body};
    std::int32_t slot{-1};
    std::string domain{"default"};
    std::string section;
    std::string name;
    std::int32_t color{0xFFFFFF};
    float alpha{1.0F};
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
    std::int32_t color{0xFFFFFF};
    float alpha{1.0F};
    std::int32_t glow{0};
    float glossiness{0.0F};
    float specularStrength{0.0F};
    float emissiveMult{1.0F};
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

}  // namespace stui::core
