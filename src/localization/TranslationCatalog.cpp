#include "localization/TranslationCatalog.h"

#include <cctype>
#include <fstream>
#include <vector>
#include <algorithm>
#include <mutex>

namespace stui::localization {
namespace {
std::string g_language{"en"};
std::mutex g_languageMutex;
constexpr std::array<const char*, 4> kCodes{"en", "zh-CN", "ko", "ja"};

// Keep the complete printf/format token sequence, including widths and escaped
// delimiters. Translations cannot add conversions, change types or reorder args.
std::optional<std::vector<std::string>> formatTokens(std::string_view text) {
    std::vector<std::string> result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto begin = i;
        if (text[i] == '%') {
            ++i;
            if (i >= text.size()) return std::nullopt;
            if (text[i] != '%') {
                while (i < text.size() && std::string_view("-+ #0.123456789*hljztL").find(text[i]) != std::string_view::npos) ++i;
                if (i >= text.size() || std::string_view("diuoxXfFeEgGaAcsp").find(text[i]) == std::string_view::npos) return std::nullopt;
            }
            result.emplace_back(text.substr(begin, i - begin + 1));
        } else if (text[i] == '{') {
            if (i + 1 < text.size() && text[i + 1] == '{') ++i;
            else {
                i = text.find('}', i + 1);
                if (i == std::string_view::npos) return std::nullopt;
            }
            result.emplace_back(text.substr(begin, i - begin + 1));
        } else if (text[i] == '}') {
            if (i + 1 >= text.size() || text[i + 1] != '}') return std::nullopt;
            result.emplace_back("}}"); ++i;
        }
    }
    return result;
}

bool validUtf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80) { if (lead == 0 || (lead < 0x20 && lead != '\n' && lead != '\t')) return false; continue; }
        unsigned int value{}; unsigned int count{}; unsigned int minimum{};
        if (lead >= 0xC2 && lead <= 0xDF) { value = lead & 0x1F; count = 1; minimum = 0x80; }
        else if (lead >= 0xE0 && lead <= 0xEF) { value = lead & 0x0F; count = 2; minimum = 0x800; }
        else if (lead >= 0xF0 && lead <= 0xF4) { value = lead & 7; count = 3; minimum = 0x10000; }
        else return false;
        if (i + count > text.size()) return false;
        while (count--) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xC0) != 0x80) return false;
            value = (value << 6) | (next & 0x3F);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
    }
    return true;
}
std::optional<std::string> decode(std::string_view value) {
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\') result += value[i];
        else {
            if (++i == value.size()) return std::nullopt;
            if (value[i] == 'n') result += '\n';
            else if (value[i] == 't') result += '\t';
            else if (value[i] == 's') result += ' ';
            else if (value[i] == '\\') result += '\\';
            else return std::nullopt;
        }
    }
    if (!validUtf8(result)) return std::nullopt;
    return result;
}
}  // namespace

std::string_view languageCode(Language language) noexcept {
    const auto index = static_cast<std::size_t>(language);
    return index < kCodes.size() ? kCodes[index] : kCodes[0];
}
const char* languageName(Language language) noexcept {
    // ASCII labels keep the recovery path readable even with missing CJK fonts.
    constexpr std::array names{"English", "Chinese (Simplified)", "Korean", "Japanese"};
    const auto index = static_cast<std::size_t>(language);
    return index < names.size() ? names[index] : names[0];
}
std::optional<Language> parseLanguage(std::string_view code) noexcept {
    for (std::size_t i = 0; i < kCodes.size(); ++i) if (code == kCodes[i]) return static_cast<Language>(i);
    return std::nullopt;
}
bool TranslationCatalog::load(const std::filesystem::path& directory) {
    bool valid = true;
    m_dictionaries.clear();
    m_languages = {{"en", "English"}};
    std::error_code error;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && it->path().extension() == ".tsv") files.push_back(it->path());
    }
    if (error) valid = false;
    std::ranges::sort(files);
    for (const auto& file : files) {
        const auto code = file.stem().string();
        const auto isLetter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
        if (code.empty() || code.size() > 32 || !isLetter(code.front()) ||
            !std::ranges::all_of(code, [&](char c) { return isLetter(c) || (c >= '0' && c <= '9') || c == '-' || c == '_'; })) { valid = false; continue; }
        const auto size = std::filesystem::file_size(file, error);
        if (error || size > 512 * 1024) { valid = false; error.clear(); continue; }
        std::ifstream input(file, std::ios::binary);
        if (!input) { valid = false; continue; }
        auto& dictionary = m_dictionaries[code];
        std::string name = code;
        if (const auto known = parseLanguage(code)) name = languageName(*known);
        std::string line;
        std::size_t bytes = 0;
        while (std::getline(input, line)) {
            bytes += line.size();
            if (bytes > 512 * 1024 || line.size() > 8192) { valid = false; break; }
            if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            constexpr std::string_view namePrefix = "# language-name: ";
            if (line.starts_with(namePrefix)) {
                auto value = decode(std::string_view(line).substr(namePrefix.size()));
                if (value && !value->empty() && value->size() <= 128 && value->find_first_of("\n\t") == std::string::npos && value->find("##") == std::string::npos) name = std::move(*value);
                else valid = false;
                continue;
            }
            if (line.empty() || line.starts_with('#')) continue;
            const auto separator = line.find('\t');
            if (separator == std::string::npos || line.find('\t', separator + 1) != std::string::npos) { valid = false; continue; }
            auto source = decode(std::string_view(line).substr(0, separator));
            auto translation = decode(std::string_view(line).substr(separator + 1));
            if (!source || !translation || source->empty() || translation->empty() ||
                source->find("##") != std::string::npos || translation->find("##") != std::string::npos) { valid = false; continue; }
            const auto sourceTokens = formatTokens(*source);
            const auto translatedTokens = formatTokens(*translation);
            if (!sourceTokens || !translatedTokens || *sourceTokens != *translatedTokens) { valid = false; continue; }
            if (!dictionary.emplace(std::move(*source), std::move(*translation)).second) valid = false;
        }
        if (input.bad()) valid = false;
        if (code != "en") m_languages.push_back({code, std::move(name)});
    }
    return valid;
}
bool TranslationCatalog::contains(std::string_view code) const noexcept {
    return code == "en" || m_dictionaries.contains(code);
}
const char* TranslationCatalog::text(std::string_view code, const char* source) const noexcept {
    if (!source) return "";
    if (code == "en") return source;
    const auto language = m_dictionaries.find(code);
    if (language == m_dictionaries.end()) return source;
    const auto found = language->second.find(source);
    return found == language->second.end() ? source : found->second.c_str();
}
std::string TranslationCatalog::widget(std::string_view code, std::string_view source) const {
    if (source.starts_with("##")) return std::string(source);
    const auto end = source.find("##");
    const auto visible = std::string(source.substr(0, end));
    // Use the original source label as ID for every locale, including English.
    const auto explicitId = source.find("###");
    const auto id = explicitId == std::string_view::npos ? source : source.substr(explicitId + 3);
    return std::string(text(code, visible.c_str())) + "###" + std::string(id);
}
TranslationCatalog& catalog() noexcept { static TranslationCatalog instance; return instance; }
void selectLanguage(std::string_view code) {
    const std::scoped_lock lock(g_languageMutex);
    g_language = catalog().contains(code) ? code : "en";
}
void selectLanguage(Language language) { selectLanguage(languageCode(language)); }
std::string activeLanguage() { const std::scoped_lock lock(g_languageMutex); return g_language; }
const char* tr(const char* source) noexcept {
    const std::scoped_lock lock(g_languageMutex);
    return catalog().text(g_language, source);
}
std::string widgetLabel(std::string_view source) {
    const std::scoped_lock lock(g_languageMutex);
    return catalog().widget(g_language, source);
}
}  // namespace stui::localization
