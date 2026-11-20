#pragma once
#include "localization/TranslationCatalog.h"
#include "runtime/PluginConfigFile.h"
#include <memory>

namespace stui::runtime {
class LanguageSetting {
public:
    explicit LanguageSetting(std::shared_ptr<PluginConfigFile> config);
    [[nodiscard]] bool load();
    [[nodiscard]] bool select(std::string_view code);
    [[nodiscard]] bool select(localization::Language language) { return select(localization::languageCode(language)); }
    [[nodiscard]] const std::string& language() const noexcept { return m_language; }
private:
    std::shared_ptr<PluginConfigFile> m_config;
    std::string m_language{"en"};
};
}  // namespace stui::runtime
