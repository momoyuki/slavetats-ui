#pragma once

// Presentation-only adapters. Unformatted text is already rendered data;
// translate fixed captions explicitly before passing them to TextUnformatted.
// Pack/user-provided captions and texture paths are data, not translation keys.
#include "localization/TranslationCatalog.h"
#include "SKSEMenuFramework.h"
#include <utility>

namespace stui::native::localizedUi {
using ImGuiMCP::TextUnformatted;

inline bool Button(const char* source, const ImGuiMCP::ImVec2& size = {0.0F, 0.0F}) {
    const auto label = localization::widgetLabel(source);
    return ImGuiMCP::Button(label.c_str(), size);
}
#define STUI_LOCALIZED_WIDGET(Name) \
    template <class... Args> decltype(auto) Name(const char* source, Args&&... args) { \
        const auto label = localization::widgetLabel(source); \
        return ImGuiMCP::Name(label.c_str(), std::forward<Args>(args)...); \
    }
STUI_LOCALIZED_WIDGET(Begin)
STUI_LOCALIZED_WIDGET(BeginCombo)
STUI_LOCALIZED_WIDGET(BeginTabItem)
STUI_LOCALIZED_WIDGET(TabItemButton)
STUI_LOCALIZED_WIDGET(Selectable)
STUI_LOCALIZED_WIDGET(Checkbox)
STUI_LOCALIZED_WIDGET(ColorEdit3)
STUI_LOCALIZED_WIDGET(InputFloat)
STUI_LOCALIZED_WIDGET(InputText)
STUI_LOCALIZED_WIDGET(SliderFloat)
STUI_LOCALIZED_WIDGET(Combo)
STUI_LOCALIZED_WIDGET(IsPopupOpen)
STUI_LOCALIZED_WIDGET(OpenPopup)
STUI_LOCALIZED_WIDGET(BeginPopup)
STUI_LOCALIZED_WIDGET(BeginPopupModal)
#undef STUI_LOCALIZED_WIDGET
#define STUI_LOCALIZED_TEXT(Name) \
    template <class... Args> decltype(auto) Name(const char* source, Args&&... args) { \
        return ImGuiMCP::Name(localization::tr(source), std::forward<Args>(args)...); \
    }
STUI_LOCALIZED_TEXT(Text)
STUI_LOCALIZED_TEXT(TextWrapped)
STUI_LOCALIZED_TEXT(SetTooltip)
STUI_LOCALIZED_TEXT(SeparatorText)
STUI_LOCALIZED_TEXT(CalcTextSize)
#undef STUI_LOCALIZED_TEXT
}  // namespace stui::native::localizedUi
