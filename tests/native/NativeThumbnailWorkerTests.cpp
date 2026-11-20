#include "native/NativeThumbnailWorker.h"
#include "textures/ScheduledTextureArchiveReader.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
void expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        for (int i = 0; i < 200; ++i) {
            stui::native::NativeThumbnailWorker idle;
            idle.requestStop();
        }
        std::promise<std::thread::id> ran;
        std::promise<void> gate;
        auto release = gate.get_future().share();
        std::atomic_int queuedRuns{};
        auto worker = std::make_unique<stui::native::NativeThumbnailWorker>();
        worker->submit([&] { ran.set_value(std::this_thread::get_id()); release.wait(); });
        expect(ran.get_future().get() != std::this_thread::get_id(), "CPU work must run off caller thread");
        worker->submit([&] { ++queuedRuns; });
        worker->requestStop();
        auto stopped = std::async(std::launch::async, [&] { worker.reset(); });
        expect(stopped.wait_for(10ms) == std::future_status::timeout, "shutdown must join running task");
        gate.set_value();
        stopped.get();
        expect(queuedRuns == 0, "shutdown must discard queued work");

        std::vector<std::function<void()>> scheduled;
        int reads = 0;
        stui::textures::ScheduledTextureArchiveReader reader(
            [&](auto task) { scheduled.push_back(std::move(task)); },
            [&](std::string_view) -> stui::textures::TextureBytesResult { ++reads; return std::vector<std::uint8_t>{1,2,3}; }, 5ms);
        auto timedOut = reader.read("late.dds");
        expect(!timedOut && timedOut.error() == stui::textures::TextureResolveError::temporarilyUnavailable && scheduled.size() == 1, "archive wait must time out without blocking forever");
        scheduled.front()();
        expect(reads == 0, "expired queued archive task must not read engine resources");

        // In-flight transfer that exceeds the queue budget must still return bytes.
        std::thread engineThread;
        stui::textures::ScheduledTextureArchiveReader inFlight(
            [&](auto task) {
                engineThread = std::thread(std::move(task));
            },
            [](std::string_view) -> stui::textures::TextureBytesResult {
                std::this_thread::sleep_for(40ms);
                return std::vector<std::uint8_t>{7, 8};
            },
            10ms);
        const auto inFlightResult = inFlight.read("slow.dds");
        if (engineThread.joinable()) engineThread.join();
        expect(inFlightResult && (*inFlightResult)[0] == 7 && (*inFlightResult)[1] == 8,
            "started archive reads must complete instead of discarding late bytes");

        stui::textures::ScheduledTextureArchiveReader immediate(
            [](auto task) { task(); },
            [&](std::string_view path) -> stui::textures::TextureBytesResult { expect(path == "ok.dds", "path preserved"); return std::vector<std::uint8_t>{9}; });
        expect(immediate.read("ok.dds")->front() == 9, "scheduled result must return bytes");
        stui::textures::ScheduledTextureArchiveReader broken(
            [](auto) { throw std::runtime_error("scheduler"); },
            [](auto) -> stui::textures::TextureBytesResult { return std::vector<std::uint8_t>{}; });
        expect(!broken.read("bad.dds"), "scheduler exceptions must report read failure");
        std::cout << "PASS worker shutdown, off-thread execution and scheduled archive reads\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
