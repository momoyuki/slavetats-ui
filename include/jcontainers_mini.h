#pragma once

// Minimal JContainers wrapper — only the functions SlaveTatsUI needs.
// Initialized from the jc::root_interface message sent by JContainers at kDataLoaded.

#include "JContainers/jc_interface.h"
#include "RE/Skyrim.h"
#include <limits>
#include <string>

namespace jcmini {

inline void* g_domain = nullptr;

// JArray
inline int32_t (*fn_jarr_object)(void*) = nullptr;
inline int32_t (*fn_jarr_count)(void*, int32_t) = nullptr;
inline int32_t (*fn_jarr_getObj)(void*, int32_t, int32_t, int32_t) = nullptr;
inline int32_t (*fn_jarr_getInt)(void*, int32_t, int32_t, int32_t) = nullptr;
inline void    (*fn_jarr_addObj)(void*, int32_t, int32_t, int32_t) = nullptr;
inline void    (*fn_jarr_addInt)(void*, int32_t, int32_t, int32_t) = nullptr;
inline void    (*fn_jarr_clear)(void*, int32_t) = nullptr;

// JMap read
inline RE::BSFixedString (*fn_jmap_getStr)(void*, int32_t, RE::BSFixedString, RE::BSFixedString) = nullptr;
inline int32_t           (*fn_jmap_getInt)(void*, int32_t, RE::BSFixedString, int32_t) = nullptr;
inline float             (*fn_jmap_getFlt)(void*, int32_t, RE::BSFixedString, float) = nullptr;

// JMap write
inline void (*fn_jmap_setStr)(void*, int32_t, RE::BSFixedString, RE::BSFixedString) = nullptr;
inline void (*fn_jmap_setInt)(void*, int32_t, RE::BSFixedString, int32_t) = nullptr;
inline void (*fn_jmap_setFlt)(void*, int32_t, RE::BSFixedString, float)   = nullptr;

// JFormDB (form-keyed storage — used by SlaveTats for per-actor data)
inline RE::BSFixedString (*fn_jfdb_getStr)(void*, RE::TESForm*, RE::BSFixedString, RE::BSFixedString) = nullptr;
inline int32_t           (*fn_jfdb_getInt)(void*, RE::TESForm*, RE::BSFixedString, int32_t)           = nullptr;
inline void              (*fn_jfdb_setInt)(void*, RE::TESForm*, RE::BSFixedString, int32_t)           = nullptr;

// JValue
inline int32_t (*fn_jval_addToPool)(void*, int32_t, RE::BSFixedString) = nullptr;
inline void    (*fn_jval_cleanPool)(void*, RE::BSFixedString) = nullptr;
inline int32_t (*fn_jval_shallowCopy)(void*, int32_t) = nullptr;

template <typename F>
static void bindFunc(const jc::reflection_interface* refl, const char* fn, const char* cls, F& out) {
    out = reinterpret_cast<F>(refl->tes_function_of_class(fn, cls));
}

inline bool Init(const jc::root_interface* root) {
    auto* refl   = root->query_interface<jc::reflection_interface>();
    auto* domain = root->query_interface<jc::domain_interface>();
    if (!refl || !domain) return false;

    g_domain = domain->get_default_domain();

    bindFunc(refl, "object",     "JArray", fn_jarr_object);
    bindFunc(refl, "count",      "JArray", fn_jarr_count);
    bindFunc(refl, "getObj",     "JArray", fn_jarr_getObj);
    bindFunc(refl, "getInt",     "JArray", fn_jarr_getInt);
    bindFunc(refl, "addObj",     "JArray", fn_jarr_addObj);
    bindFunc(refl, "addInt",     "JArray", fn_jarr_addInt);
    bindFunc(refl, "clear",      "JArray", fn_jarr_clear);

    bindFunc(refl, "getStr",     "JMap",   fn_jmap_getStr);
    bindFunc(refl, "getInt",     "JMap",   fn_jmap_getInt);
    bindFunc(refl, "getFlt",     "JMap",   fn_jmap_getFlt);
    bindFunc(refl, "setStr",     "JMap",   fn_jmap_setStr);
    bindFunc(refl, "setInt",     "JMap",   fn_jmap_setInt);
    bindFunc(refl, "setFlt",     "JMap",   fn_jmap_setFlt);

    bindFunc(refl, "getStr",     "JFormDB", fn_jfdb_getStr);
    bindFunc(refl, "getInt",     "JFormDB", fn_jfdb_getInt);
    bindFunc(refl, "setInt",     "JFormDB", fn_jfdb_setInt);

    bindFunc(refl, "addToPool",  "JValue", fn_jval_addToPool);
    bindFunc(refl, "cleanPool",  "JValue", fn_jval_cleanPool);
    bindFunc(refl, "shallowCopy", "JValue", fn_jval_shallowCopy);

    return true;
}

struct JArray {
    static int object() {
        return fn_jarr_object ? fn_jarr_object(g_domain) : 0;
    }
    static int count(int arr) {
        return fn_jarr_count ? fn_jarr_count(g_domain, arr) : 0;
    }
    static int getObj(int arr, int idx, int def = 0) {
        return fn_jarr_getObj ? fn_jarr_getObj(g_domain, arr, idx, def) : def;
    }
    static int getInt(int arr, int idx, int def = 0) {
        return fn_jarr_getInt ? fn_jarr_getInt(g_domain, arr, idx, def) : def;
    }
    static void addObj(int arr, int obj, int idx = -1) {
        if (fn_jarr_addObj) fn_jarr_addObj(g_domain, arr, obj, idx);
    }
    static void addInt(int arr, int val, int idx = -1) {
        if (fn_jarr_addInt) fn_jarr_addInt(g_domain, arr, val, idx);
    }
    static void clear(int arr) {
        if (fn_jarr_clear) fn_jarr_clear(g_domain, arr);
    }
};

struct JMap {
    static std::string getStr(int obj, const char* key, const char* def = "") {
        if (!fn_jmap_getStr) return def;
        return fn_jmap_getStr(g_domain, obj, RE::BSFixedString(key), RE::BSFixedString(def)).c_str();
    }
    static int getInt(int obj, const char* key, int def = 0) {
        return fn_jmap_getInt ? fn_jmap_getInt(g_domain, obj, RE::BSFixedString(key), def) : def;
    }
    static float getFlt(int obj, const char* key, float def = 0.f) {
        return fn_jmap_getFlt ? fn_jmap_getFlt(g_domain, obj, RE::BSFixedString(key), def) : def;
    }
    static void setStr(int obj, const char* key, const char* val) {
        if (fn_jmap_setStr) fn_jmap_setStr(g_domain, obj, RE::BSFixedString(key), RE::BSFixedString(val));
    }
    static void setInt(int obj, const char* key, int val) {
        if (fn_jmap_setInt) fn_jmap_setInt(g_domain, obj, RE::BSFixedString(key), val);
    }
    static void setFlt(int obj, const char* key, float val) {
        if (fn_jmap_setFlt) fn_jmap_setFlt(g_domain, obj, RE::BSFixedString(key), val);
    }
    // JContainers setters return void, so verify the observable postcondition
    // with a default value that cannot be mistaken for the requested value.
    static bool setIntAndVerify(int obj, const char* key, int val) {
        if (obj == 0 || !fn_jmap_setInt || !fn_jmap_getInt) return false;
        fn_jmap_setInt(g_domain, obj, RE::BSFixedString(key), val);
        constexpr int missing = std::numeric_limits<int>::min();
        return fn_jmap_getInt(g_domain, obj, RE::BSFixedString(key), missing) == val;
    }
    static bool setFltAndVerify(int obj, const char* key, float val) {
        if (obj == 0 || !fn_jmap_setFlt || !fn_jmap_getFlt) return false;
        fn_jmap_setFlt(g_domain, obj, RE::BSFixedString(key), val);
        const float missing = std::numeric_limits<float>::quiet_NaN();
        return fn_jmap_getFlt(g_domain, obj, RE::BSFixedString(key), missing) == val;
    }
};

struct JFormDB {
    static std::string getStr(RE::TESForm* form, const char* path, const char* def = "") {
        if (!fn_jfdb_getStr || !form) return def;
        return fn_jfdb_getStr(g_domain, form, RE::BSFixedString(path), RE::BSFixedString(def)).c_str();
    }
    static void setInt(RE::TESForm* form, const char* path, int val) {
        if (fn_jfdb_setInt && form) fn_jfdb_setInt(g_domain, form, RE::BSFixedString(path), val);
    }
    static int getInt(RE::TESForm* form, const char* path, int def = 0) {
        return fn_jfdb_getInt && form
            ? fn_jfdb_getInt(g_domain, form, RE::BSFixedString(path), def)
            : def;
    }
    static bool setIntAndVerify(RE::TESForm* form, const char* path, int val) {
        if (!form || !fn_jfdb_setInt || !fn_jfdb_getInt) return false;
        fn_jfdb_setInt(g_domain, form, RE::BSFixedString(path), val);
        constexpr int missing = std::numeric_limits<int>::min();
        return fn_jfdb_getInt(g_domain, form, RE::BSFixedString(path), missing) == val;
    }
};

struct JValue {
    static int shallowCopy(int obj) {
        if (obj == 0 || !fn_jval_shallowCopy) return 0;
        const int copy = fn_jval_shallowCopy(g_domain, obj);
        return copy != obj ? copy : 0;
    }
    static int addToPool(int obj, const char* pool) {
        return fn_jval_addToPool ? fn_jval_addToPool(g_domain, obj, RE::BSFixedString(pool)) : obj;
    }
    static void cleanPool(const char* pool) {
        if (fn_jval_cleanPool) fn_jval_cleanPool(g_domain, RE::BSFixedString(pool));
    }
};

inline bool isReady() { return g_domain != nullptr; }

}  // namespace jcmini
