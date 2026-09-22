#pragma once

#include "native/MenuFrameworkPort.h"

#include <functional>
#include <optional>

namespace stui::native {

class NativeMenu {
public:
    using RenderFunction = std::function<void(NativeMenu&)>;
    using LaunchFunction = std::function<bool()>;
    using OpenFunction = std::function<void()>;
    using CloseRequestFunction = std::function<bool()>;

    explicit NativeMenu(RenderFunction render = {}, LaunchFunction launch = {});
    ~NativeMenu();

    NativeMenu(const NativeMenu&) = delete;
    NativeMenu& operator=(const NativeMenu&) = delete;

    [[nodiscard]] RegistrationResult registerMenu(MenuFrameworkPort& port);
    void toggle() noexcept;
    [[nodiscard]] bool handleFrameworkHotkey(
        bool isKeyboard, bool isDown, bool matchesBinding) noexcept;
    void open() noexcept;
    void setOpenCallback(OpenFunction callback);
    void setCloseRequestCallback(CloseRequestFunction callback);
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] bool isRegistered() const noexcept;
    [[nodiscard]] std::optional<MenuRegistrationError> lastError() const noexcept;

private:
    static void sectionCallback() noexcept;
    static void renderCallback() noexcept;

    MenuFrameworkPort* port_{};
    MenuWindow window_{};
    RenderFunction render_;
    LaunchFunction launch_;
    OpenFunction openCallback_;
    CloseRequestFunction closeRequestCallback_;
    std::optional<MenuRegistrationError> lastError_;
    bool registered_{};
};

}  // namespace stui::native
