#define ENABLE_COMMONLIBSSE_TESTING

#include "runtime/SlaveTatsRuntime.h"

#include "jcontainers_mini.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using stui::core::ServiceErrorCode;
using stui::core::SetTattooLockedRequest;
using stui::core::UpdateTattooAppearanceMode;
using stui::core::UpdateTattooAppearanceRequest;
using stui::runtime::SlaveTatsAppearanceBindings;
using stui::runtime::SlaveTatsRuntime;

using TattooFieldKey = std::pair<std::int32_t, std::string>;

struct IntegerRead {
    std::int32_t handle{};
    std::string key;
    std::int32_t fallback{};
};

struct FloatRead {
    std::int32_t handle{};
    std::string key;
    float fallback{};
};

struct StringRead {
    std::int32_t handle{};
    std::string key;
    std::string fallback;
};

struct QueryState {
    void* actor{reinterpret_cast<void*>(0x1234)};
    int queryCount{};
    int applyCount{};
    int removeCount{};
    int synchronizeCount{};
    int updatedWriteCount{};
    int arrayCreateCount{};
    std::int32_t nextArrayHandle{900};
    std::int32_t availableTattoo{};
    std::int32_t slotTattoo{};
    std::map<std::int32_t, std::vector<std::int32_t>> objectArrays;
    std::map<std::int32_t, std::vector<std::int32_t>> integerArrays;
    std::map<TattooFieldKey, std::int32_t> integers;
    std::map<TattooFieldKey, float> floats;
    std::map<TattooFieldKey, std::string> strings;
    std::vector<IntegerRead> integerReads;
    std::vector<FloatRead> floatReads;
    std::vector<StringRead> stringReads;
};

QueryState* g_queryState{};
std::vector<std::unique_ptr<std::byte[]>> g_fixedStringStorage;

void expect(bool condition, std::string_view message);

RE::BSFixedString* fixedStringConstructor(RE::BSFixedString* self, const char* value) {
    const auto length = std::strlen(value);
    auto storage = std::make_unique<std::byte[]>(sizeof(RE::BSStringPool::Entry) + length + 1);
    std::memset(storage.get(), 0, sizeof(RE::BSStringPool::Entry) + length + 1);
    auto* entry = reinterpret_cast<RE::BSStringPool::Entry*>(storage.get());
    entry->_flags = RE::BSStringPool::Entry::kRefCountMask;
    entry->_length = static_cast<std::uint32_t>(length);
    auto* data = reinterpret_cast<char*>(entry + 1);
    std::memcpy(data, value, length + 1);
    *reinterpret_cast<const char**>(self) = data;
    g_fixedStringStorage.push_back(std::move(storage));
    return self;
}

void fixedStringRelease(const char*& value) {
    value = nullptr;
}

class CommonLibTestHostGuard {
public:
    CommonLibTestHostGuard() {
        const auto constructor = reinterpret_cast<std::uintptr_t>(&fixedStringConstructor);
        const auto release = reinterpret_cast<std::uintptr_t>(&fixedStringRelease);
        const auto base = std::min(constructor, release) & ~std::uintptr_t{0xFFFFFFFF};

        expect(REL::Module::mock(
                   REL::Version{1, 6, 999, 0},
                   REL::Module::Runtime::AE,
                   L"SkyrimSE.exe",
                   base),
            "expected CommonLib test module mock to initialize");

        m_databasePath = std::filesystem::temp_directory_path()
            / ("slavetats-ui-runtime-query-addresses-" +
                std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".csv");
        std::ofstream database(m_databasePath, std::ios::trunc);
        database << "id,offset\n";
        database << "2,1.6.999.0\n";
        database << "69161," << std::hex << constructor - base << '\n';
        database << "69192," << std::hex << release - base << '\n';
        database.close();

        expect(REL::IDDatabase::inject(
                   m_databasePath.wstring(),
                   REL::IDDatabase::Format::VR,
                   REL::Version{1, 6, 999, 0}),
            "expected CommonLib test address database to initialize");
    }

    ~CommonLibTestHostGuard() {
        REL::IDDatabase::reset();
        REL::Module::reset();
        g_fixedStringStorage.clear();
        std::error_code ignored;
        std::filesystem::remove(m_databasePath, ignored);
    }

private:
    std::filesystem::path m_databasePath;
};

std::int32_t arrayObject(void*) {
    ++g_queryState->arrayCreateCount;
    return g_queryState->nextArrayHandle++;
}

std::int32_t arrayCount(void*, std::int32_t array) {
    if (const auto objects = g_queryState->objectArrays.find(array);
        objects != g_queryState->objectArrays.end()) {
        return static_cast<std::int32_t>(objects->second.size());
    }
    if (const auto integers = g_queryState->integerArrays.find(array);
        integers != g_queryState->integerArrays.end()) {
        return static_cast<std::int32_t>(integers->second.size());
    }
    return 0;
}

std::int32_t arrayGetObject(
    void*, std::int32_t array, std::int32_t index, std::int32_t fallback) {
    const auto found = g_queryState->objectArrays.find(array);
    if (found == g_queryState->objectArrays.end() || index < 0 ||
        static_cast<std::size_t>(index) >= found->second.size()) {
        return fallback;
    }
    return found->second[static_cast<std::size_t>(index)];
}

std::int32_t arrayGetInteger(
    void*, std::int32_t array, std::int32_t index, std::int32_t fallback) {
    const auto found = g_queryState->integerArrays.find(array);
    if (found == g_queryState->integerArrays.end() || index < 0 ||
        static_cast<std::size_t>(index) >= found->second.size()) {
        return fallback;
    }
    return found->second[static_cast<std::size_t>(index)];
}

std::int32_t addToPool(void*, std::int32_t object, RE::BSFixedString) {
    return object;
}

void cleanPool(void*, RE::BSFixedString) {}

std::int32_t mapGetInteger(
    void*, std::int32_t handle, RE::BSFixedString key, std::int32_t fallback) {
    g_queryState->integerReads.push_back(IntegerRead{handle, key.c_str(), fallback});
    const auto found = g_queryState->integers.find({handle, key.c_str()});
    return found == g_queryState->integers.end() ? fallback : found->second;
}

float mapGetFloat(void*, std::int32_t handle, RE::BSFixedString key, float fallback) {
    g_queryState->floatReads.push_back(FloatRead{handle, key.c_str(), fallback});
    const auto found = g_queryState->floats.find({handle, key.c_str()});
    return found == g_queryState->floats.end() ? fallback : found->second;
}

RE::BSFixedString mapGetString(
    void*, std::int32_t handle, RE::BSFixedString key, RE::BSFixedString fallback) {
    g_queryState->stringReads.push_back(StringRead{handle, key.c_str(), fallback.c_str()});
    const auto found = g_queryState->strings.find({handle, key.c_str()});
    return found == g_queryState->strings.end()
        ? fallback
        : RE::BSFixedString(found->second.c_str());
}

bool queryAvailableTattoos(
    std::int32_t, std::int32_t matches, std::int32_t, RE::BSFixedString) {
    ++g_queryState->queryCount;
    g_queryState->objectArrays[matches] = {g_queryState->availableTattoo};
    return false;
}

bool queryExternalSlots(RE::Actor*, RE::BSFixedString, std::int32_t matches) {
    ++g_queryState->queryCount;
    g_queryState->integerArrays[matches] = {};
    return false;
}

std::int32_t getAppliedTattooInSlot(RE::Actor*, RE::BSFixedString, std::int32_t slot) {
    ++g_queryState->queryCount;
    return slot == 0 ? g_queryState->slotTattoo : 0;
}

std::int32_t addAndGetTattoo(RE::Actor*, int, int, bool, bool, bool) {
    ++g_queryState->applyCount;
    return 73;
}

bool removeTattooFromSlot(RE::Actor*, RE::BSFixedString, int, bool, bool) {
    ++g_queryState->removeCount;
    return false;
}

bool synchronizeTattoos(RE::Actor*, bool) {
    ++g_queryState->synchronizeCount;
    return false;
}

void setActorInteger(void*, RE::TESForm*, RE::BSFixedString, std::int32_t) {
    ++g_queryState->updatedWriteCount;
}

const slavetats::interface::Addresses& queryApi() {
    static const slavetats::interface::Addresses api{
        .current_version = slavetats::interface::Addresses::version,
        .synchronize_tattoos = synchronizeTattoos,
        .query_available_tattoos = queryAvailableTattoos,
        .remove_tattoo_from_slot = removeTattooFromSlot,
        .get_applied_tattoo_in_slot = getAppliedTattooInSlot,
        .external_slots = queryExternalSlots,
        .add_and_get_tattoo = addAndGetTattoo,
    };
    return api;
}

struct JcminiPointerGuard {
    void* domain{jcmini::g_domain};
    decltype(jcmini::fn_jarr_object) arrayObject{jcmini::fn_jarr_object};
    decltype(jcmini::fn_jarr_count) arrayCount{jcmini::fn_jarr_count};
    decltype(jcmini::fn_jarr_getObj) arrayGetObject{jcmini::fn_jarr_getObj};
    decltype(jcmini::fn_jarr_getInt) arrayGetInteger{jcmini::fn_jarr_getInt};
    decltype(jcmini::fn_jmap_getStr) mapGetString{jcmini::fn_jmap_getStr};
    decltype(jcmini::fn_jmap_getInt) mapGetInteger{jcmini::fn_jmap_getInt};
    decltype(jcmini::fn_jmap_getFlt) mapGetFloat{jcmini::fn_jmap_getFlt};
    decltype(jcmini::fn_jval_addToPool) addToPool{jcmini::fn_jval_addToPool};
    decltype(jcmini::fn_jval_cleanPool) cleanPool{jcmini::fn_jval_cleanPool};
    decltype(jcmini::fn_jfdb_setInt) setActorInteger{jcmini::fn_jfdb_setInt};

    explicit JcminiPointerGuard(QueryState& state) {
        g_queryState = &state;
        jcmini::g_domain = &state;
        jcmini::fn_jarr_object = ::arrayObject;
        jcmini::fn_jarr_count = ::arrayCount;
        jcmini::fn_jarr_getObj = ::arrayGetObject;
        jcmini::fn_jarr_getInt = ::arrayGetInteger;
        jcmini::fn_jmap_getStr = ::mapGetString;
        jcmini::fn_jmap_getInt = ::mapGetInteger;
        jcmini::fn_jmap_getFlt = ::mapGetFloat;
        jcmini::fn_jval_addToPool = ::addToPool;
        jcmini::fn_jval_cleanPool = ::cleanPool;
        jcmini::fn_jfdb_setInt = ::setActorInteger;
    }

    ~JcminiPointerGuard() {
        jcmini::g_domain = domain;
        jcmini::fn_jarr_object = arrayObject;
        jcmini::fn_jarr_count = arrayCount;
        jcmini::fn_jarr_getObj = arrayGetObject;
        jcmini::fn_jarr_getInt = arrayGetInteger;
        jcmini::fn_jmap_getStr = mapGetString;
        jcmini::fn_jmap_getInt = mapGetInteger;
        jcmini::fn_jmap_getFlt = mapGetFloat;
        jcmini::fn_jval_addToPool = addToPool;
        jcmini::fn_jval_cleanPool = cleanPool;
        jcmini::fn_jfdb_setInt = setActorInteger;
        g_queryState = nullptr;
    }
};

struct BindingState {
    void* actor{reinterpret_cast<void*>(0x1234)};
    std::uint32_t actorFormId{0x14};
    bool actorLoaded{true};
    void* queriedActor{};
    void* updatedActor{};
    void* synchronizedActor{};
    bool synchronizationFailed{};
    bool persistUpdated{true};
    int queryCount{};
    int integerWriteCount{};
    int floatWriteCount{};
    int updatedWriteCount{};
    int synchronizeCount{};
    bool synchronizedSilently{true};
    std::vector<std::int32_t> handles{73};
    std::map<TattooFieldKey, std::int32_t> integers;
    std::map<TattooFieldKey, float> floats;
    std::vector<std::string> appearanceWriteKeys;
    std::vector<std::string> appearanceReadKeys;
    std::string ineffectiveReadbackKey;
    std::int32_t updatedValue{};
    std::string updatedPath;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

SlaveTatsAppearanceBindings bindingsFor(BindingState& state) {
    return SlaveTatsAppearanceBindings{
        .resolveActor = [&state](std::uint32_t actorFormId) -> void* {
            return actorFormId == state.actorFormId ? state.actor : nullptr;
        },
        .isActor3DLoaded = [&state](void* actor) {
            return actor == state.actor && state.actorLoaded;
        },
        .queryAppliedTattooHandles = [&state](void* actor) {
            ++state.queryCount;
            state.queriedActor = actor;
            return stui::runtime::AppliedTattooHandleQueryResult{state.handles};
        },
        .setTattooInt = [&state](std::int32_t handle, const char* key, std::int32_t value) {
            ++state.integerWriteCount;
            state.appearanceWriteKeys.emplace_back(key);
            state.integers[{handle, key}] = value;
        },
        .getTattooInt = [&state](
                            std::int32_t handle,
                            const char* key,
                            std::int32_t fallback) {
            state.appearanceReadKeys.emplace_back(key);
            if (state.ineffectiveReadbackKey == key) {
                return fallback;
            }
            const auto found = state.integers.find({handle, key});
            return found == state.integers.end() ? fallback : found->second;
        },
        .setTattooFloat = [&state](std::int32_t handle, const char* key, float value) {
            ++state.floatWriteCount;
            state.appearanceWriteKeys.emplace_back(key);
            state.floats[{handle, key}] = value;
        },
        .getTattooFloat = [&state](std::int32_t handle, const char* key, float fallback) {
            state.appearanceReadKeys.emplace_back(key);
            if (state.ineffectiveReadbackKey == key) {
                return fallback;
            }
            const auto found = state.floats.find({handle, key});
            return found == state.floats.end() ? fallback : found->second;
        },
        .setActorInt = [&state](void* actor, const char* path, std::int32_t value) {
            ++state.updatedWriteCount;
            state.updatedActor = actor;
            state.updatedPath = path;
            if (state.persistUpdated) {
                state.updatedValue = value;
            }
        },
        .getActorInt = [&state](void*, const char*, std::int32_t fallback) {
            return state.persistUpdated ? state.updatedValue : fallback;
        },
        .synchronizeTattoos = [&state](void* actor, bool silent) {
            ++state.synchronizeCount;
            state.synchronizedActor = actor;
            state.synchronizedSilently = silent;
            return state.synchronizationFailed;
        },
    };
}

UpdateTattooAppearanceRequest request(UpdateTattooAppearanceMode mode =
        UpdateTattooAppearanceMode::updateAndSynchronize) {
    return UpdateTattooAppearanceRequest{
        .actorFormId = 0x14,
        .runtimeHandle = 73,
        .color = 0x123456,
        .alpha = 0.35F,
        .glow = 0x102030,
        .glossiness = 2.5F,
        .specularStrength = 1.25F,
        .emissiveMult = 3.0F,
        .mode = mode,
    };
}

SetTattooLockedRequest lockRequest(bool locked) {
    return SetTattooLockedRequest{
        .actorFormId = 0x14,
        .runtimeHandle = 73,
        .locked = locked,
    };
}

void seedBaseTattoo(QueryState& state, std::int32_t handle) {
    state.strings[{handle, "domain"}] = "default";
    state.strings[{handle, "section"}] = "Test Section";
    state.strings[{handle, "name"}] = "Test Tattoo";
    state.strings[{handle, "texture"}] = "textures\\actors\\character\\test.dds";
    state.strings[{handle, "area"}] = "BODY";
    state.integers[{handle, "slot"}] = 0;
    state.integers[{handle, "color"}] = 0xA0B0C0;
    state.floats[{handle, "invertedAlpha"}] = 0.25F;
}

void seedAdvancedTattoo(QueryState& state, std::int32_t handle) {
    seedBaseTattoo(state, handle);
    state.integers[{handle, "glow"}] = 0x102030;
    state.floats[{handle, "glossiness"}] = 2.5F;
    state.floats[{handle, "specularStrength"}] = 1.25F;
    state.strings[{handle, "bump"}] = "textures\\packs\\exact-bump_n.dds";
    state.strings[{handle, "glowTexture"}] = "textures\\packs\\exact-glow_g.dds";
    state.floats[{handle, "emissiveMult"}] = 3.75F;
}

void expectAdvancedSnapshot(const stui::core::TattooEntry& tattoo) {
    expect(tattoo.glow == 0x102030, "expected exact glow RGB in runtime snapshot");
    expect(tattoo.glossiness == 2.5F, "expected exact glossiness in runtime snapshot");
    expect(tattoo.specularStrength == 1.25F,
        "expected exact specular strength in runtime snapshot");
    expect(tattoo.bump == "textures\\packs\\exact-bump_n.dds",
        "expected bump path preserved without derivation or normalization");
    expect(tattoo.glowTexture == "textures\\packs\\exact-glow_g.dds",
        "expected glow texture path preserved without derivation or normalization");
    expect(tattoo.emissiveMult == 3.75F,
        "expected exact emissive multiplier in runtime snapshot");
}

void expectAdvancedDefaults(const stui::core::TattooEntry& tattoo) {
    expect(tattoo.glow == 0, "expected missing glow to default to zero");
    expect(tattoo.glossiness == 0.0F,
        "expected missing glossiness to default to zero");
    expect(tattoo.specularStrength == 0.0F,
        "expected missing specular strength to default to zero");
    expect(tattoo.bump.empty(), "expected missing bump path to remain empty");
    expect(tattoo.glowTexture.empty(),
        "expected missing glow texture path to remain empty");
    expect(tattoo.emissiveMult == 1.0F,
        "expected missing emissive multiplier to default to one");
}

void expectAdvancedKeysReadWithDefaults(const QueryState& state, std::int32_t handle) {
    const auto hasIntegerRead = [&](std::string_view key, std::int32_t fallback) {
        for (const auto& read : state.integerReads) {
            if (read.handle == handle && read.key == key && read.fallback == fallback) {
                return true;
            }
        }
        return false;
    };
    const auto hasFloatRead = [&](std::string_view key, float fallback) {
        for (const auto& read : state.floatReads) {
            if (read.handle == handle && read.key == key && read.fallback == fallback) {
                return true;
            }
        }
        return false;
    };
    const auto hasStringRead = [&](std::string_view key) {
        for (const auto& read : state.stringReads) {
            if (read.handle == handle && read.key == key && read.fallback.empty()) {
                return true;
            }
        }
        return false;
    };

    expect(hasIntegerRead("glow", 0), "expected glow read with zero default");
    expect(hasFloatRead("glossiness", 0.0F),
        "expected glossiness read with zero default");
    expect(hasFloatRead("specularStrength", 0.0F),
        "expected specular strength read with zero default");
    expect(hasStringRead("bump"), "expected bump read with empty default");
    expect(hasStringRead("glowTexture"),
        "expected glow texture read with empty default");
    expect(hasFloatRead("emissiveMult", 1.0F),
        "expected emissive multiplier read with one default");
}

void runtimeQueriesPreserveAdvancedSnapshotFields() {
    CommonLibTestHostGuard hostGuard;
    QueryState queryState;
    JcminiPointerGuard guard(queryState);
    BindingState bindingState;
    SlaveTatsRuntime runtime(bindingsFor(bindingState));
    runtime.bindSlaveTats(&queryApi());

    queryState.availableTattoo = 73;
    queryState.slotTattoo = 74;
    seedAdvancedTattoo(queryState, queryState.availableTattoo);
    seedAdvancedTattoo(queryState, queryState.slotTattoo);

    const auto available = runtime.queryAvailable("default");
    expect(available && available->size() == 1,
        "expected one available tattoo snapshot");
    expectAdvancedSnapshot(available->front());

    const auto slots = runtime.querySlots(0x14, stui::core::TattooArea::body);
    expect(slots && !slots->slots.empty() && slots->slots.front().tattoo.has_value(),
        "expected occupied slot tattoo snapshot");
    expectAdvancedSnapshot(*slots->slots.front().tattoo);
}

void runtimeQueriesReadMissingAdvancedKeysWithDocumentedDefaults() {
    CommonLibTestHostGuard hostGuard;
    QueryState queryState;
    JcminiPointerGuard guard(queryState);
    BindingState bindingState;
    SlaveTatsRuntime runtime(bindingsFor(bindingState));
    runtime.bindSlaveTats(&queryApi());

    queryState.availableTattoo = 81;
    queryState.slotTattoo = 82;
    seedBaseTattoo(queryState, queryState.availableTattoo);
    seedBaseTattoo(queryState, queryState.slotTattoo);

    const auto available = runtime.queryAvailable("default");
    expect(available && available->size() == 1,
        "expected legacy available tattoo snapshot");
    expectAdvancedDefaults(available->front());
    expectAdvancedKeysReadWithDefaults(queryState, queryState.availableTattoo);

    const auto slots = runtime.querySlots(0x14, stui::core::TattooArea::body);
    expect(slots && !slots->slots.empty() && slots->slots.front().tattoo.has_value(),
        "expected legacy occupied slot tattoo snapshot");
    expectAdvancedDefaults(*slots->slots.front().tattoo);
    expectAdvancedKeysReadWithDefaults(queryState, queryState.slotTattoo);
}

void productionDelegationQueriesRequestedActorAndWritesExactAppearance() {
    BindingState state;
    SlaveTatsRuntime runtime(bindingsFor(state));

    const auto result = runtime.updateAppearance(request());

    expect(result.has_value(), "expected production runtime update to succeed");
    expect(state.queryCount == 1 && state.queriedActor == state.actor,
        "expected actor-specific applied-tattoo query");
    expect(state.integerWriteCount == 2 && state.integers[{73, "color"}] == 0x123456 &&
            state.integers[{73, "glow"}] == 0x102030,
        "expected exact diffuse and glow RGB writes with readback");
    expect(state.floatWriteCount == 4 &&
            state.floats[{73, "invertedAlpha"}] == 0.65F &&
            state.floats[{73, "glossiness"}] == 2.5F &&
            state.floats[{73, "specularStrength"}] == 1.25F &&
            state.floats[{73, "emissiveMult"}] == 3.0F,
        "expected exact alpha and material float writes with readback");
    expect(state.appearanceWriteKeys == std::vector<std::string>{
            "color",
            "invertedAlpha",
            "glow",
            "glossiness",
            "specularStrength",
            "emissiveMult",
        },
        "expected stable sequential appearance write order");
    expect(state.updatedWriteCount == 1 && state.updatedActor == state.actor &&
            state.updatedPath == ".SlaveTats.updated" && state.updatedValue == 1,
        "expected truthful actor updated marker");
    expect(state.synchronizeCount == 1 && state.synchronizedActor == state.actor &&
            !state.synchronizedSilently,
        "expected exactly one synchronize call with SlaveTats silent=false polarity");
}

void glowingAppearanceRestoresZeroGlowAndEmissionWithVerifiedWrites() {
    BindingState state;
    state.integers[{73, "glow"}] = 0x102030;
    state.floats[{73, "emissiveMult"}] = 3.0F;
    SlaveTatsRuntime runtime(bindingsFor(state));
    auto restore = request();
    restore.glow = 0;
    restore.emissiveMult = 0.0F;

    const auto result = runtime.updateAppearance(restore);

    expect(result.has_value(), "expected glowing appearance restoration to succeed");
    expect(state.integers[{73, "glow"}] == 0 && state.floats[{73, "emissiveMult"}] == 0.0F,
        "expected exact zero glow and emission values stored after previously non-zero values");
    expect(state.appearanceWriteKeys == std::vector<std::string>{
            "color",
            "invertedAlpha",
            "glow",
            "glossiness",
            "specularStrength",
            "emissiveMult",
        } && state.appearanceReadKeys == std::vector<std::string>{
            "color",
            "invertedAlpha",
            "glow",
            "glossiness",
            "specularStrength",
            "emissiveMult",
        }, "expected every appearance write, including zero values, verified by readback");
    expect(state.updatedWriteCount == 1 && state.updatedActor == state.actor &&
            state.updatedPath == ".SlaveTats.updated" && state.updatedValue == 1 &&
            state.synchronizeCount == 1 && state.synchronizedActor == state.actor &&
            !state.synchronizedSilently,
        "expected one truthful updated marker and one synchronization after verified restoration");
}

void staleHandlesNeverWriteMarkOrSynchronize() {
    for (const auto handles : std::vector<std::vector<std::int32_t>>{{}, {74}, {0}}) {
        BindingState state;
        state.handles = handles;
        SlaveTatsRuntime runtime(bindingsFor(state));

        auto staleRequest = request();
        if (handles == std::vector<std::int32_t>{0}) {
            staleRequest.runtimeHandle = 0;
        }
        const auto result = runtime.updateAppearance(staleRequest);

        expect(!result && result.error().code == ServiceErrorCode::staleTattooHandle,
            "expected zero, absent, or foreign handle to be stale");
        expect(state.integerWriteCount == 0 && state.floatWriteCount == 0 &&
                state.updatedWriteCount == 0 && state.synchronizeCount == 0,
            "expected stale path to perform no write, mark, or synchronization");
    }
}

void missingActorStopsBeforeQueryOrMutation() {
    BindingState state;
    state.actor = nullptr;
    SlaveTatsRuntime runtime(bindingsFor(state));

    const auto result = runtime.updateAppearance(request());

    expect(!result && result.error().code == ServiceErrorCode::actorNotFound,
        "expected missing requested actor to return actorNotFound");
    expect(state.queryCount == 0 && state.integerWriteCount == 0 &&
            state.floatWriteCount == 0 && state.updatedWriteCount == 0 &&
            state.synchronizeCount == 0,
        "expected missing actor path not to query, write, mark, or synchronize");
}

void unloadedActorStopsAppearanceAndRetry() {
    for (const auto mode : {UpdateTattooAppearanceMode::updateAndSynchronize,
             UpdateTattooAppearanceMode::synchronizeOnly}) {
        BindingState state;
        state.actorLoaded = false;
        SlaveTatsRuntime runtime(bindingsFor(state));
        const auto result = runtime.updateAppearance(request(mode));
        expect(!result && result.error().code == ServiceErrorCode::actorNotFound,
            "expected unloaded actor to reject appearance and retry");
        expect(state.queryCount == 0 && state.integerWriteCount == 0 &&
                state.floatWriteCount == 0 && state.updatedWriteCount == 0 &&
                state.synchronizeCount == 0,
            "expected unloaded appearance rejection before any query or mutation");
    }
}

void unloadedActorStopsLockAndUnlock() {
    for (const bool locked : {true, false}) {
        BindingState state;
        state.actorLoaded = false;
        SlaveTatsRuntime runtime(bindingsFor(state));
        const auto result = runtime.setTattooLocked(lockRequest(locked));
        expect(!result && result.error().code == ServiceErrorCode::actorNotFound,
            "expected unloaded actor to reject Lock and Unlock");
        expect(state.queryCount == 0 && state.integerWriteCount == 0 &&
                state.updatedWriteCount == 0 && state.synchronizeCount == 0,
            "expected unloaded Lock rejection before any query or mutation");
    }
}

void invalidActorStopsEveryOperationBeforeApiOrJContainers() {
    CommonLibTestHostGuard hostGuard;
    enum class InvalidActor { unloaded, missing, zeroId, missingResolver, missingLoadedCheck };
    for (const auto invalid : {InvalidActor::unloaded, InvalidActor::missing,
             InvalidActor::zeroId, InvalidActor::missingResolver, InvalidActor::missingLoadedCheck}) {
        QueryState queryState;
        JcminiPointerGuard guard(queryState);
        BindingState state;
        state.actorFormId = invalid == InvalidActor::zeroId ? 0 : 0x1234;
        state.actorLoaded = invalid != InvalidActor::unloaded;
        if (invalid == InvalidActor::missing) {
            state.actor = nullptr;
        }
        auto bindings = bindingsFor(state);
        if (invalid == InvalidActor::missingResolver) {
            bindings.resolveActor = {};
        }
        if (invalid == InvalidActor::missingLoadedCheck) {
            bindings.isActor3DLoaded = {};
        }
        SlaveTatsRuntime runtime(std::move(bindings));
        runtime.bindSlaveTats(&queryApi());
        const auto expectRejected = [&](const auto& result, std::string_view operation) {
            expect(!result && result.error().code == ServiceErrorCode::actorNotFound,
                std::string(operation) + " must reject an invalid actor");
            expect(queryState.queryCount == 0 && queryState.applyCount == 0 &&
                    queryState.removeCount == 0 && queryState.synchronizeCount == 0 &&
                    queryState.updatedWriteCount == 0 && queryState.arrayCreateCount == 0,
                "expected actor rejection before SlaveTatsNG or JContainers work");
            expect(state.queryCount == 0 && state.integerWriteCount == 0 &&
                    state.floatWriteCount == 0 && state.updatedWriteCount == 0 &&
                    state.synchronizeCount == 0,
                "expected actor rejection before binding query, write, mark, or sync");
        };
        expectRejected(runtime.querySlots(state.actorFormId, stui::core::TattooArea::body),
            "querySlots");
        auto appearance = request();
        appearance.actorFormId = state.actorFormId;
        expectRejected(runtime.updateAppearance(appearance), "updateAppearance");
        appearance.mode = UpdateTattooAppearanceMode::synchronizeOnly;
        expectRejected(runtime.updateAppearance(appearance), "appearance retry");
        for (const bool locked : {true, false}) {
            auto lock = lockRequest(locked);
            lock.actorFormId = state.actorFormId;
            expectRejected(runtime.setTattooLocked(lock), "setTattooLocked");
        }
        expectRejected(runtime.applyToSlot(stui::core::ApplyTattooRequest{
            .actorFormId = state.actorFormId,
            .area = stui::core::TattooArea::body,
            .slot = 0,
            .domain = "default",
            .section = "Test Section",
            .name = "Test Tattoo",
        }), "applyToSlot");
        for (const auto mode : {stui::core::RemoveTattooMode::removeAndSynchronize,
                 stui::core::RemoveTattooMode::synchronizeOnly}) {
            expectRejected(runtime.removeFromSlot(stui::core::RemoveTattooRequest{
                .actorFormId = state.actorFormId,
                .area = stui::core::TattooArea::body,
                .slot = 0,
                .mode = mode,
            }), "removeFromSlot");
        }
    }
}

void ineffectiveAppearanceReadbacksStopBeforeUpdatedMarkerAndSynchronization() {
    {
        BindingState state;
        state.ineffectiveReadbackKey = "color";
        SlaveTatsRuntime runtime(bindingsFor(state));

        const auto result = runtime.updateAppearance(request());
        expect(!result && result.error().code == ServiceErrorCode::updateFailed,
            "expected ineffective color write to return updateFailed");
        expect(state.integerWriteCount == 1 && state.floatWriteCount == 0 &&
                state.updatedWriteCount == 0 && state.synchronizeCount == 0,
            "expected color readback failure to stop later writes and synchronization");
    }

    {
        BindingState state;
        auto bindings = bindingsFor(state);
        bindings.getTattooFloat = [](std::int32_t, const char* key, float fallback) {
            return std::string_view(key) == "invertedAlpha" ? 0.650001F : fallback;
        };
        SlaveTatsRuntime runtime(std::move(bindings));

        const auto result = runtime.updateAppearance(request());
        expect(!result && result.error().code == ServiceErrorCode::updateFailed,
            "expected non-exact inverted-alpha readback to return updateFailed");
        expect(state.integerWriteCount == 1 && state.floatWriteCount == 1 &&
                state.updatedWriteCount == 0 && state.synchronizeCount == 0,
            "expected alpha readback failure to stop updated marker and synchronization");
    }

    struct FailureCase {
        std::string_view key;
        std::vector<std::string> expectedStoredKeys;
    };
    const std::vector<FailureCase> failureCases{
        {"glow", {"color", "invertedAlpha", "glow"}},
        {"glossiness", {"color", "invertedAlpha", "glow", "glossiness"}},
        {"specularStrength",
            {"color", "invertedAlpha", "glow", "glossiness", "specularStrength"}},
        {"emissiveMult",
            {"color", "invertedAlpha", "glow", "glossiness", "specularStrength",
                "emissiveMult"}},
    };
    for (const auto& failureCase : failureCases) {
        BindingState state;
        state.ineffectiveReadbackKey = failureCase.key;
        SlaveTatsRuntime runtime(bindingsFor(state));

        const auto result = runtime.updateAppearance(request());

        expect(!result && result.error().code == ServiceErrorCode::updateFailed,
            "expected ineffective material readback to return updateFailed");
        expect(state.appearanceWriteKeys == failureCase.expectedStoredKeys,
            "expected sequential writes to stop at the first failed material readback");
        expect(state.integers.size() + state.floats.size() ==
                failureCase.expectedStoredKeys.size(),
            "expected earlier sequential writes to remain stored after readback failure");
        expect(state.updatedWriteCount == 0 && state.synchronizeCount == 0,
            "expected material readback failure not to mark or synchronize");
    }
}

void failedUpdatedReadbackStopsBeforeSynchronization() {
    BindingState state;
    state.persistUpdated = false;
    SlaveTatsRuntime runtime(bindingsFor(state));

    const auto result = runtime.updateAppearance(request());

    expect(!result && result.error().code == ServiceErrorCode::updateFailed,
        "expected stable updateFailed when updated marker does not stick");
    expect(state.integerWriteCount == 2 && state.floatWriteCount == 4 &&
            state.updatedWriteCount == 1 && state.synchronizeCount == 0,
        "expected updated readback failure to stop before synchronization");
}

void synchronizeOnlyMarksAndUsesFailurePolarityWithoutAppearanceWrites() {
    BindingState state;
    state.synchronizationFailed = true;
    SlaveTatsRuntime runtime(bindingsFor(state));

    const auto result = runtime.updateAppearance(request(UpdateTattooAppearanceMode::synchronizeOnly));

    expect(!result && result.error().code == ServiceErrorCode::synchronizeFailed,
        "expected SlaveTats true return to map to synchronizeFailed");
    expect(state.queryCount == 0 && state.integerWriteCount == 0 &&
            state.floatWriteCount == 0,
        "expected synchronize-only path not to query or write appearance");
    expect(state.updatedWriteCount == 1 && state.synchronizeCount == 1,
        "expected synchronize-only path to mark and synchronize exactly once");
}

void lockStateWritesAndVerifiesWithoutSynchronization() {
    BindingState lockedState;
    SlaveTatsRuntime lockedRuntime(bindingsFor(lockedState));
    const auto lockedResult = lockedRuntime.setTattooLocked(lockRequest(true));

    expect(lockedResult.has_value() && lockedResult->locked,
        "expected lock state change success");
    expect(lockedState.queryCount == 1 && lockedState.queriedActor == lockedState.actor,
        "expected lock mutation to query the requested actor handles");
    expect(lockedState.integerWriteCount == 1 && lockedState.integers[{73, "locked"}] == 1,
        "expected Lock to write verified integer one");
    expect(lockedState.updatedWriteCount == 0 && lockedState.synchronizeCount == 0,
        "expected Lock not to mark updated or synchronize");

    BindingState unlockedState;
    SlaveTatsRuntime unlockedRuntime(bindingsFor(unlockedState));
    const auto unlockedResult = unlockedRuntime.setTattooLocked(lockRequest(false));

    expect(unlockedResult.has_value() && !unlockedResult->locked,
        "expected unlock state change success");
    expect(unlockedState.integerWriteCount == 1 && unlockedState.integers[{73, "locked"}] == 0,
        "expected Unlock to write verified integer zero");
    expect(unlockedState.updatedWriteCount == 0 && unlockedState.synchronizeCount == 0,
        "expected Unlock not to mark updated or synchronize");
}

void staleLockHandlePerformsNoWrite() {
    BindingState state;
    state.handles = {74};
    SlaveTatsRuntime runtime(bindingsFor(state));

    const auto result = runtime.setTattooLocked(lockRequest(true));

    expect(!result && result.error().code == ServiceErrorCode::staleTattooHandle,
        "expected foreign lock handle rejected as stale");
    expect(state.integerWriteCount == 0 && state.updatedWriteCount == 0 &&
            state.synchronizeCount == 0,
        "expected stale lock handle to perform no mutation");
}

void failedLockReadbackReturnsLockFailed() {
    BindingState state;
    state.ineffectiveReadbackKey = "locked";
    SlaveTatsRuntime runtime(bindingsFor(state));

    const auto result = runtime.setTattooLocked(lockRequest(true));

    expect(!result && result.error().code == ServiceErrorCode::lockFailed,
        "expected failed lock readback to return lockFailed");
    expect(state.integerWriteCount == 1 && state.updatedWriteCount == 0 &&
            state.synchronizeCount == 0,
        "expected failed lock readback not to mark or synchronize");
}

template <class Test>
int run(std::string_view name, Test&& test) {
    try {
        std::forward<Test>(test)();
        std::cout << "PASS " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        return 1;
    }
}

}  // namespace

int main() {
    int failures = 0;
    failures += run("runtime queries preserve advanced snapshot fields",
        runtimeQueriesPreserveAdvancedSnapshotFields);
    failures += run("runtime queries read missing advanced keys with documented defaults",
        runtimeQueriesReadMissingAdvancedKeysWithDocumentedDefaults);
    failures += run("production delegation queries actor and writes exact appearance",
        productionDelegationQueriesRequestedActorAndWritesExactAppearance);
    failures += run("glowing appearance restores zero glow and emission with verified writes",
        glowingAppearanceRestoresZeroGlowAndEmissionWithVerifiedWrites);
    failures += run("stale handles never mutate or synchronize",
        staleHandlesNeverWriteMarkOrSynchronize);
    failures += run("missing actor stops before query or mutation",
        missingActorStopsBeforeQueryOrMutation);
    failures += run("unloaded actor stops appearance and retry", unloadedActorStopsAppearanceAndRetry);
    failures += run("unloaded actor stops Lock and Unlock", unloadedActorStopsLockAndUnlock);
    failures += run("invalid actor stops every operation before API or JContainers",
        invalidActorStopsEveryOperationBeforeApiOrJContainers);
    failures += run("ineffective appearance readbacks stop before synchronization",
        ineffectiveAppearanceReadbacksStopBeforeUpdatedMarkerAndSynchronization);
    failures += run("failed updated readback stops before synchronization",
        failedUpdatedReadbackStopsBeforeSynchronization);
    failures += run("synchronize-only preserves no-write and failure polarity",
        synchronizeOnlyMarksAndUsesFailurePolarityWithoutAppearanceWrites);
    failures += run("lock state writes and verifies without synchronization",
        lockStateWritesAndVerifiesWithoutSynchronization);
    failures += run("stale lock handle performs no write", staleLockHandlePerformsNoWrite);
    failures += run("failed lock readback returns lock failed", failedLockReadbackReturnsLockFailed);
    return failures == 0 ? 0 : 1;
}
