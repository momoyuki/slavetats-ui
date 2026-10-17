#include "runtime/PrepareTattooApply.h"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
using namespace stui;
int main() {
    try {
        using Value=std::variant<int,float,std::string>;
        std::map<int,std::map<std::string,Value>> maps;
        maps[1]={{"color",1},{"glow",2},{"glossiness",3.0F},{"bump",std::string("pack_n.dds")}};
        const auto original=maps[1];
        runtime::TattooApplyTemplateBindings b;
        b.copy=[&](int source) { maps[2]=maps[source]; return 2; };
        b.setInt=[&](int h,const char* k,int v) { maps[h][k]=v; return true; };
        b.setFloat=[&](int h,const char* k,float v) { maps[h][k]=v; return true; };
        b.setString=[&](int h,const char* k,const std::string& v) { maps[h][k]=v; return true; };
        core::ApplyTattooRequest request;
        request.color=0x123456;request.alpha=0.75F;
        request.appearance=core::ApplyTattooAppearanceOverrides{.glow=0,.glossiness=0.0F,.emissiveMult=2.5F};
        auto copy=runtime::prepareTattooApplyTemplate(1,request,b);
        if (!copy || *copy!=2 || maps[1]!=original) throw std::runtime_error("shared template was changed");
        if (std::get<int>(maps[2]["glow"])!=0 || std::get<float>(maps[2]["emissiveMult"])!=2.5F ||
            std::get<float>(maps[2]["invertedAlpha"])!=0.25F || maps[2]["bump"]!=original.at("bump"))
            throw std::runtime_error("effective values/defaults were lost");
        b.copy=[](int source) {return source;};
        if (runtime::prepareTattooApplyTemplate(1,request,b)) throw std::runtime_error("alias accepted");
        b.copy=[](int) {return 0;};
        if (runtime::prepareTattooApplyTemplate(1,request,b)) throw std::runtime_error("missing copy accepted");
        b.copy=[](int) {return 2;};
        request.appearance->emissiveMult=0.0F;
        request.appearance->glowTexture="";
        request.appearance->bump="";
        if (!runtime::prepareTattooApplyTemplate(1,request,b) ||
            std::get<float>(maps[2]["emissiveMult"])!=0.0F ||
            !std::get<std::string>(maps[2]["bump"]).empty())
            throw std::runtime_error("explicit zero/empty overrides were lost");
        auto goodString=b.setString;
        b.setString=[](int,const char*,const std::string&) {return false;};
        if (runtime::prepareTattooApplyTemplate(1,request,b)) throw std::runtime_error("failed string write accepted");
        b.setString=goodString;
        auto goodInt=b.setInt;
        b.setInt=[](int,const char*,int) {return false;};
        if (runtime::prepareTattooApplyTemplate(1,request,b)) throw std::runtime_error("failed integer write accepted");
        b.setInt=goodInt;
        b.setFloat=[](int,const char*,float) {return false;};
        if (runtime::prepareTattooApplyTemplate(1,request,b)) throw std::runtime_error("failed write accepted");
        std::cout << "PASS isolated full appearance apply template\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
