#include "native/FlickLauncher.h"

#include <utility>

namespace stui::native {

FlickLauncher::FlickLauncher(
    FlickPort& port, Scheduler scheduler, OpenBrowser openBrowser)
    : port_(port),
      scheduler_(std::move(scheduler)),
      openBrowser_(std::move(openBrowser)) {}

bool FlickLauncher::registerTool() noexcept {
    if (registered_) return true;
    if (!port_.connect()) return false;
    if (!port_.registerTool(*this)) return false;
    registered_ = true;
    return true;
}

bool FlickLauncher::requestOpen() noexcept {
    if (!registered_ || !scheduler_ || !openBrowser_) return false;
    bool expected = false;
    if (!openPending_.compare_exchange_strong(expected, true)) return false;

    try {
        port_.closeMenu();
        if (!scheduler_([this] {
                try {
                    openBrowser_();
                } catch (...) {
                    // No exception may escape an SKSE task callback.
                }
                openPending_.store(false);
            })) {
            openPending_.store(false);
            return false;
        }
    } catch (...) {
        openPending_.store(false);
        return false;
    }
    return true;
}

bool FlickLauncher::openPending() const noexcept {
    return openPending_.load();
}

}  // namespace stui::native
