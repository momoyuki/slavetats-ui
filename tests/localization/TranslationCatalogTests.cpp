#include "localization/TranslationCatalog.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace stui::localization;
void expect(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    const auto dir = std::filesystem::temp_directory_path() / "stui-translation-tests";
    std::filesystem::create_directories(dir);
    try {
        std::ofstream(dir / "fr.tsv", std::ios::binary) << "# language-name: French\nSave\tEnregistrer\n";
        TranslationCatalog catalog;
        expect(std::string(catalog.text(Language::japanese, "Save")) == "Save", "missing file falls back");
        std::ofstream(dir / "ja.tsv", std::ios::binary)
            << "Save\t保存\nClose\t閉じる\nSlot %d\tスロット %d\n"
               "Unsafe %s\t危険 %d\nInjected\t悪用###id\nBlank\t\n"
               "Duplicate\t一\nDuplicate\t二\n"
               "Target: \t対象: \nLine\\nTwo\t行\\n二\n"
               "Suffix\\s\tFin\\s\n"
               "Bad {}\t不正 {\nPositional {}\t不正 {0}\n"
               "BadRowWithoutTab\n";
        expect(!catalog.load(dir), "partial/missing dictionaries reported");
        expect(catalog.contains("fr"), "new locale discovered without code changes");
        expect(std::string(catalog.text("fr", "Save")) == "Enregistrer", "external French translation loaded");
        expect(catalog.widget("fr", "Save##one") == "Enregistrer###Save##one", "external locale keeps widget ID");
        expect(catalog.languages().front().code == "en", "English recovery option remains first");
        expect(std::string(catalog.text(Language::japanese, "Save")) == "保存", "Japanese UTF-8 loaded");
        expect(std::string(catalog.text(Language::japanese, "Unknown")) == "Unknown", "missing key falls back");
        expect(std::string(catalog.text(Language::japanese, "Unsafe %s")) == "Unsafe %s", "printf types protected");
        expect(std::string(catalog.text(Language::japanese, "Injected")) == "Injected", "widget delimiter rejected");
        expect(std::string(catalog.text(Language::japanese, "Blank")) == "Blank", "empty translation falls back");
        expect(std::string(catalog.text(Language::japanese, "Duplicate")) == "一", "first duplicate wins deterministically");
        expect(std::string(catalog.text(Language::japanese, "Target: ")) == "対象: ", "trailing spaces preserved");
        expect(std::string(catalog.text(Language::japanese, "Suffix ")) == "Fin ", "escaped trailing spaces preserved");
        expect(std::string(catalog.text(Language::japanese, "Line\nTwo")) == "行\n二", "escaped newlines decoded");
        expect(std::string(catalog.text(Language::japanese, "Bad {}")) == "Bad {}", "broken format rejected");
        expect(std::string(catalog.text(Language::japanese, "Positional {}")) == "Positional {}", "format argument order protected");
        expect(catalog.widget(Language::english, "Save##one") == "Save###Save##one", "English stable widget id");
        expect(catalog.widget(Language::japanese, "Save##one") == "保存###Save##one", "localized widget keeps original id");
        expect(catalog.widget(Language::japanese, "動的###stable") == "動的###stable", "explicit ID preserved");
        expect(catalog.widget(Language::japanese, "##hidden") == "##hidden", "hidden labels untouched");
        expect(catalog.widget(Language::japanese, "Save##one") != catalog.widget(Language::japanese, "Save##two"), "distinct widgets remain distinct");
        expect(parseLanguage("zh-CN") == Language::chinese, "Chinese locale");
        expect(parseLanguage("ko") == Language::korean, "Korean locale");
        expect(parseLanguage("ja") == Language::japanese, "Japanese locale");
        expect(!parseLanguage("../../ja"), "unknown locale rejected");
        if (argc > 1) {
            TranslationCatalog shipped;
            expect(shipped.load(argv[1]), "all shipped catalogs validate");
            for (auto language : {Language::chinese, Language::korean, Language::japanese}) {
                expect(std::string(shipped.text(language, "Save")) != "Save", "shipped Save translated");
                expect(std::string(shipped.text(language, "Open Tattoo Browser")) != "Open Tattoo Browser", "launcher translated");
            }
        }
        std::filesystem::remove_all(dir);
        std::cout << "PASS catalog fallback, validation, Unicode, locales and stable widget IDs\n";
    } catch (const std::exception& e) {
        std::filesystem::remove_all(dir);
        std::cerr << "FAIL " << e.what() << '\n'; return 1;
    }
}
