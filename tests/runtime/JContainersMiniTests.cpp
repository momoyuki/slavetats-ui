#include "jcontainers_mini.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

struct CallbackState {
    void* expectedDomain{reinterpret_cast<void*>(0x1234)};
    void* receivedDomain{};
    RE::TESForm* receivedForm{};
    int object{};
    int integerValue{};
    int integerDefault{};
    float floatValue{};
    float floatDefault{};
    std::string key;
    int integerSetterCalls{};
    int integerGetterCalls{};
    int floatSetterCalls{};
    int floatGetterCalls{};
    bool persistInteger{true};
    bool persistFloat{true};
};

CallbackState* g_state{};

void setMapInt(void* domain, std::int32_t object, RE::BSFixedString key, std::int32_t value) {
    g_state->receivedDomain = domain;
    g_state->object = object;
    g_state->key = key.c_str();
    ++g_state->integerSetterCalls;
    if (g_state->persistInteger) {
        g_state->integerValue = value;
    }
}

std::int32_t getMapInt(
    void* domain, std::int32_t object, RE::BSFixedString key, std::int32_t fallback) {
    g_state->receivedDomain = domain;
    g_state->object = object;
    g_state->key = key.c_str();
    g_state->integerDefault = fallback;
    ++g_state->integerGetterCalls;
    return g_state->persistInteger ? g_state->integerValue : fallback;
}

void setMapFloat(void* domain, std::int32_t object, RE::BSFixedString key, float value) {
    g_state->receivedDomain = domain;
    g_state->object = object;
    g_state->key = key.c_str();
    ++g_state->floatSetterCalls;
    if (g_state->persistFloat) {
        g_state->floatValue = value;
    }
}

float getMapFloat(void* domain, std::int32_t object, RE::BSFixedString key, float fallback) {
    g_state->receivedDomain = domain;
    g_state->object = object;
    g_state->key = key.c_str();
    g_state->floatDefault = fallback;
    ++g_state->floatGetterCalls;
    return g_state->persistFloat ? g_state->floatValue : fallback;
}

void setFormInt(void* domain, RE::TESForm* form, RE::BSFixedString path, std::int32_t value) {
    g_state->receivedDomain = domain;
    g_state->receivedForm = form;
    g_state->key = path.c_str();
    ++g_state->integerSetterCalls;
    if (g_state->persistInteger) {
        g_state->integerValue = value;
    }
}

std::int32_t getFormInt(
    void* domain, RE::TESForm* form, RE::BSFixedString path, std::int32_t fallback) {
    g_state->receivedDomain = domain;
    g_state->receivedForm = form;
    g_state->key = path.c_str();
    g_state->integerDefault = fallback;
    ++g_state->integerGetterCalls;
    return g_state->persistInteger ? g_state->integerValue : fallback;
}

struct JcminiPointerGuard {
    void* domain{jcmini::g_domain};
    decltype(jcmini::fn_jmap_setInt) mapSetInt{jcmini::fn_jmap_setInt};
    decltype(jcmini::fn_jmap_getInt) mapGetInt{jcmini::fn_jmap_getInt};
    decltype(jcmini::fn_jmap_setFlt) mapSetFloat{jcmini::fn_jmap_setFlt};
    decltype(jcmini::fn_jmap_getFlt) mapGetFloat{jcmini::fn_jmap_getFlt};
    decltype(jcmini::fn_jfdb_setInt) formSetInt{jcmini::fn_jfdb_setInt};
    decltype(jcmini::fn_jfdb_getInt) formGetInt{jcmini::fn_jfdb_getInt};

    ~JcminiPointerGuard() {
        jcmini::g_domain = domain;
        jcmini::fn_jmap_setInt = mapSetInt;
        jcmini::fn_jmap_getInt = mapGetInt;
        jcmini::fn_jmap_setFlt = mapSetFloat;
        jcmini::fn_jmap_getFlt = mapGetFloat;
        jcmini::fn_jfdb_setInt = formSetInt;
        jcmini::fn_jfdb_getInt = formGetInt;
        g_state = nullptr;
    }
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void mapSetIntAndVerifyRejectsMissingCallbacksAndZeroObject() {
    JcminiPointerGuard guard;
    CallbackState state;
    g_state = &state;
    jcmini::g_domain = state.expectedDomain;

    jcmini::fn_jmap_setInt = nullptr;
    jcmini::fn_jmap_getInt = getMapInt;
    expect(!jcmini::JMap::setIntAndVerify(41, "color", 0x123456),
        "expected missing integer setter to be rejected");

    jcmini::fn_jmap_setInt = setMapInt;
    jcmini::fn_jmap_getInt = nullptr;
    expect(!jcmini::JMap::setIntAndVerify(41, "color", 0x123456),
        "expected missing integer getter to be rejected");

    jcmini::fn_jmap_getInt = getMapInt;
    expect(!jcmini::JMap::setIntAndVerify(0, "color", 0x123456),
        "expected zero JMap object to be rejected");
    expect(state.integerSetterCalls == 0 && state.integerGetterCalls == 0,
        "expected rejected integer writes not to call JContainers");
}

void mapSetIntAndVerifyForwardsSentinelAndRequiresExactReadback() {
    JcminiPointerGuard guard;
    CallbackState state;
    g_state = &state;
    jcmini::g_domain = state.expectedDomain;
    jcmini::fn_jmap_setInt = setMapInt;
    jcmini::fn_jmap_getInt = getMapInt;

    expect(jcmini::JMap::setIntAndVerify(41, "color", 0x123456),
        "expected exact integer readback to succeed");
    expect(state.receivedDomain == state.expectedDomain && state.object == 41 &&
            state.key == "color" && state.integerValue == 0x123456,
        "expected integer write arguments to be forwarded exactly");
    expect(state.integerDefault == std::numeric_limits<int>::min(),
        "expected integer readback to use the missing-value sentinel");

    state.persistInteger = false;
    expect(!jcmini::JMap::setIntAndVerify(41, "color", 7),
        "expected ineffective integer readback to fail verification");
}

void mapSetFloatAndVerifyRejectsMissingCallbacksAndZeroObject() {
    JcminiPointerGuard guard;
    CallbackState state;
    g_state = &state;
    jcmini::g_domain = state.expectedDomain;

    jcmini::fn_jmap_setFlt = nullptr;
    jcmini::fn_jmap_getFlt = getMapFloat;
    expect(!jcmini::JMap::setFltAndVerify(42, "invertedAlpha", 0.65F),
        "expected missing float setter to be rejected");

    jcmini::fn_jmap_setFlt = setMapFloat;
    jcmini::fn_jmap_getFlt = nullptr;
    expect(!jcmini::JMap::setFltAndVerify(42, "invertedAlpha", 0.65F),
        "expected missing float getter to be rejected");

    jcmini::fn_jmap_getFlt = getMapFloat;
    expect(!jcmini::JMap::setFltAndVerify(0, "invertedAlpha", 0.65F),
        "expected zero JMap object to be rejected");
    expect(state.floatSetterCalls == 0 && state.floatGetterCalls == 0,
        "expected rejected float writes not to call JContainers");
}

void mapSetFloatAndVerifyUsesNanSentinelAndExactReadback() {
    JcminiPointerGuard guard;
    CallbackState state;
    g_state = &state;
    jcmini::g_domain = state.expectedDomain;
    jcmini::fn_jmap_setFlt = setMapFloat;
    jcmini::fn_jmap_getFlt = getMapFloat;

    expect(jcmini::JMap::setFltAndVerify(42, "invertedAlpha", 0.65F),
        "expected exact float readback to succeed");
    expect(state.receivedDomain == state.expectedDomain && state.object == 42 &&
            state.key == "invertedAlpha" && state.floatValue == 0.65F,
        "expected float write arguments to be forwarded exactly");
    expect(std::isnan(state.floatDefault),
        "expected float readback to use a NaN missing-value sentinel");

    state.persistFloat = false;
    state.floatValue = 0.650001F;
    expect(!jcmini::JMap::setFltAndVerify(42, "invertedAlpha", 0.65F),
        "expected near but non-exact float readback to fail verification");
}

void formDbGetIntRejectsMissingInputsAndForwardsDefault() {
    JcminiPointerGuard guard;
    CallbackState state;
    g_state = &state;
    jcmini::g_domain = state.expectedDomain;
    auto* form = reinterpret_cast<RE::TESForm*>(0x5678);

    jcmini::fn_jfdb_getInt = nullptr;
    expect(jcmini::JFormDB::getInt(form, ".SlaveTats.updated", 17) == 17,
        "expected missing JFormDB getter to return the caller default");

    jcmini::fn_jfdb_getInt = getFormInt;
    expect(jcmini::JFormDB::getInt(nullptr, ".SlaveTats.updated", 18) == 18,
        "expected null form to return the caller default");
    expect(state.integerGetterCalls == 0,
        "expected rejected JFormDB reads not to call JContainers");

    state.integerValue = 23;
    expect(jcmini::JFormDB::getInt(form, ".SlaveTats.updated", 19) == 23,
        "expected JFormDB getter result to be returned");
    expect(state.receivedDomain == state.expectedDomain && state.receivedForm == form &&
            state.key == ".SlaveTats.updated" && state.integerDefault == 19,
        "expected JFormDB read arguments and caller default to be forwarded exactly");
}

void formDbSetIntAndVerifyRejectsMissingCallbacksAndNullForm() {
    JcminiPointerGuard guard;
    CallbackState state;
    g_state = &state;
    jcmini::g_domain = state.expectedDomain;
    auto* form = reinterpret_cast<RE::TESForm*>(0x5678);

    jcmini::fn_jfdb_setInt = nullptr;
    jcmini::fn_jfdb_getInt = getFormInt;
    expect(!jcmini::JFormDB::setIntAndVerify(form, ".SlaveTats.updated", 1),
        "expected missing JFormDB setter to be rejected");

    jcmini::fn_jfdb_setInt = setFormInt;
    jcmini::fn_jfdb_getInt = nullptr;
    expect(!jcmini::JFormDB::setIntAndVerify(form, ".SlaveTats.updated", 1),
        "expected missing JFormDB getter to be rejected");

    jcmini::fn_jfdb_getInt = getFormInt;
    expect(!jcmini::JFormDB::setIntAndVerify(nullptr, ".SlaveTats.updated", 1),
        "expected null form write to be rejected");
    expect(state.integerSetterCalls == 0 && state.integerGetterCalls == 0,
        "expected rejected JFormDB writes not to call JContainers");
}

void formDbSetIntAndVerifyForwardsSentinelAndRequiresExactReadback() {
    JcminiPointerGuard guard;
    CallbackState state;
    g_state = &state;
    jcmini::g_domain = state.expectedDomain;
    jcmini::fn_jfdb_setInt = setFormInt;
    jcmini::fn_jfdb_getInt = getFormInt;
    auto* form = reinterpret_cast<RE::TESForm*>(0x5678);

    expect(jcmini::JFormDB::setIntAndVerify(form, ".SlaveTats.updated", 1),
        "expected exact JFormDB integer readback to succeed");
    expect(state.receivedDomain == state.expectedDomain && state.receivedForm == form &&
            state.key == ".SlaveTats.updated" && state.integerValue == 1,
        "expected JFormDB write arguments to be forwarded exactly");
    expect(state.integerDefault == std::numeric_limits<int>::min(),
        "expected JFormDB readback to use the missing-value sentinel");

    state.persistInteger = false;
    expect(!jcmini::JFormDB::setIntAndVerify(form, ".SlaveTats.updated", 2),
        "expected ineffective JFormDB readback to fail verification");
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
    failures += run("JValue copy rejects missing callback and source alias", [] {
        const auto saved = jcmini::fn_jval_shallowCopy;
        jcmini::fn_jval_shallowCopy = nullptr;
        expect(jcmini::JValue::shallowCopy(12) == 0, "missing copy must fail closed");
        jcmini::fn_jval_shallowCopy = [](void*, std::int32_t source) { return source; };
        expect(jcmini::JValue::shallowCopy(12) == 0, "shared source must never be returned as a copy");
        jcmini::fn_jval_shallowCopy = [](void*, std::int32_t source) { return source + 1; };
        expect(jcmini::JValue::shallowCopy(12) == 13, "independent copy must be returned");
        expect(jcmini::JValue::shallowCopy(0) == 0, "null source must not be copied");
        jcmini::fn_jval_shallowCopy = saved;
    });
    failures += run("JMap integer verify rejects missing callbacks and zero object",
        mapSetIntAndVerifyRejectsMissingCallbacksAndZeroObject);
    failures += run("JMap integer verify forwards sentinel and requires exact readback",
        mapSetIntAndVerifyForwardsSentinelAndRequiresExactReadback);
    failures += run("JMap float verify rejects missing callbacks and zero object",
        mapSetFloatAndVerifyRejectsMissingCallbacksAndZeroObject);
    failures += run("JMap float verify uses NaN sentinel and exact readback",
        mapSetFloatAndVerifyUsesNanSentinelAndExactReadback);
    failures += run("JFormDB integer getter rejects missing inputs and forwards default",
        formDbGetIntRejectsMissingInputsAndForwardsDefault);
    failures += run("JFormDB integer verify rejects missing callbacks and null form",
        formDbSetIntAndVerifyRejectsMissingCallbacksAndNullForm);
    failures += run("JFormDB integer verify forwards sentinel and requires exact readback",
        formDbSetIntAndVerifyForwardsSentinelAndRequiresExactReadback);
    return failures == 0 ? 0 : 1;
}
