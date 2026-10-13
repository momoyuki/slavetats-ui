#pragma once

#include <atomic>
#include <functional>

namespace stui::native {

class FlickLauncher;

class FlickPort {
public:
    virtual ~FlickPort() = default;
    [[nodiscard]] virtual bool connect() noexcept = 0;
    [[nodiscard]] virtual bool registerTool(FlickLauncher& launcher) noexcept = 0;
    virtual void closeMenu() noexcept = 0;
};

class FlickLauncher {
public:
    using Task = std::function<void()>;
    using Scheduler = std::function<bool(Task)>;
    using OpenBrowser = std::function<void()>;

    FlickLauncher(FlickPort& port, Scheduler scheduler, OpenBrowser openBrowser);

    [[nodiscard]] bool registerTool() noexcept;
    [[nodiscard]] bool requestOpen() noexcept;
    [[nodiscard]] bool openPending() const noexcept;

private:
    FlickPort& port_;
    Scheduler scheduler_;
    OpenBrowser openBrowser_;
    std::atomic_bool openPending_{false};
    bool registered_{};
};

}  // namespace stui::native
