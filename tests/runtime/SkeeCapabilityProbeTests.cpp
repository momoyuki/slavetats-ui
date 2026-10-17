#include "runtime/SkeeCapabilityProbe.h"
#include <iostream>
#include <stdexcept>
using namespace stui::runtime::skee;
struct Plugin final: PluginInterface {
 std::uint32_t version{2}; bool reverted{};
 std::uint32_t GetVersion() override{return version;}
 void Revert() override{reverted=true;}
};
struct Map final: InterfaceMap {
 Plugin plugin;bool present{true};
 PluginInterface* QueryInterface(const char* name) override {return present&&std::string_view(name)=="Override"?&plugin:nullptr;}
 bool AddInterface(const char*,PluginInterface*) override{throw std::runtime_error("must not register");}
 PluginInterface* RemoveInterface(const char*) override{throw std::runtime_error("must not remove");}
};
int main(){try{
 Map map;
 auto dispatch=[&](InterfaceExchange& exchange){exchange.interfaceMap=&map;return true;};
 if(probeOverrideCapability(dispatch)!=OverrideCapability::version2Detected)throw std::runtime_error("supported version");
 map.plugin.version=3;
 if(probeOverrideCapability(dispatch)!=OverrideCapability::unsupportedVersion)throw std::runtime_error("future ABI rejected");
 map.present=false;
 if(probeOverrideCapability(dispatch)!=OverrideCapability::interfaceUnavailable)throw std::runtime_error("missing interface");
 if(probeOverrideCapability([](InterfaceExchange&){return true;})!=OverrideCapability::mapUnavailable)throw std::runtime_error("missing map");
 if(probeOverrideCapability([](InterfaceExchange&){return false;})!=OverrideCapability::dispatchFailed)throw std::runtime_error("failed dispatch");
 if(map.plugin.reverted)throw std::runtime_error("probe mutated runtime");
 std::cout<<"PASS read-only SKEE capability probe\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
