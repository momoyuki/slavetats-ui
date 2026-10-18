#pragma once
#include <cstdint>
#include <functional>
#include <string_view>
namespace stui::runtime::skee {
// Independently declared ABI prefix from the public SKEE interface contract.
// Provenance: expired6978/SKSE64Plugins @ 9ebcb733, skee64/IPluginInterface.h.
// Only QueryInterface/GetVersion are called; no override mutation interface is bound.
class PluginInterface {
public:
    virtual ~PluginInterface() = default;
    virtual std::uint32_t GetVersion() = 0;
    virtual void Revert() = 0;
};
class InterfaceMap {
public:
    virtual PluginInterface* QueryInterface(const char*) = 0;
    virtual bool AddInterface(const char*,PluginInterface*) = 0;
    virtual PluginInterface* RemoveInterface(const char*) = 0;
};
class GetVariant {
public:
    virtual void Int(std::int32_t) = 0;
    virtual void Float(float) = 0;
    virtual void String(const char*) = 0;
    virtual void Bool(bool) = 0;
    virtual void TextureSet(const void*) = 0;
};
class SetVariant {
public:
    enum class Type { None, Int, Float, String, Bool, TextureSet };
    virtual Type GetType() { return Type::None; }
    virtual std::int32_t Int() { return 0; }
    virtual float Float() { return 0.0F; }
    virtual const char* String() { return nullptr; }
    virtual bool Bool() { return false; }
    virtual void* TextureSet() { return nullptr; }
};
// Independently declared complete v2 layout. Pointer parameters are opaque here;
// their widths and slot order match the public SKEE interface.
class OverrideInterface : public PluginInterface {
public:
    virtual bool HasArmorAddonNode(void*,bool,void*,void*,const char*,bool)=0;
    virtual bool HasArmorOverride(void*,bool,void*,void*,const char*,std::uint16_t,std::uint8_t)=0;
    virtual void AddArmorOverride(void*,bool,void*,void*,const char*,std::uint16_t,std::uint8_t,SetVariant&)=0;
    virtual bool GetArmorOverride(void*,bool,void*,void*,const char*,std::uint16_t,std::uint8_t,GetVariant&)=0;
    virtual void RemoveArmorOverride(void*,bool,void*,void*,const char*,std::uint16_t,std::uint8_t)=0;
    virtual void SetArmorProperties(void*,bool)=0;
    virtual void SetArmorProperty(void*,bool,void*,void*,const char*,std::uint16_t,std::uint8_t,SetVariant&,bool)=0;
    virtual bool GetArmorProperty(void*,bool,void*,void*,const char*,std::uint16_t,std::uint8_t,GetVariant&)=0;
    virtual void ApplyArmorOverrides(void*,void*,void*,void*,bool)=0;
    virtual void RemoveAllArmorOverrides()=0;
    virtual void RemoveAllArmorOverridesByReference(void*)=0;
    virtual void RemoveAllArmorOverridesByArmor(void*,bool,void*)=0;
    virtual void RemoveAllArmorOverridesByAddon(void*,bool,void*,void*)=0;
    virtual void RemoveAllArmorOverridesByNode(void*,bool,void*,void*,const char*)=0;
    virtual bool HasNodeOverride(void*,bool,const char*,std::uint16_t,std::uint8_t)=0;
    virtual void AddNodeOverride(void*,bool,const char*,std::uint16_t,std::uint8_t,SetVariant&)=0;
    virtual bool GetNodeOverride(void*,bool,const char*,std::uint16_t,std::uint8_t,GetVariant&)=0;
    virtual void RemoveNodeOverride(void*,bool,const char*,std::uint16_t,std::uint8_t)=0;
    virtual void SetNodeProperties(void*,bool)=0;
    virtual void SetNodeProperty(void*,bool,const char*,std::uint16_t,std::uint8_t,SetVariant&,bool)=0;
    virtual bool GetNodeProperty(void*,bool,const char*,std::uint16_t,std::uint8_t,GetVariant&)=0;
    virtual void ApplyNodeOverrides(void*,void*,bool)=0;
    virtual void RemoveAllNodeOverrides()=0;
    virtual void RemoveAllNodeOverridesByReference(void*)=0;
    virtual void RemoveAllNodeOverridesByNode(void*,bool,const char*)=0;
    virtual bool HasSkinOverride(void*,bool,bool,std::uint32_t,std::uint16_t,std::uint8_t)=0;
    virtual void AddSkinOverride(void*,bool,bool,std::uint32_t,std::uint16_t,std::uint8_t,SetVariant&)=0;
    virtual bool GetSkinOverride(void*,bool,bool,std::uint32_t,std::uint16_t,std::uint8_t,GetVariant&)=0;
    virtual void RemoveSkinOverride(void*,bool,bool,std::uint32_t,std::uint16_t,std::uint8_t)=0;
    virtual void SetSkinProperties(void*,bool)=0;
    virtual void SetSkinProperty(void*,bool,std::uint32_t,std::uint16_t,std::uint8_t,SetVariant&,bool)=0;
    virtual bool GetSkinProperty(void*,bool,std::uint32_t,std::uint16_t,std::uint8_t,GetVariant&)=0;
    virtual void ApplySkinOverrides(void*,bool,void*,void*,std::uint32_t,void*,bool)=0;
    virtual void RemoveAllSkinOverrides()=0;
    virtual void RemoveAllSkinOverridesByReference(void*)=0;
    virtual void RemoveAllSkinOverridesBySlot(void*,bool,bool,std::uint32_t)=0;
};
struct InterfaceExchange {InterfaceMap* interfaceMap{};};
inline constexpr std::uint32_t exchangeMessage=0x9E3779B9;
enum class OverrideCapability {
    dispatchFailed,mapUnavailable,interfaceUnavailable,unsupportedVersion,version2Detected
};
inline OverrideCapability probeOverrideCapability(
    const std::function<bool(InterfaceExchange&)>& dispatch) {
    InterfaceExchange exchange;
    if(!dispatch || !dispatch(exchange))return OverrideCapability::dispatchFailed;
    if(!exchange.interfaceMap)return OverrideCapability::mapUnavailable;
    auto* plugin=exchange.interfaceMap->QueryInterface("Override");
    if(!plugin)return OverrideCapability::interfaceUnavailable;
    return plugin->GetVersion()==2 ? OverrideCapability::version2Detected :
        OverrideCapability::unsupportedVersion;
}
inline OverrideInterface* queryOverrideInterface(InterfaceMap* map) {
    if (!map) return nullptr;
    auto* plugin=map->QueryInterface("Override");
    if (!plugin || plugin->GetVersion()!=2) return nullptr;
    return static_cast<OverrideInterface*>(plugin);
}
} // namespace stui::runtime::skee
