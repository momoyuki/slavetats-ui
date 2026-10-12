#include "runtime/SlaveTatsRuntime.h"

#include "jcontainers_mini.h"
#include "runtime/SlaveTatsAlpha.h"
#include "runtime/UpdateTattooAppearanceOrchestration.h"
#include "SKSE/SKSE.h"

#include <expected>
#include <array>
#include <limits>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace stui::runtime {
namespace {

constexpr const char* kQueryAvailablePool = "SlaveTatsUI-queryAvailable";
constexpr const char* kQuerySlotsExternalPool = "SlaveTatsUI-querySlotsExternal";
constexpr const char* kApplyExternalPool = "SlaveTatsUI-applyExternal";
constexpr const char* kApplyAvailablePool = "SlaveTatsUI-applyAvailable";
constexpr const char* kRemoveExternalPool = "SlaveTatsUI-removeExternal";
constexpr const char* kUpdateAppearancePool = "SlaveTatsUI-updateAppearance";
constexpr const char* kSetTattooLockedPool = "SlaveTatsUI-setTattooLocked";

class JContainerPoolGuard {
public:
    explicit JContainerPoolGuard(const char* pool) noexcept : m_pool(pool) {}
    ~JContainerPoolGuard() { jcmini::JValue::cleanPool(m_pool); }

    JContainerPoolGuard(const JContainerPoolGuard&) = delete;
    JContainerPoolGuard& operator=(const JContainerPoolGuard&) = delete;

private:
    const char* m_pool;
};

const char* areaName(core::TattooArea area) noexcept {
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

    return nullptr;
}

bool sameRollbackTattoo(
    const core::TattooEntry& current,
    const core::TattooEntry& expected) {
    return current.domain == expected.domain && current.section == expected.section &&
        current.name == expected.name && current.texturePath == expected.texturePath &&
        current.color == expected.color && current.locked == expected.locked &&
        current.alpha == expected.alpha && current.glow == expected.glow &&
        current.glossiness == expected.glossiness &&
        current.specularStrength == expected.specularStrength &&
        current.bump == expected.bump && current.glowTexture == expected.glowTexture &&
        current.emissiveMult == expected.emissiveMult;
}

std::expected<std::unordered_set<int>, core::ServiceError> queryExternalSlots(
    const slavetats::interface::Addresses& api,
    RE::Actor* actor,
    const char* area,
    const char* pool) {
    const int matches = jcmini::JValue::addToPool(jcmini::JArray::object(), pool);
    const JContainerPoolGuard poolGuard(pool);
    if (api.external_slots(actor, RE::BSFixedString(area), matches)) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::slotQueryFailed,
            "external_slots failed",
        });
    }

    std::unordered_set<int> slots;
    const int count = jcmini::JArray::count(matches);
    for (int index = 0; index < count; ++index) {
        slots.insert(jcmini::JArray::getInt(matches, index));
    }
    return slots;
}

class TattooTemplateAppearanceGuard {
public:
    TattooTemplateAppearanceGuard(int tattoo, int color, float alpha) :
        m_tattoo(tattoo),
        m_color(jcmini::JMap::getInt(tattoo, "color", 0)),
        m_invertedAlpha(jcmini::JMap::getFlt(tattoo, "invertedAlpha", 0.0F)) {
        jcmini::JMap::setInt(m_tattoo, "color", color);
        jcmini::JMap::setFlt(
            m_tattoo,
            "invertedAlpha",
            toSlaveTatsInvertedAlpha(alpha));
    }

    ~TattooTemplateAppearanceGuard() {
        jcmini::JMap::setInt(m_tattoo, "color", m_color);
        jcmini::JMap::setFlt(m_tattoo, "invertedAlpha", m_invertedAlpha);
    }

    TattooTemplateAppearanceGuard(const TattooTemplateAppearanceGuard&) = delete;
    TattooTemplateAppearanceGuard& operator=(const TattooTemplateAppearanceGuard&) = delete;

private:
    int m_tattoo;
    int m_color;
    float m_invertedAlpha;
};

class SlaveTatsAppearanceBackend final : public IUpdateTattooAppearanceBackend {
public:
    SlaveTatsAppearanceBackend(
        const slavetats::interface::Addresses* api,
        const SlaveTatsAppearanceBindings* bindings,
        std::function<void*(std::uint32_t)> resolveLoadedActor) noexcept :
        m_api(api), m_bindings(bindings),
        m_resolveLoadedActor(std::move(resolveLoadedActor)) {}

    ActorHandle resolveActor(std::uint32_t actorFormId) override {
        return m_resolveLoadedActor(actorFormId);
    }

    std::expected<std::vector<std::int32_t>, core::ServiceError>
    queryAppliedTattooHandles(ActorHandle actorHandle) override {
        if (m_bindings) {
            if (!m_bindings->queryAppliedTattooHandles) {
                return std::unexpected(core::ServiceError{
                    core::ServiceErrorCode::updateFailed,
                    "Applied tattoo query binding is unavailable",
                });
            }
            return m_bindings->queryAppliedTattooHandles(actorHandle);
        }
        auto* actor = static_cast<RE::Actor*>(actorHandle);
        const int matches = jcmini::JValue::addToPool(
            jcmini::JArray::object(),
            kUpdateAppearancePool);
        const JContainerPoolGuard poolGuard(kUpdateAppearancePool);
        if (m_api->query_applied_tattoos(
                actor,
                0,
                matches,
                RE::BSFixedString(""),
                -1)) {
            return std::unexpected(core::ServiceError{
                core::ServiceErrorCode::updateFailed,
                "query_applied_tattoos failed",
            });
        }

        std::vector<std::int32_t> appliedHandles;
        const int count = jcmini::JArray::count(matches);
        appliedHandles.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            appliedHandles.push_back(jcmini::JArray::getObj(matches, index));
        }
        return appliedHandles;
    }

    bool writeAppearance(
        std::int32_t runtimeHandle,
        const core::UpdateTattooAppearanceRequest& request) override {
        const float invertedAlpha = toSlaveTatsInvertedAlpha(request.alpha);
        if (m_bindings) {
            if (!m_bindings->setTattooInt || !m_bindings->getTattooInt ||
                !m_bindings->setTattooFloat || !m_bindings->getTattooFloat ||
                !m_bindings->setTattooString || !m_bindings->getTattooString ||
                runtimeHandle == 0) {
                return false;
            }
            constexpr auto missingInt = std::numeric_limits<std::int32_t>::min();
            const auto setIntAndVerify = [&](const char* key, std::int32_t value) {
                m_bindings->setTattooInt(runtimeHandle, key, value);
                return m_bindings->getTattooInt(runtimeHandle, key, missingInt) == value;
            };
            const float missingFloat = std::numeric_limits<float>::quiet_NaN();
            const auto setFloatAndVerify = [&](const char* key, float value) {
                m_bindings->setTattooFloat(runtimeHandle, key, value);
                return m_bindings->getTattooFloat(runtimeHandle, key, missingFloat) == value;
            };
            const auto setStringAndVerify = [&](const char* key, const std::string& value) {
                m_bindings->setTattooString(runtimeHandle, key, value.c_str());
                return m_bindings->getTattooString(
                    runtimeHandle, key, value.empty() ? "\x01" : "") == value;
            };

            if (!setIntAndVerify("color", request.color)) {
                return false;
            }
            if (!setFloatAndVerify("invertedAlpha", invertedAlpha)) {
                return false;
            }
            if (!setIntAndVerify("glow", request.glow)) {
                return false;
            }
            if (!setFloatAndVerify("glossiness", request.glossiness)) {
                return false;
            }
            if (!setFloatAndVerify("specularStrength", request.specularStrength)) {
                return false;
            }
            if (!setFloatAndVerify("emissiveMult", request.emissiveMult)) {
                return false;
            }
            if (!setStringAndVerify("glowTexture", request.glowTexture)) {
                return false;
            }
            return setStringAndVerify("bump", request.bump);
        }
        if (!jcmini::JMap::setIntAndVerify(runtimeHandle, "color", request.color)) {
            return false;
        }
        if (!jcmini::JMap::setFltAndVerify(runtimeHandle, "invertedAlpha", invertedAlpha)) {
            return false;
        }
        if (!jcmini::JMap::setIntAndVerify(runtimeHandle, "glow", request.glow)) {
            return false;
        }
        if (!jcmini::JMap::setFltAndVerify(runtimeHandle, "glossiness", request.glossiness)) {
            return false;
        }
        if (!jcmini::JMap::setFltAndVerify(
                runtimeHandle,
                "specularStrength",
                request.specularStrength)) {
            return false;
        }
        if (!jcmini::JMap::setFltAndVerify(
                runtimeHandle,
                "emissiveMult",
                request.emissiveMult)) {
            return false;
        }
        if (!jcmini::fn_jmap_setStr || !jcmini::fn_jmap_getStr) {
            return false;
        }
        jcmini::JMap::setStr(runtimeHandle, "glowTexture", request.glowTexture.c_str());
        if (jcmini::JMap::getStr(runtimeHandle, "glowTexture",
                request.glowTexture.empty() ? "\x01" : "") != request.glowTexture) {
            return false;
        }
        jcmini::JMap::setStr(runtimeHandle, "bump", request.bump.c_str());
        return jcmini::JMap::getStr(runtimeHandle, "bump",
            request.bump.empty() ? "\x01" : "") == request.bump;
    }

    bool markActorUpdated(ActorHandle actorHandle) override {
        if (m_bindings) {
            if (!m_bindings->setActorInt || !m_bindings->getActorInt || !actorHandle) {
                return false;
            }
            m_bindings->setActorInt(actorHandle, ".SlaveTats.updated", 1);
            constexpr auto missing = std::numeric_limits<std::int32_t>::min();
            return m_bindings->getActorInt(
                actorHandle, ".SlaveTats.updated", missing) == 1;
        }
        return jcmini::JFormDB::setIntAndVerify(
            static_cast<RE::Actor*>(actorHandle), ".SlaveTats.updated", 1);
    }

    bool synchronize(ActorHandle actorHandle) override {
        if (m_bindings) {
            return m_bindings->synchronizeTattoos &&
                !m_bindings->synchronizeTattoos(actorHandle, false);
        }
        return !m_api->synchronize_tattoos(static_cast<RE::Actor*>(actorHandle), false);
    }

private:
    const slavetats::interface::Addresses* m_api;
    const SlaveTatsAppearanceBindings* m_bindings;
    std::function<void*(std::uint32_t)> m_resolveLoadedActor;
};

}  // namespace

void SlaveTatsRuntime::bindSlaveTats(const slavetats::interface::Addresses* api) noexcept {
    m_api = api;
    m_apiVersion = api ? api->current_version : 0;
}

void SlaveTatsRuntime::noteSlaveTatsVersionMismatch(std::uint32_t version) noexcept {
    m_api = nullptr;
    m_apiVersion = version;
}

bool SlaveTatsRuntime::bindJContainers(const jc::root_interface* root) {
    m_jContainersReady = root && jcmini::Init(root);
    return m_jContainersReady;
}

bool SlaveTatsRuntime::apiAvailable() const noexcept {
    return m_api != nullptr;
}

bool SlaveTatsRuntime::jContainersReady() const noexcept {
    return m_jContainersReady;
}

std::uint32_t SlaveTatsRuntime::apiVersion() const noexcept {
    return m_apiVersion;
}

const slavetats::interface::Addresses* SlaveTatsRuntime::api() const noexcept {
    return m_api;
}

void* SlaveTatsRuntime::resolveLoadedActor(std::uint32_t actorFormId) const {
    if (actorFormId == 0) {
        return nullptr;
    }
    if (m_appearanceBindings) {
        if (!m_appearanceBindings->resolveActor || !m_appearanceBindings->isActor3DLoaded) {
            return nullptr;
        }
        auto* actor = m_appearanceBindings->resolveActor(actorFormId);
        return actor && m_appearanceBindings->isActor3DLoaded(actor) ? actor : nullptr;
    }
    auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorFormId);
    return actor && actor->Is3DLoaded() ? actor : nullptr;
}

core::TattooQueryResult SlaveTatsRuntime::queryAvailable(std::string_view domain) {
    const int matches = jcmini::JValue::addToPool(jcmini::JArray::object(), kQueryAvailablePool);
    const JContainerPoolGuard poolGuard(kQueryAvailablePool);
    const std::string domainString(domain);

    if (m_api->query_available_tattoos(
            0, matches, 0, RE::BSFixedString(domainString.c_str()))) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::queryAvailableFailed,
            "query_available_tattoos failed",
        });
    }

    std::vector<core::TattooEntry> entries;
    const int count = jcmini::JArray::count(matches);
    entries.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index) {
        const int handle = jcmini::JArray::getObj(matches, index);
        const int rawColor = jcmini::JMap::getInt(handle, "color", 0);
        entries.push_back(core::TattooEntry{
            .runtimeHandle = handle,
            .domain = domainString,
            .section = jcmini::JMap::getStr(handle, "section"),
            .name = jcmini::JMap::getStr(handle, "name"),
            .texturePath = jcmini::JMap::getStr(handle, "texture"),
            .area = jcmini::JMap::getStr(handle, "area"),
            .slot = jcmini::JMap::getInt(handle, "slot"),
            .color = rawColor == 0 ? 0xFFFFFF : rawColor,
            .locked = jcmini::JMap::getInt(handle, "locked") != 0,
            .alpha = fromSlaveTatsInvertedAlpha(
                jcmini::JMap::getFlt(handle, "invertedAlpha", 0.0F)),
            .glow = jcmini::JMap::getInt(handle, "glow", 0),
            .glossiness = jcmini::JMap::getFlt(handle, "glossiness", 0.0F),
            .specularStrength = jcmini::JMap::getFlt(
                handle, "specularStrength", 0.0F),
            .bump = jcmini::JMap::getStr(handle, "bump"),
            .glowTexture = jcmini::JMap::getStr(handle, "glowTexture"),
            .emissiveMult = jcmini::JMap::getFlt(handle, "emissiveMult", 1.0F),
        });
    }

    return entries;
}

core::TattooSlotsResult SlaveTatsRuntime::querySlots(
    std::uint32_t actorFormId,
    core::TattooArea area) {
    auto* actor = resolveLoadedActor(actorFormId);
    if (!actor) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::actorNotFound,
            "Actor not found",
        });
    }
    return querySlotsForActor(actorFormId, area, actor);
}

core::TattooSlotsResult SlaveTatsRuntime::querySlotsForActor(
    const std::uint32_t actorFormId,
    const core::TattooArea area,
    void* actorHandle) {
    auto* actor = static_cast<RE::Actor*>(actorHandle);

    const char* areaString = areaName(area);
    if (!areaString) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::invalidArea,
            "Invalid tattoo area",
        });
    }

    const auto externalSlots = queryExternalSlots(
        *m_api,
        actor,
        areaString,
        kQuerySlotsExternalPool);
    if (!externalSlots) {
        return std::unexpected(externalSlots.error());
    }

    const int configuredCount = m_slotConfiguration.count(area);
    core::TattooSlots result{
        .actorFormId = actorFormId,
        .area = area,
        .configuredCount = configuredCount,
    };
    if (configuredCount > 0) {
        result.slots.reserve(static_cast<std::size_t>(configuredCount));
    }

    for (int slot = 0; slot < configuredCount; ++slot) {
        const int tattoo = m_api->get_applied_tattoo_in_slot(
            actor,
            RE::BSFixedString(areaString),
            slot);
        if (tattoo != 0) {
            const int rawColor = jcmini::JMap::getInt(tattoo, "color", 0);
            result.slots.push_back(core::TattooSlot{
                .index = slot,
                .occupancy = core::SlotOccupancy::slaveTats,
                .tattoo = core::TattooEntry{
                    .runtimeHandle = tattoo,
                    .domain = jcmini::JMap::getStr(tattoo, "domain", "default"),
                    .section = jcmini::JMap::getStr(tattoo, "section"),
                    .name = jcmini::JMap::getStr(tattoo, "name"),
                    .texturePath = jcmini::JMap::getStr(tattoo, "texture"),
                    .area = areaString,
                    .slot = slot,
                    .color = rawColor == 0 ? 0xFFFFFF : rawColor,
                    .locked = jcmini::JMap::getInt(tattoo, "locked") != 0,
                    .alpha = fromSlaveTatsInvertedAlpha(
                        jcmini::JMap::getFlt(tattoo, "invertedAlpha", 0.0F)),
                    .glow = jcmini::JMap::getInt(tattoo, "glow", 0),
                    .glossiness = jcmini::JMap::getFlt(tattoo, "glossiness", 0.0F),
                    .specularStrength = jcmini::JMap::getFlt(
                        tattoo, "specularStrength", 0.0F),
                    .bump = jcmini::JMap::getStr(tattoo, "bump"),
                    .glowTexture = jcmini::JMap::getStr(tattoo, "glowTexture"),
                    .emissiveMult = jcmini::JMap::getFlt(
                        tattoo, "emissiveMult", 1.0F),
                },
            });
        } else if (externalSlots->contains(slot)) {
            result.slots.push_back(core::TattooSlot{
                .index = slot,
                .occupancy = core::SlotOccupancy::external,
            });
        } else {
            result.slots.push_back(core::TattooSlot{
                .index = slot,
                .occupancy = core::SlotOccupancy::empty,
            });
        }
    }

    return result;
}

core::ApplyTattooResult SlaveTatsRuntime::applyToSlot(const core::ApplyTattooRequest& request) {
    auto* actor = static_cast<RE::Actor*>(resolveLoadedActor(request.actorFormId));
    if (!actor) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::actorNotFound,
            "Actor not found",
        });
    }

    const char* areaString = areaName(request.area);
    if (!areaString) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::invalidArea,
            "Invalid tattoo area",
        });
    }

    if (request.mode == core::ApplyTattooMode::applyAndSynchronize) {
        const auto externalSlots = queryExternalSlots(
            *m_api,
            actor,
            areaString,
            kApplyExternalPool);
        if (!externalSlots) {
            return std::unexpected(externalSlots.error());
        }
        if (externalSlots->contains(request.slot)) {
            return std::unexpected(core::ServiceError{
                core::ServiceErrorCode::externalSlot,
                "Slot is occupied by an external overlay",
            });
        }

        const int available = jcmini::JValue::addToPool(
            jcmini::JArray::object(),
            kApplyAvailablePool);
        const JContainerPoolGuard poolGuard(kApplyAvailablePool);
        if (m_api->query_available_tattoos(
                0,
                available,
                0,
                RE::BSFixedString(request.domain.c_str()))) {
            return std::unexpected(core::ServiceError{
                core::ServiceErrorCode::applyFailed,
                "query_available_tattoos failed",
            });
        }

        int tattooHandle = 0;
        const int count = jcmini::JArray::count(available);
        for (int index = 0; index < count; ++index) {
            const int candidate = jcmini::JArray::getObj(available, index);
            if (jcmini::JMap::getStr(candidate, "section") == request.section &&
                jcmini::JMap::getStr(candidate, "name") == request.name) {
                tattooHandle = candidate;
                break;
            }
        }

        if (tattooHandle == 0) {
            return std::unexpected(core::ServiceError{
                core::ServiceErrorCode::tattooNotFound,
                "Tattoo not found in available list",
            });
        }

        int applied = 0;
        {
            const TattooTemplateAppearanceGuard appearance(
                tattooHandle,
                request.color,
                request.alpha);
            applied = m_api->add_and_get_tattoo(
                actor,
                tattooHandle,
                request.slot,
                false,
                false,
                true);
        }

        if (applied == 0) {
            SKSE::log::warn(
                "SlaveTatsUI: SlaveTatsNG rejected tattoo apply (section={}, name={}, area={}, slot={})",
                request.section,
                request.name,
                areaString,
                request.slot);
            return std::unexpected(core::ServiceError{
                core::ServiceErrorCode::applyFailed,
                "Failed to apply tattoo to slot",
            });
        }
    }

    jcmini::JFormDB::setInt(actor, ".SlaveTats.updated", 1);
    if (m_api->synchronize_tattoos(actor, false)) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::synchronizeFailed,
            "Tattoo applied but synchronization failed",
            request.mode == core::ApplyTattooMode::applyAndSynchronize
                ? core::MutationSideEffect::mayHaveOccurred
                : core::MutationSideEffect::none,
        });
    }

    return core::ApplyTattooSuccess{
        .actorFormId = request.actorFormId,
        .area = request.area,
        .slot = request.slot,
        .section = request.section,
        .name = request.name,
    };
}

core::RemoveTattooResult SlaveTatsRuntime::removeFromSlot(
    const core::RemoveTattooRequest& request) {
    auto* actor = static_cast<RE::Actor*>(resolveLoadedActor(request.actorFormId));
    if (!actor) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::actorNotFound,
            "Actor not found",
        });
    }

    const char* areaString = areaName(request.area);
    if (!areaString) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::invalidArea,
            "Invalid tattoo area",
        });
    }

    if (request.mode == core::RemoveTattooMode::removeAndSynchronize) {
        const auto externalSlots = queryExternalSlots(
            *m_api,
            actor,
            areaString,
            kRemoveExternalPool);
        if (!externalSlots) {
            return std::unexpected(externalSlots.error());
        }
        if (externalSlots->contains(request.slot)) {
            return std::unexpected(core::ServiceError{
                core::ServiceErrorCode::externalSlot,
                "Slot is occupied by an external overlay",
            });
        }

        if (m_api->remove_tattoo_from_slot(
                actor,
                RE::BSFixedString(areaString),
                request.slot,
                false,
                false)) {
            return std::unexpected(core::ServiceError{
                core::ServiceErrorCode::removeFailed,
                "Failed to remove tattoo from slot",
            });
        }
    }

    jcmini::JFormDB::setInt(actor, ".SlaveTats.updated", 1);
    if (m_api->synchronize_tattoos(actor, false)) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::synchronizeFailed,
            "Tattoo removed but synchronization failed",
        });
    }

    return core::RemoveTattooSuccess{
        .actorFormId = request.actorFormId,
        .area = request.area,
        .slot = request.slot,
    };
}

core::UpdateTattooAppearanceResult SlaveTatsRuntime::updateAppearance(
    const core::UpdateTattooAppearanceRequest& request) {
    SlaveTatsAppearanceBackend backend(
        m_api,
        m_appearanceBindings ? &*m_appearanceBindings : nullptr,
        [this](std::uint32_t actorFormId) { return resolveLoadedActor(actorFormId); });
    return updateTattooAppearance(request, backend);
}

core::SetTattooLockedResult SlaveTatsRuntime::setTattooLocked(
    const core::SetTattooLockedRequest& request) {
    const auto lockFailed = [](const char* message) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::lockFailed,
            message,
        });
    };
    const auto staleHandle = [] {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::staleTattooHandle,
            "Tattoo handle is stale; refresh the slot snapshot and try again",
        });
    };

    auto* actorHandle = resolveLoadedActor(request.actorFormId);
    if (!actorHandle) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::actorNotFound,
            "Actor not found",
        });
    }

    std::vector<std::int32_t> appliedHandles;
    if (m_appearanceBindings) {
        if (!m_appearanceBindings->queryAppliedTattooHandles) {
            return lockFailed("Applied tattoo query binding is unavailable");
        }
        const auto queried = m_appearanceBindings->queryAppliedTattooHandles(actorHandle);
        if (!queried) {
            return std::unexpected(queried.error());
        }
        appliedHandles = *queried;
    } else {
        if (!m_api) {
            return std::unexpected(core::ServiceError{
                core::ServiceErrorCode::slaveTatsUnavailable,
                "SlaveTatsNG not available",
            });
        }
        const int matches = jcmini::JValue::addToPool(
            jcmini::JArray::object(),
            kSetTattooLockedPool);
        const JContainerPoolGuard poolGuard(kSetTattooLockedPool);
        if (m_api->query_applied_tattoos(
                static_cast<RE::Actor*>(actorHandle),
                0,
                matches,
                RE::BSFixedString(""),
                -1)) {
            return lockFailed("query_applied_tattoos failed");
        }
        const int count = jcmini::JArray::count(matches);
        appliedHandles.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            appliedHandles.push_back(jcmini::JArray::getObj(matches, index));
        }
    }

    if (request.runtimeHandle == 0 ||
        std::ranges::find(appliedHandles, request.runtimeHandle) == appliedHandles.end()) {
        return staleHandle();
    }

    const std::int32_t value = request.locked ? 1 : 0;
    bool wrote = false;
    if (m_appearanceBindings) {
        if (m_appearanceBindings->setTattooInt && m_appearanceBindings->getTattooInt) {
            constexpr auto missing = std::numeric_limits<std::int32_t>::min();
            m_appearanceBindings->setTattooInt(request.runtimeHandle, "locked", value);
            wrote = m_appearanceBindings->getTattooInt(
                        request.runtimeHandle, "locked", missing) == value;
        }
    } else {
        wrote = jcmini::JMap::setIntAndVerify(request.runtimeHandle, "locked", value);
    }
    if (!wrote) {
        return lockFailed("Failed to update tattoo lock state");
    }

    return core::SetTattooLockedSuccess{
        .actorFormId = request.actorFormId,
        .runtimeHandle = request.runtimeHandle,
        .locked = request.locked,
    };
}

core::CaptureTattooLoadoutResult SlaveTatsRuntime::captureLoadout(
    const core::CaptureTattooLoadoutRequest& request) {
    core::TattooLoadoutCaptureValue captured{.actorFormId = request.actorFormId};
    constexpr std::array areas{
        core::TattooArea::body,
        core::TattooArea::face,
        core::TattooArea::hands,
        core::TattooArea::feet,
    };
    auto* actor = resolveLoadedActor(request.actorFormId);
    if (!actor) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::actorNotFound, "Actor not found"});
    }

    for (const auto area : areas) {
        auto snapshot = querySlotsForActor(request.actorFormId, area, actor);
        if (!snapshot) {
            return std::unexpected(std::move(snapshot.error()));
        }
        for (const auto& slot : snapshot->slots) {
            if (slot.occupancy != core::SlotOccupancy::slaveTats || !slot.tattoo) {
                continue;
            }
            const auto& tattoo = *slot.tattoo;
            captured.entries.push_back(core::TattooLoadoutEntry{
                .area = area,
                .slot = slot.index,
                .domain = tattoo.domain,
                .section = tattoo.section,
                .name = tattoo.name,
                .color = tattoo.color,
                .alpha = tattoo.alpha,
                .glow = tattoo.glow,
                .emissiveMult = tattoo.emissiveMult,
                .glossiness = tattoo.glossiness,
                .specularStrength = tattoo.specularStrength,
            });
        }
    }
    return captured;
}

core::PreviewTattooLoadoutResult SlaveTatsRuntime::previewLoadout(
    const core::PreviewTattooLoadoutRequest& request) {
    auto plan = preflight(request);
    if (!plan) {
        return core::TattooLoadoutPreviewValue{
            .actorFormId = request.actorFormId,
            .blockers = std::move(plan.error()),
        };
    }
    return core::TattooLoadoutPreviewValue{
        .actorFormId = request.actorFormId,
        .plan = std::move(*plan),
    };
}

core::TattooLoadoutPlanResult SlaveTatsRuntime::preflight(
    const core::PreviewTattooLoadoutRequest& request) {
    constexpr std::array areas{
        core::TattooArea::body,
        core::TattooArea::face,
        core::TattooArea::hands,
        core::TattooArea::feet,
    };
    core::TattooLoadoutPlanningInput input{
        .actorFormId = request.actorFormId,
        .loadout = request.loadout,
    };
    auto* actor = resolveLoadedActor(request.actorFormId);
    if (!actor) {
        return std::unexpected(std::vector<core::TattooLoadoutBlocker>{{
            .code = core::TattooLoadoutBlockerCode::actorMismatch,
            .slot = -1,
            .message = "Actor not found",
        }});
    }
    for (std::size_t index = 0; index < areas.size(); ++index) {
        auto snapshot = querySlotsForActor(request.actorFormId, areas[index], actor);
        if (!snapshot) {
            return std::unexpected(std::vector<core::TattooLoadoutBlocker>{{
                .code = core::TattooLoadoutBlockerCode::actorMismatch,
                .area = areas[index],
                .slot = -1,
                .message = snapshot.error().message,
            }});
        }
        input.slots[index] = std::move(*snapshot);
    }

    std::set<std::string> domains;
    for (const auto& entry : request.loadout.entries) {
        domains.insert(entry.domain);
    }
    for (const auto& domain : domains) {
        auto available = queryAvailable(domain);
        if (!available) {
            return std::unexpected(std::vector<core::TattooLoadoutBlocker>{{
                .code = core::TattooLoadoutBlockerCode::missingTattoo,
                .message = available.error().message,
            }});
        }
        input.available.insert(
            input.available.end(),
            std::make_move_iterator(available->begin()),
            std::make_move_iterator(available->end()));
    }

    auto plan = core::planTattooLoadout(input);
    if (plan) m_loadoutActorFormId = request.actorFormId;
    return plan;
}

core::RestoreTattooLoadoutResult SlaveTatsRuntime::restoreLoadout(
    const core::RestoreTattooLoadoutRequest& request) {
    return executeTattooLoadout(request, *this, m_loadoutRetries);
}

core::RestoreTattooLoadoutResult SlaveTatsRuntime::retryLoadoutSynchronization(
    const core::RetryTattooLoadoutSynchronizationRequest& request) {
    return runtime::retryTattooLoadoutSynchronization(
        request, *this, m_loadoutRetries);
}

std::expected<core::TattooLoadoutRollbackSnapshot, core::ServiceError>
SlaveTatsRuntime::captureRollback(const core::TattooLoadoutPlan& plan) {
    core::TattooLoadoutRollbackSnapshot snapshot{.actorFormId = plan.actorFormId};
    constexpr std::array areas{
        core::TattooArea::body,
        core::TattooArea::face,
        core::TattooArea::hands,
        core::TattooArea::feet,
    };
    for (std::size_t index = 0; index < areas.size(); ++index) {
        auto slots = querySlots(plan.actorFormId, areas[index]);
        if (!slots) return std::unexpected(std::move(slots.error()));
        snapshot.areas[index] = std::move(*slots);
    }
    m_loadoutActorFormId = plan.actorFormId;
    return snapshot;
}

std::expected<void, core::ServiceError> SlaveTatsRuntime::applyAction(
    const core::TattooLoadoutPlanAction& action) {
    if (action.kind == core::TattooLoadoutActionKind::unchanged) return {};
    auto* actor = static_cast<RE::Actor*>(resolveLoadedActor(m_loadoutActorFormId));
    const char* area = areaName(action.area);
    if (!actor) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::actorNotFound, "Actor not found"});
    }
    if (!area) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::invalidArea, "Invalid tattoo area"});
    }

    if (action.kind == core::TattooLoadoutActionKind::remove ||
        action.kind == core::TattooLoadoutActionKind::replace) {
        if (m_api->remove_tattoo_from_slot(
                actor, RE::BSFixedString(area), action.slot, false, false)) {
            return std::unexpected(core::ServiceError{
                core::ServiceErrorCode::removeFailed,
                "Failed to remove tattoo during loadout restore",
                core::MutationSideEffect::mayHaveOccurred,
            });
        }
        if (action.kind == core::TattooLoadoutActionKind::remove) return {};
    }

    std::int32_t runtimeHandle = action.before ? action.before->runtimeHandle : 0;
    if (action.kind == core::TattooLoadoutActionKind::add ||
        action.kind == core::TattooLoadoutActionKind::replace) {
        const auto& desired = *action.after;
        const int available = jcmini::JValue::addToPool(
            jcmini::JArray::object(), kApplyAvailablePool);
        const JContainerPoolGuard poolGuard(kApplyAvailablePool);
        if (m_api->query_available_tattoos(
                0, available, 0, RE::BSFixedString(desired.domain.c_str()))) {
            return std::unexpected(core::ServiceError{
                core::ServiceErrorCode::applyFailed,
                "query_available_tattoos failed during loadout restore"});
        }
        int templateHandle = 0;
        for (int index = 0; index < jcmini::JArray::count(available); ++index) {
            const int candidate = jcmini::JArray::getObj(available, index);
            if (jcmini::JMap::getStr(candidate, "section") == desired.section &&
                jcmini::JMap::getStr(candidate, "name") == desired.name) {
                templateHandle = candidate;
                break;
            }
        }
        if (templateHandle == 0) {
            return std::unexpected(core::ServiceError{
                core::ServiceErrorCode::tattooNotFound,
                "Tattoo disappeared after loadout Preflight"});
        }
        const TattooTemplateAppearanceGuard appearance(
            templateHandle, desired.color, desired.alpha);
        runtimeHandle = m_api->add_and_get_tattoo(
            actor, templateHandle, action.slot, false, false, true);
        if (runtimeHandle == 0) {
            return std::unexpected(core::ServiceError{
                core::ServiceErrorCode::applyFailed,
                "Failed to add tattoo during loadout restore",
                core::MutationSideEffect::mayHaveOccurred,
            });
        }
    }

    if (!action.after) return {};
    SlaveTatsAppearanceBackend appearance(
        m_api,
        m_appearanceBindings ? &*m_appearanceBindings : nullptr,
        [this](std::uint32_t actorFormId) { return resolveLoadedActor(actorFormId); });
    auto handles = appearance.queryAppliedTattooHandles(actor);
    if (!handles || std::ranges::find(*handles, runtimeHandle) == handles->end()) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::staleTattooHandle,
            "Tattoo handle became stale during loadout restore",
            core::MutationSideEffect::mayHaveOccurred,
        });
    }
    const auto& desired = *action.after;
    if (!appearance.writeAppearance(runtimeHandle, {
            .actorFormId = m_loadoutActorFormId,
            .runtimeHandle = runtimeHandle,
            .color = desired.color,
            .alpha = desired.alpha,
            .glow = desired.glow,
            .glossiness = desired.glossiness,
            .specularStrength = desired.specularStrength,
            .emissiveMult = desired.emissiveMult,
        })) {
        return std::unexpected(core::ServiceError{
            core::ServiceErrorCode::updateFailed,
            "Failed to verify tattoo appearance during loadout restore",
            core::MutationSideEffect::mayHaveOccurred,
        });
    }
    return {};
}

bool SlaveTatsRuntime::synchronize(const std::uint32_t actorFormId) {
    auto* actor = resolveLoadedActor(actorFormId);
    if (!actor) return false;
    if (m_appearanceBindings) {
        if (!m_appearanceBindings->setActorInt || !m_appearanceBindings->getActorInt ||
            !m_appearanceBindings->synchronizeTattoos) {
            return false;
        }
        m_appearanceBindings->setActorInt(actor, ".SlaveTats.updated", 1);
        if (m_appearanceBindings->getActorInt(actor, ".SlaveTats.updated", 0) != 1) {
            return false;
        }
        return !m_appearanceBindings->synchronizeTattoos(actor, false);
    }
    jcmini::JFormDB::setInt(static_cast<RE::Actor*>(actor), ".SlaveTats.updated", 1);
    return !m_api->synchronize_tattoos(static_cast<RE::Actor*>(actor), false);
}

core::TattooLoadoutVerification SlaveTatsRuntime::verifyForward(
    const core::TattooLoadoutPlan& plan) {
    core::TattooLoadoutVerification result{.matches = true};
    for (const auto& action : plan.actions) {
        auto slots = querySlots(plan.actorFormId, action.area);
        if (!slots) {
            result.matches = false;
            result.affectedSlots.push_back({action.area, action.slot, slots.error().message});
            continue;
        }
        const auto found = std::ranges::find(slots->slots, action.slot, &core::TattooSlot::index);
        const bool expectsEmpty = !action.after;
        bool matches = found != slots->slots.end();
        if (matches && expectsEmpty) {
            matches = found->occupancy == core::SlotOccupancy::empty;
        } else if (matches) {
            const auto& desired = *action.after;
            matches = found->occupancy == core::SlotOccupancy::slaveTats && found->tattoo &&
                found->tattoo->domain == desired.domain &&
                found->tattoo->section == desired.section &&
                found->tattoo->name == desired.name &&
                found->tattoo->color == desired.color && found->tattoo->alpha == desired.alpha &&
                found->tattoo->glow == desired.glow &&
                found->tattoo->emissiveMult == desired.emissiveMult &&
                found->tattoo->glossiness == desired.glossiness &&
                found->tattoo->specularStrength == desired.specularStrength;
        }
        if (!matches) {
            result.matches = false;
            result.affectedSlots.push_back(
                {action.area, action.slot, "Forward tattoo state did not verify"});
        }
    }
    return result;
}

std::expected<void, core::ServiceError> SlaveTatsRuntime::applyRollback(
    const core::TattooLoadoutRollbackSnapshot& snapshot) {
    m_loadoutActorFormId = snapshot.actorFormId;
    for (const auto& area : snapshot.areas) {
        auto current = querySlots(snapshot.actorFormId, area.area);
        if (!current) return std::unexpected(std::move(current.error()));
        for (const auto& target : area.slots) {
            const auto found = std::ranges::find(
                current->slots, target.index, &core::TattooSlot::index);
            if (found != current->slots.end() &&
                found->occupancy == core::SlotOccupancy::slaveTats) {
                auto removed = applyAction({
                    .kind = core::TattooLoadoutActionKind::remove,
                    .area = area.area,
                    .slot = target.index,
                    .before = found->tattoo,
                });
                if (!removed) return removed;
            }
            if (target.occupancy == core::SlotOccupancy::slaveTats && target.tattoo) {
                const auto& tattoo = *target.tattoo;
                auto added = applyAction({
                    .kind = core::TattooLoadoutActionKind::add,
                    .area = area.area,
                    .slot = target.index,
                    .after = core::TattooLoadoutEntry{
                        .area = area.area,
                        .slot = target.index,
                        .domain = tattoo.domain,
                        .section = tattoo.section,
                        .name = tattoo.name,
                        .color = tattoo.color,
                        .alpha = tattoo.alpha,
                        .glow = tattoo.glow,
                        .emissiveMult = tattoo.emissiveMult,
                        .glossiness = tattoo.glossiness,
                        .specularStrength = tattoo.specularStrength,
                    },
                });
                if (!added) return added;

                auto restoredSlots = querySlots(snapshot.actorFormId, area.area);
                if (!restoredSlots) {
                    return std::unexpected(std::move(restoredSlots.error()));
                }
                const auto restored = std::ranges::find(
                    restoredSlots->slots, target.index, &core::TattooSlot::index);
                if (restored == restoredSlots->slots.end() || !restored->tattoo) {
                    return std::unexpected(core::ServiceError{
                        core::ServiceErrorCode::updateFailed,
                        "Rollback tattoo could not be read after restore",
                        core::MutationSideEffect::mayHaveOccurred,
                    });
                }
                const auto handle = restored->tattoo->runtimeHandle;
                bool metadataRestored = false;
                if (m_appearanceBindings) {
                    if (m_appearanceBindings->setTattooString &&
                        m_appearanceBindings->getTattooString &&
                        m_appearanceBindings->setTattooInt &&
                        m_appearanceBindings->getTattooInt) {
                        m_appearanceBindings->setTattooString(
                            handle, "bump", tattoo.bump.c_str());
                        m_appearanceBindings->setTattooString(
                            handle, "glowTexture", tattoo.glowTexture.c_str());
                        m_appearanceBindings->setTattooInt(
                            handle, "locked", tattoo.locked ? 1 : 0);
                        metadataRestored =
                            m_appearanceBindings->getTattooString(
                                handle, "bump", tattoo.bump.empty() ? "\x01" : "") ==
                                tattoo.bump &&
                            m_appearanceBindings->getTattooString(
                                handle, "glowTexture",
                                tattoo.glowTexture.empty() ? "\x01" : "") ==
                                tattoo.glowTexture &&
                            m_appearanceBindings->getTattooInt(handle, "locked", -1) ==
                                (tattoo.locked ? 1 : 0);
                    }
                } else {
                    jcmini::JMap::setStr(handle, "bump", tattoo.bump.c_str());
                    jcmini::JMap::setStr(
                        handle, "glowTexture", tattoo.glowTexture.c_str());
                    metadataRestored =
                        jcmini::JMap::getStr(handle, "bump",
                            tattoo.bump.empty() ? "\x01" : "") == tattoo.bump &&
                        jcmini::JMap::getStr(handle, "glowTexture",
                            tattoo.glowTexture.empty() ? "\x01" : "") == tattoo.glowTexture &&
                        jcmini::JMap::setIntAndVerify(
                            handle, "locked", tattoo.locked ? 1 : 0);
                }
                if (!metadataRestored) {
                    return std::unexpected(core::ServiceError{
                        core::ServiceErrorCode::updateFailed,
                        "Rollback tattoo metadata did not verify",
                        core::MutationSideEffect::mayHaveOccurred,
                    });
                }
            }
        }
    }
    return {};
}

core::TattooLoadoutVerification SlaveTatsRuntime::verifyRollback(
    const core::TattooLoadoutRollbackSnapshot& snapshot) {
    core::TattooLoadoutVerification result{.matches = true};
    for (const auto& area : snapshot.areas) {
        auto current = querySlots(snapshot.actorFormId, area.area);
        if (!current) {
            result.matches = false;
            result.affectedSlots.push_back({area.area, -1, current.error().message});
            continue;
        }
        for (const auto& target : area.slots) {
            const auto found = std::ranges::find(
                current->slots, target.index, &core::TattooSlot::index);
            bool matches = found != current->slots.end() &&
                found->occupancy == target.occupancy;
            if (matches && target.tattoo) {
                matches = found->tattoo && sameRollbackTattoo(*found->tattoo, *target.tattoo);
            }
            if (!matches) {
                result.matches = false;
                result.affectedSlots.push_back(
                    {area.area, target.index, "Rollback tattoo state did not verify"});
            }
        }
    }
    return result;
}

}  // namespace stui::runtime
