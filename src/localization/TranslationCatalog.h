#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stui::localization {
enum class Language { english, chinese, korean, japanese };
[[nodiscard]] std::string_view languageCode(Language language) noexcept;
[[nodiscard]] const char* languageName(Language language) noexcept;
[[nodiscard]] std::optional<Language> parseLanguage(std::string_view code) noexcept;

// Load once before rendering. Dictionaries are immutable afterwards, so returned
// text pointers survive language changes. Reloading while rendering is unsupported.
struct LanguageOption { std::string code; std::string name; };

class TranslationCatalog {
public:
    [[nodiscard]] bool load(const std::filesystem::path& directory);
    [[nodiscard]] const std::vector<LanguageOption>& languages() const noexcept { return m_languages; }
    [[nodiscard]] bool contains(std::string_view code) const noexcept;
    [[nodiscard]] const char* text(std::string_view code, const char* source) const noexcept;
    [[nodiscard]] const char* text(Language language, const char* source) const noexcept { return text(languageCode(language), source); }
    [[nodiscard]] std::string widget(std::string_view code, std::string_view source) const;
    [[nodiscard]] std::string widget(Language language, std::string_view source) const { return widget(languageCode(language), source); }
private:
    std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> m_dictionaries;
    std::vector<LanguageOption> m_languages{{"en", "English"}};
};
[[nodiscard]] TranslationCatalog& catalog() noexcept;
void selectLanguage(std::string_view code);
void selectLanguage(Language language);
[[nodiscard]] std::string activeLanguage();
[[nodiscard]] const char* tr(const char* source) noexcept;
[[nodiscard]] std::string widgetLabel(std::string_view source);
}  // namespace stui::localization
