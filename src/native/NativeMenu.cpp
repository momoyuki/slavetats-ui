#include "native/NativeMenu.h"

#include <utility>

namespace stui::native {
namespace {

NativeMenu* g_activeMenu{};

}  // namespace

std::string_view registrationErrorName(MenuRegistrationError error) noexcept {
    switch (error) {
    case MenuRegistrationError::unavailable:
        return "unavailable";
    case MenuRegistrationError::unsupportedVersion:
        return "unsupported version";
    case MenuRegistrationError::missingExport:
        return "missing export";
    case MenuRegistrationError::windowCreationFailed:
        return "window creation failed";
    case MenuRegistrationError::callbackFailed:
        return "callback failed";
    }
    return "unknown";
}

NativeMenu::NativeMenu(RenderFunction render, LaunchFunction launch)
    : render_(std::move(render)), launch_(std::move(launch)) {}

NativeMenu::~NativeMenu() {
    if (g_activeMenu == this) {
        g_activeMenu = nullptr;
    }
}

RegistrationResult NativeMenu::registerMenu(MenuFrameworkPort& port) {
    const auto fail = [this](MenuRegistrationError error) -> RegistrationResult {
        lastError_ = error;
        return std::unexpected(error);
    };
    if (registered_) {
        return {};
    }
    if (!port.available()) {
        return fail(MenuRegistrationError::unavailable);
    }
    const float frameworkVersion = port.version();
    if (frameworkVersion < 3.0F || frameworkVersion >= 4.0F) {
        return fail(MenuRegistrationError::unsupportedVersion);
    }
    if (auto result = port.setSection("SlaveTatsUI"); !result) {
        return fail(result.error());
    }
    auto window = port.addWindow(&NativeMenu::renderCallback, true);
    if (!window) {
        return fail(window.error());
    }
    if (*window == 0) {
        return fail(MenuRegistrationError::windowCreationFailed);
    }
    if (auto result = port.addSectionItem("Tattoo Browser", &NativeMenu::sectionCallback);
        !result) {
        return fail(result.error());
    }

    port_ = &port;
    window_ = *window;
    registered_ = true;
    lastError_.reset();
    g_activeMenu = this;
    return {};
}

std::optional<MenuRegistrationError> NativeMenu::lastError() const noexcept {
    return lastError_;
}

void NativeMenu::toggle() noexcept {
    if (isOpen()) {
        if (closeRequestCallback_) {
            try {
                if (closeRequestCallback_()) {
                    return;
                }
            } catch (...) {
                lastError_ = MenuRegistrationError::callbackFailed;
                return;
            }
        }
        close();
        return;
    }
    if (registered_) {
        open();
    }
}

bool NativeMenu::handleFrameworkHotkey(
    bool isKeyboard, bool isDown, bool matchesBinding) noexcept {
    if (!isKeyboard || !isDown || !matchesBinding) {
        return false;
    }
    toggle();
    return true;
}

void NativeMenu::open() noexcept {
    if (port_ && window_ != 0) {
        port_->setWindowOpen(window_, true);
        if (openCallback_) {
            try {
                openCallback_();
            } catch (...) {
                lastError_ = MenuRegistrationError::callbackFailed;
            }
        }
    }
}

void NativeMenu::setOpenCallback(OpenFunction callback) {
    openCallback_ = std::move(callback);
}

void NativeMenu::setCloseRequestCallback(CloseRequestFunction callback) {
    closeRequestCallback_ = std::move(callback);
}

void NativeMenu::close() noexcept {
    if (port_ && window_ != 0) {
        port_->setWindowOpen(window_, false);
    }
}

bool NativeMenu::isOpen() const noexcept {
    return port_ && window_ != 0 && port_->isWindowOpen(window_);
}

bool NativeMenu::isRegistered() const noexcept {
    return registered_;
}

void NativeMenu::sectionCallback() noexcept {
    if (!g_activeMenu || !g_activeMenu->launch_) {
        return;
    }
    try {
        if (g_activeMenu->launch_()) {
            g_activeMenu->open();
        }
    } catch (...) {
        g_activeMenu->lastError_ = MenuRegistrationError::callbackFailed;
    }
}

void NativeMenu::renderCallback() noexcept {
    if (!g_activeMenu || !g_activeMenu->render_) {
        return;
    }
    try {
        g_activeMenu->render_(*g_activeMenu);
    } catch (...) {
        g_activeMenu->lastError_ = MenuRegistrationError::callbackFailed;
    }
}

}  // namespace stui::native
