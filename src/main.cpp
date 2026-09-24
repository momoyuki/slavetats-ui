#include "pch.h"
#include "SlaveTatsNG_Interface.h"
#include "repository/TattooCatalogStore.h"
#include "native/NativeMenu.h"
#include "native/NativeCatalogBrowserModel.h"
#include "native/NativeSlotWorkflowModel.h"
#include "native/NativeSlotWorkflowRuntime.h"
#include "native/ActorTargetProvider.h"
#include "native/D3D11NativeThumbnailSource.h"
#include "native/NativeThumbnailRuntime.h"
#include "native/OfficialMenuFrameworkAdapter.h"
#include "runtime/ApplicationRuntime.h"
#include "runtime/HotkeyBinding.h"
#include "runtime/PluginConfigFile.h"
#include "runtime/FavoriteStore.h"
#include "SKSEMenuFramework.h"
#include "textures/ExactStreamReader.h"

#include <array>
#include <chrono>
#include <memory>

using namespace stui;

// ── Config (SlaveTatsUI.json) ─────────────────────────────────────────────────
namespace {

repository::TattooCatalogStore g_tattooCatalogStore;
runtime::ApplicationRuntime g_applicationRuntime;
native::NativeCatalogBrowserModel g_nativeCatalogBrowser(
    [] { return g_tattooCatalogStore.snapshot(); });
native::NativeSlotWorkflowModel g_nativeSlotWorkflow(g_nativeCatalogBrowser);
std::shared_ptr<runtime::PluginConfigFile> g_pluginConfig;
std::unique_ptr<runtime::FavoriteStore> g_favoriteStore;
native::NativeSlotWorkflowRuntime g_nativeSlotWorkflowRuntime(
    g_nativeSlotWorkflow,
    [] { return native::resolveCrosshairActorTarget(); },
    [](std::uint32_t actorFormId, core::TattooArea area) {
        return g_applicationRuntime.service().querySlots(actorFormId, area);
    },
    [](const core::ApplyTattooRequest& request) {
        return g_applicationRuntime.service().applyToSlot(request);
    },
    [](const core::RemoveTattooRequest& request) {
        return g_applicationRuntime.service().removeFromSlot(request);
    },
    [](const core::UpdateTattooAppearanceRequest& request) {
        return g_applicationRuntime.service().updateAppearance(request);
    },
    [](const core::SetTattooLockedRequest& request) {
        return g_applicationRuntime.service().setTattooLocked(request);
    },
    [](native::NativeSlotTask task) {
        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) {
            throw std::runtime_error("SKSE task interface unavailable");
        }
        taskInterface->AddTask(std::move(task));
    },
    [] { return std::chrono::steady_clock::now(); },
    [](const repository::FavoriteIdentity& identity, bool enabled) {
        if (!g_favoriteStore) {
            return runtime::FavoriteResult(std::unexpected(runtime::ConfigError{
                .message = "Favorite storage is unavailable."}));
        }
        return g_favoriteStore->setFavorite(identity, enabled);
    });

std::unique_ptr<native::NativeThumbnailRuntime> makeUnavailableNativeThumbnailRuntime(
    std::filesystem::path textureRoot = {}) {
    auto source = std::make_unique<native::D3D11NativeThumbnailSource>(
        std::move(textureRoot),
        nullptr,
        12,
        std::chrono::minutes(2),
        textures::TextureArchiveReader{});
    return std::make_unique<native::NativeThumbnailRuntime>(
        std::move(source),
        [](native::NativeThumbnailTask) {},
        [] { return textures::TextureCacheClock::now(); });
}

std::unique_ptr<native::NativeThumbnailRuntime> g_nativeThumbnailRuntime =
    makeUnavailableNativeThumbnailRuntime();
bool g_nativeThumbnailRuntimeUnavailableLogged{};
native::NativeMenu* g_nativeMenu{};
std::unique_ptr<runtime::HotkeyBinding> g_hotkeyBinding;
SKSEMenuFramework::Model::InputEvent* g_hotkeyInputEvent{};

}  // namespace

// ── Input handler ─────────────────────────────────────────────────────────────

bool __stdcall onMenuHotkeyInput(RE::InputEvent* event) {
    if (!event || !g_hotkeyBinding || !g_nativeMenu) {
        return false;
    }
    const auto* button = event->AsButtonEvent();
    if (!button) {
        return false;
    }
    return g_nativeMenu->handleFrameworkHotkey(
        event->GetDevice() == RE::INPUT_DEVICE::kKeyboard,
        button->IsDown(),
        g_hotkeyBinding->matches(button->GetIDCode()));
}

// ── SKSE message handlers ─────────────────────────────────────────────────────

static void onSKSEMessage(SKSE::MessagingInterface::Message* msg) {
    switch (msg->type) {
    case SKSE::MessagingInterface::kDataLoaded:
        {
            std::array<wchar_t, 32768> executablePath{};
            const DWORD pathLength = GetModuleFileNameW(
                nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));

            if (pathLength == 0 || pathLength >= executablePath.size()) {
                logger::warn(
                    "SlaveTatsUI: cannot locate Skyrim executable for effective JSON scan (error={})",
                    GetLastError());
            } else try {
                const auto sourceDirectory = std::filesystem::path(
                    executablePath.data(), executablePath.data() + pathLength).parent_path()
                    / L"Data" / repository::kTattooSourceRelativeDirectory;
                auto catalog = g_tattooCatalogStore.refresh(sourceDirectory);

                if (!catalog) {
                    logger::warn(
                        "SlaveTatsUI: effective JSON scan failed for '{}': {}",
                        sourceDirectory.string(),
                        catalog.error().message());
                } else {
                    logger::info(
                        "SlaveTatsUI: loaded {} tattoo definitions from {} sources ({} issues)",
                        (*catalog)->repository.query().totalEntries,
                        (*catalog)->sourceCount,
                        (*catalog)->issues.size());
                    for (const auto& issue : (*catalog)->issues) {
                        if (issue.entryIndex) {
                            logger::warn(
                                "SlaveTatsUI: source '{}' entry {} skipped: {}",
                                issue.sourceId, *issue.entryIndex, issue.message);
                        } else {
                            logger::warn(
                                "SlaveTatsUI: source '{}' skipped: {}",
                                issue.sourceId, issue.message);
                        }
                    }
                }
            } catch (const std::exception& error) {
                logger::warn(
                    "SlaveTatsUI: effective JSON scan aborted without blocking UI: {}",
                    error.what());
            }
        }
        {
            std::array<wchar_t, 32768> executablePath{};
            const DWORD pathLength = GetModuleFileNameW(
                nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
            const auto textureRoot = pathLength == 0 || pathLength >= executablePath.size()
                ? std::filesystem::path{}
                : std::filesystem::path(
                      executablePath.data(), executablePath.data() + pathLength).parent_path() /
                      L"Data" / L"textures" / L"actors" / L"character" / L"slavetats";
            auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
            auto* taskInterface = SKSE::GetTaskInterface();

            if (textureRoot.empty() || !renderer || !renderer->data.forwarder || !taskInterface) {
                g_nativeThumbnailRuntime = makeUnavailableNativeThumbnailRuntime(textureRoot);
                if (!g_nativeThumbnailRuntimeUnavailableLogged) {
                    logger::warn(
                        "SlaveTatsUI: native thumbnail runtime unavailable; rendering placeholders only");
                    g_nativeThumbnailRuntimeUnavailableLogged = true;
                }
            } else {
                auto source = std::make_unique<native::D3D11NativeThumbnailSource>(
                    textureRoot,
                    renderer->data.forwarder,
                    12,
                    std::chrono::minutes(2),
                    [](std::string_view resourcePath) {
                        const std::string path(resourcePath);
                        RE::BSResourceNiBinaryStream stream(path.c_str());
                        if (!stream.good()) {
                            return textures::TextureBytesResult(
                                std::unexpected(textures::TextureResolveError::notFound));
                        }
                        auto bytes = textures::readExactBytes(stream, stream.stream->totalSize);
                        if (!bytes) {
                            return textures::TextureBytesResult(
                                std::unexpected(textures::TextureResolveError::readFailed));
                        }
                        return textures::TextureBytesResult(std::move(*bytes));
                    },
                    [](std::string_view texturePath, native::NativeThumbnailFailureStage stage) {
                        std::string_view stageName = "exception";
                        if (stage == native::NativeThumbnailFailureStage::resolve) {
                            stageName = "resolve";
                        } else if (stage == native::NativeThumbnailFailureStage::upload) {
                            stageName = "upload";
                        }
                        logger::warn(
                            "SlaveTatsUI: native thumbnail failed: path='{}', stage={}",
                            texturePath,
                            stageName);
                    });
                g_nativeThumbnailRuntime = std::make_unique<native::NativeThumbnailRuntime>(
                    std::move(source),
                    [taskInterface](native::NativeThumbnailTask task) {
                        taskInterface->AddTask(std::move(task));
                    },
                    [] { return textures::TextureCacheClock::now(); });
                logger::info(
                    "SlaveTatsUI: native thumbnail runtime initialized (capacity=12, ttl=2m)");
            }
        }
        g_nativeSlotWorkflow.start();
        break;
    }
}

static void onSlaveTatsMessage(SKSE::MessagingInterface::Message* msg) {
    if (msg->type != slavetats::interface::Interface) return;

    // Peek version before the strict check so the UI can surface a named error
    uint32_t gotVersion = (msg->data && msg->dataLen >= 4)
        ? *reinterpret_cast<const uint32_t*>(msg->data)
        : 0u;

    auto* api = slavetats::interface::Addresses::from_void(msg->data);
    if (!api) {
        logger::error("SlaveTatsUI: SlaveTatsNG API version mismatch (got v{}, need v{})",
            gotVersion, static_cast<uint32_t>(slavetats::interface::Addresses::version));
        // Store peeked version so the UI can display a meaningful error on connect
        g_applicationRuntime.noteSlaveTatsVersionMismatch(gotVersion);
        return;
    }
    g_applicationRuntime.bindSlaveTats(api);
}

static void onJContainersMessage(SKSE::MessagingInterface::Message* msg) {
    logger::info("SlaveTatsUI: JContainers message received, type={}", msg->type);

    if (msg->type != jc::message_root_interface) return;

    auto* root = jc::root_interface::from_void(msg->data);
    if (!root) {
        logger::error("SlaveTatsUI: JContainers root_interface version mismatch (got {})",
            msg->data ? static_cast<jc::root_interface*>(msg->data)->current_version : 0u);
        return;
    }
    if (!g_applicationRuntime.bindJContainers(root)) {
        logger::error("SlaveTatsUI: failed to initialize JContainers interface");
    }
}

// ── Plugin entry ──────────────────────────────────────────────────────────────

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);

    // Try Documents\My Games\Skyrim Special Edition\SKSE\ first,
    // then fall back to Data\SKSE\Plugins\ beside Skyrim.exe.
    auto setupLog = [&]() -> std::filesystem::path {
        if (auto p = SKSE::log::log_directory()) {
            std::error_code ec;
            std::filesystem::create_directories(*p, ec);
            if (!ec) return *p / "SlaveTatsUI.log";
        }
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        auto dir = std::filesystem::path(exe).parent_path() / L"Data" / L"SKSE" / L"Plugins";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir / "SlaveTatsUI.log";
    };

    std::filesystem::path pluginDir;
    try {
        auto logPath = setupLog();
        pluginDir = logPath.parent_path();
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        auto log  = std::make_shared<spdlog::logger>("SlaveTatsUI", std::move(sink));
        log->set_level(spdlog::level::debug);
        log->flush_on(spdlog::level::debug);
        spdlog::set_default_logger(std::move(log));
    } catch (...) {}

    if (!pluginDir.empty()) {
        std::error_code absoluteError;
        const auto configPath = std::filesystem::absolute(pluginDir / "SlaveTatsUI.json", absoluteError);
        if (!absoluteError) {
            g_pluginConfig = std::make_shared<runtime::PluginConfigFile>(configPath);
            logger::info("SlaveTatsUI: configuration path is '{}'", configPath.string());
        }
    }
    if (!g_pluginConfig) {
        logger::warn("SlaveTatsUI: configuration path unavailable; persistence disabled");
    }
    g_hotkeyBinding = std::make_unique<runtime::HotkeyBinding>(g_pluginConfig);
    if (!g_hotkeyBinding->load()) {
        logger::warn("SlaveTatsUI: failed to load hotkey configuration; hotkey disabled");
    }
    if (g_pluginConfig) {
        g_favoriteStore = std::make_unique<runtime::FavoriteStore>(g_pluginConfig);
        const auto favorites = g_favoriteStore->load();
        if (favorites) {
            g_nativeCatalogBrowser.setFavoriteIdentities(std::move(*favorites));
        } else {
            logger::warn("SlaveTatsUI: failed to load favorites: {}", favorites.error().message);
        }
    }

    static native::OfficialMenuFrameworkAdapter menuFrameworkAdapter;
    static native::NativeMenu nativeMenu([](native::NativeMenu& menu) {
        native::OfficialMenuFrameworkAdapter::renderFoundation(
            g_nativeSlotWorkflow,
            g_nativeSlotWorkflowRuntime,
            g_nativeCatalogBrowser,
            *g_nativeThumbnailRuntime,
            [&menu] { menu.close(); });
    }, [] {
        return native::OfficialMenuFrameworkAdapter::renderLauncher(*g_hotkeyBinding);
    });
    nativeMenu.setOpenGuard([] {
        auto* ui = RE::UI::GetSingleton();
        return ui &&
            !ui->IsMenuOpen(RE::Console::MENU_NAME) &&
            !ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME);
    });
    nativeMenu.setOpenCallback([] { g_nativeSlotWorkflow.resetSession(); });
    nativeMenu.setCloseRequestCallback([] {
        if (!g_nativeSlotWorkflow.editAppearance()) {
            return false;
        }
        (void)g_nativeSlotWorkflow.requestEditAppearanceClose();
        return true;
    });
    g_nativeMenu = &nativeMenu;
    if (const auto result = nativeMenu.registerMenu(menuFrameworkAdapter); !result) {
        logger::warn(
            "SlaveTatsUI: native menu unavailable: {}",
            native::registrationErrorName(result.error()));
    } else {
        g_hotkeyInputEvent = SKSEMenuFramework::AddInputEvent(&onMenuHotkeyInput);
        logger::info(
            "SlaveTatsUI: native menu and input callback registered "
            "(frameworkVersion={:.2f}, blocking=true)",
            menuFrameworkAdapter.version());
    }

    auto* msg = SKSE::GetMessagingInterface();
    if (!msg) {
        logger::critical("SlaveTatsUI: messaging interface unavailable");
        return false;
    }

    msg->RegisterListener("SKSE",          onSKSEMessage);
    msg->RegisterListener("SlaveTatsNG",   onSlaveTatsMessage);
    msg->RegisterListener("JContainers64", onJContainersMessage);

    logger::info("SlaveTatsUI: loaded — hotkey={}", g_hotkeyBinding->label());
    return true;
}
