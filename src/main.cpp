#include "pch.h"
#include "SlaveTatsNG_Interface.h"
#include "repository/TattooCatalogStore.h"
#include "native/NativeMenu.h"
#include "native/CatalogPreviewProbe.h"
#include "native/BrowserCameraCoordinator.h"
#include "native/SkyrimBrowserCameraPort.h"
#include "native/FlickLauncher.h"
#include "native/OfficialFlickPort.h"
#include "native/NativeCatalogBrowserModel.h"
#include "native/NativeSlotWorkflowModel.h"
#include "native/NativeSlotWorkflowRuntime.h"
#include "native/NearbyActorProvider.h"
#include "native/D3D11NativeThumbnailSource.h"
#include "native/NativeThumbnailRuntime.h"
#include "native/OfficialMenuFrameworkAdapter.h"
#include "runtime/ApplicationRuntime.h"
#include "runtime/SkeeCapabilityProbe.h"
#include "runtime/SkyrimCatalogPreviewBindings.h"
#include "runtime/AppearancePresetStore.h"
#include "runtime/TattooLoadoutStore.h"
#include "runtime/HotkeyBinding.h"
#include "runtime/MenuSideSetting.h"
#include "runtime/PluginConfigFile.h"
#include "runtime/FavoriteStore.h"
#include "runtime/RecentTattooStore.h"
#include "SKSEMenuFramework.h"
#define SMOOTHCAM_API_COMMONLIB
#include "SmoothCamAPI.h"
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
std::unique_ptr<runtime::RecentTattooStore> g_recentTattooStore;
std::unique_ptr<runtime::AppearancePresetStore> g_appearancePresetStore;
std::unique_ptr<runtime::TattooLoadoutStore> g_tattooLoadoutStore;
native::NativeSlotWorkflowRuntime g_nativeSlotWorkflowRuntime(
    g_nativeSlotWorkflow,
    native::NearbyActorRuntimeTag{},
    [] { return native::scanNearbyActors(); },
    [](std::uint32_t actorFormId, core::TattooArea area) {
        auto result = g_applicationRuntime.service().querySlots(actorFormId, area);
        if (result) native::logCatalogPreviewNodeCapabilities(*result);
        return result;
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
    },
    [] {
        if (!g_recentTattooStore) {
            return runtime::RecentTattooResult(std::unexpected(runtime::ConfigError{
                .message = "Recent tattoo storage is unavailable."}));
        }
        return g_recentTattooStore->load();
    },
    [](const repository::RecentTattooIdentity& identity) {
        if (!g_recentTattooStore) {
            return runtime::RecentTattooResult(std::unexpected(runtime::ConfigError{
                .message = "Recent tattoo storage is unavailable."}));
        }
        return g_recentTattooStore->record(identity);
    },
    [](const native::AppearancePresetTicket& ticket) {
        if (!g_appearancePresetStore) {
            return runtime::AppearancePresetResult(std::unexpected(runtime::ConfigError{
                .message = "Appearance preset storage is unavailable."}));
        }
        switch (ticket.kind) {
        case native::AppearancePresetRequestKind::load:
            return g_appearancePresetStore->load();
        case native::AppearancePresetRequestKind::create:
            if (ticket.preset) return g_appearancePresetStore->create(*ticket.preset);
            break;
        case native::AppearancePresetRequestKind::overwrite:
            if (ticket.preset) return g_appearancePresetStore->overwrite(*ticket.preset);
            break;
        case native::AppearancePresetRequestKind::rename:
            if (ticket.preset) {
                return g_appearancePresetStore->rename(
                    ticket.existingName, ticket.preset->name);
            }
            break;
        case native::AppearancePresetRequestKind::erase:
            return g_appearancePresetStore->erase(ticket.existingName);
        }
        return runtime::AppearancePresetResult(std::unexpected(runtime::ConfigError{
            .message = "Appearance preset request is incomplete."}));
    },
    [](const native::TattooLoadoutStoreTicket& ticket) {
        if (!g_tattooLoadoutStore) {
            return runtime::TattooLoadoutResult(std::unexpected(runtime::ConfigError{
                .message = "Loadout storage is unavailable."}));
        }
        switch (ticket.kind) {
        case native::TattooLoadoutRequestKind::load: return g_tattooLoadoutStore->load();
        case native::TattooLoadoutRequestKind::create:
            if (ticket.loadout) return g_tattooLoadoutStore->create(*ticket.loadout);
            break;
        case native::TattooLoadoutRequestKind::overwrite:
            if (ticket.loadout) return g_tattooLoadoutStore->overwrite(*ticket.loadout);
            break;
        case native::TattooLoadoutRequestKind::rename:
            if (ticket.loadout) return g_tattooLoadoutStore->rename(ticket.existingName, ticket.loadout->name);
            break;
        case native::TattooLoadoutRequestKind::erase: return g_tattooLoadoutStore->erase(ticket.existingName);
        }
        return runtime::TattooLoadoutResult(std::unexpected(runtime::ConfigError{
            .message = "Loadout request is incomplete."}));
    },
    [](const core::CaptureTattooLoadoutRequest& request) {
        return g_applicationRuntime.service().captureLoadout(request);
    },
    [](const core::PreviewTattooLoadoutRequest& request) {
        return g_applicationRuntime.service().previewLoadout(request);
    },
    [](const core::RestoreTattooLoadoutRequest& request) {
        return g_applicationRuntime.service().restoreLoadout(request);
    },
    [](const core::RetryTattooLoadoutSynchronizationRequest& request) {
        return g_applicationRuntime.service().retryLoadoutSynchronization(request);
    },
    [] { return native::scanLoadedActors(); },
    [](const core::CatalogPreviewBeginRequest& request) {
        return g_applicationRuntime.service().beginCatalogPreview(request);
    },
    [](const core::CatalogPreviewPaintRequest& request) {
        return g_applicationRuntime.service().paintCatalogPreview(request);
    },
    [](const core::CatalogPreviewRestoreRequest& request) {
        return g_applicationRuntime.service().restoreCatalogPreview(request);
    },
    [](const core::CatalogPreviewCommitRequest& request) {
        return g_applicationRuntime.service().commitCatalogPreview(request);
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
native::SkyrimBrowserCameraPort g_browserCameraPort;
native::BrowserCameraCoordinator g_browserCameraCoordinator(
    g_browserCameraPort,
    [](native::BrowserCameraCoordinator::Task task) {
        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) throw std::runtime_error("SKSE task interface unavailable");
        taskInterface->AddTask(std::move(task));
    });
native::FlickLauncher* g_flickLauncher{};
std::unique_ptr<runtime::HotkeyBinding> g_hotkeyBinding;
std::unique_ptr<runtime::MenuSideSetting> g_menuSideSetting;
SKSEMenuFramework::Model::InputEvent* g_hotkeyInputEvent{};
SKSEMenuFramework::Model::Event* g_menuLifecycleEvent{};

}  // namespace

// ── Input handler ─────────────────────────────────────────────────────────────

bool __stdcall onMenuHotkeyInput(RE::InputEvent* event) {
    if (g_nativeMenu) g_nativeMenu->syncWindowState();
    if (!event || !g_hotkeyBinding || !g_nativeMenu) {
        return false;
    }
    const auto* button = event->AsButtonEvent();
    if (!button) {
        return false;
    }
    auto* ui = RE::UI::GetSingleton();
    const bool canOpen = ui &&
        !ui->GameIsPaused() &&
        !ui->IsItemMenuOpen() &&
        !ui->IsModalMenuOpen() &&
        !ui->IsApplicationMenuOpen() &&
        !ui->IsMenuOpen(RE::Console::MENU_NAME) &&
        !ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME);
    return g_nativeMenu->handleFrameworkHotkey(
        event->GetDevice() == RE::INPUT_DEVICE::kKeyboard,
        button->IsDown(),
        g_hotkeyBinding->matches(button->GetIDCode()),
        canOpen);
}

void __stdcall onMenuLifecycleEvent(SKSEMenuFramework::Model::EventType event) {
    if (event == SKSEMenuFramework::Model::kBeforeRender ||
        event == SKSEMenuFramework::Model::kAfterRender ||
        event == SKSEMenuFramework::Model::kCloseMenu) {
        if (g_nativeMenu) g_nativeMenu->syncWindowState();
    }
}

// ── SKSE message handlers ─────────────────────────────────────────────────────

static void onSKSEMessage(SKSE::MessagingInterface::Message* msg) {
    switch (msg->type) {
    case SKSE::MessagingInterface::kPreLoadGame:
    case SKSE::MessagingInterface::kNewGame:
        if (g_nativeMenu) g_nativeMenu->close();
        g_browserCameraCoordinator.setOpen(false);
        g_applicationRuntime.runtime().invalidateCatalogPreview();
        g_nativeSlotWorkflowRuntime.invalidateCatalogPreview();
        break;
    case SKSE::MessagingInterface::kPostLoad:
        if (auto* messaging = SKSE::GetMessagingInterface()) {
            const bool registered = SmoothCamAPI::RegisterInterfaceLoaderCallback(
                messaging, [](void* instance, SmoothCamAPI::InterfaceVersion version) {
                    g_browserCameraPort.setSmoothCamInterface(
                        instance, static_cast<int>(version) + 1);
                    logger::info("SlaveTatsUI: SmoothCam camera interface V{} available",
                        static_cast<int>(version) + 1);
                });
            if (!registered) {
                logger::info("SlaveTatsUI: SmoothCam interface unavailable");
            }
        }
        if (g_flickLauncher && g_nativeMenu && g_nativeMenu->isRegistered()) {
            if (g_flickLauncher->registerTool()) {
                logger::info("SlaveTatsUI: FLICK launcher registered");
            } else {
                logger::info("SlaveTatsUI: FLICK unavailable; Menu Framework remains available");
            }
        }
        break;
    case SKSE::MessagingInterface::kPostPostLoad:
        if (auto* messaging = SKSE::GetMessagingInterface()) {
            stui::runtime::skee::InterfaceExchange exchange;
            const bool dispatched = messaging->Dispatch(
                stui::runtime::skee::exchangeMessage,
                &exchange, sizeof(exchange), "skee");
            auto* interface = dispatched
                ? stui::runtime::skee::queryOverrideInterface(exchange.interfaceMap)
                : nullptr;
            const auto capability = !dispatched
                ? stui::runtime::skee::OverrideCapability::dispatchFailed
                : !exchange.interfaceMap
                    ? stui::runtime::skee::OverrideCapability::mapUnavailable
                    : interface
                        ? stui::runtime::skee::OverrideCapability::version2Detected
                        : stui::runtime::skee::OverrideCapability::interfaceUnavailable;
            if (interface) {
                g_applicationRuntime.runtime().bindCatalogPreview(
                    stui::runtime::makeSkyrimCatalogPreviewBindings(interface));
            }
            logger::info("[CatalogPreview] SKEE Override probe result={} (v2Detected={}); typed nonpersistent backend bound={}",
                static_cast<int>(capability),
                capability == stui::runtime::skee::OverrideCapability::version2Detected,
                interface != nullptr);
            (void)SmoothCamAPI::RequestInterface(
                messaging, SmoothCamAPI::InterfaceVersion::V3);
        }
        break;
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
            // CommonLib's REX declaration and the DirectX SDK declaration describe the same COM object.
            auto* const device = renderer
                ? reinterpret_cast<ID3D11Device*>(renderer->GetRuntimeData().forwarder)
                : nullptr;

            if (textureRoot.empty() || !device || !taskInterface) {
                g_nativeThumbnailRuntime = makeUnavailableNativeThumbnailRuntime(textureRoot);
                if (!g_nativeThumbnailRuntimeUnavailableLogged) {
                    logger::warn(
                        "SlaveTatsUI: native thumbnail runtime unavailable; rendering placeholders only");
                    g_nativeThumbnailRuntimeUnavailableLogged = true;
                }
            } else {
                auto source = std::make_unique<native::D3D11NativeThumbnailSource>(
                    textureRoot,
                    device,
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
    g_menuSideSetting = std::make_unique<runtime::MenuSideSetting>(g_pluginConfig);
    if (!g_menuSideSetting->load()) {
        logger::warn("SlaveTatsUI: failed to load browser position; defaulting to left");
    }
    if (g_pluginConfig) {
        g_favoriteStore = std::make_unique<runtime::FavoriteStore>(g_pluginConfig);
        const auto favorites = g_favoriteStore->load();
        if (favorites) {
            g_nativeCatalogBrowser.setFavoriteIdentities(std::move(*favorites));
        } else {
            logger::warn("SlaveTatsUI: failed to load favorites: {}", favorites.error().message);
        }
        g_recentTattooStore = std::make_unique<runtime::RecentTattooStore>(g_pluginConfig);
        g_appearancePresetStore =
            std::make_unique<runtime::AppearancePresetStore>(g_pluginConfig);
        g_tattooLoadoutStore = std::make_unique<runtime::TattooLoadoutStore>(g_pluginConfig);
    }
    g_nativeSlotWorkflow.initializeRecentTattoos();
    g_nativeSlotWorkflow.initializeAppearancePresets();
    g_nativeSlotWorkflow.initializeTattooLoadouts();

    static native::OfficialMenuFrameworkAdapter menuFrameworkAdapter;
    static native::NativeMenu nativeMenu([](native::NativeMenu& menu) {
        native::OfficialMenuFrameworkAdapter::renderFoundation(
            g_nativeSlotWorkflow,
            g_nativeSlotWorkflowRuntime,
            g_nativeCatalogBrowser,
            *g_nativeThumbnailRuntime,
            g_menuSideSetting->side(),
            [](std::uint32_t formId, native::BrowserCameraPointerInput input) {
                g_browserCameraCoordinator.setTarget(
                    formId, g_menuSideSetting->side() == runtime::MenuSide::right,
                    input.frameCenterX);
                if (!input.empty()) g_browserCameraCoordinator.control(input);
                g_browserCameraCoordinator.refresh();
            },
            [&menu] { menu.close(); });
    }, [] {
        return native::OfficialMenuFrameworkAdapter::renderLauncher(
            *g_hotkeyBinding, *g_menuSideSetting);
    });
    nativeMenu.setOpenGuard([] {
        auto* ui = RE::UI::GetSingleton();
        return ui &&
            !ui->IsMenuOpen(RE::Console::MENU_NAME) &&
            !ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME);
    });
    nativeMenu.setOpenCallback([] {
        g_nativeSlotWorkflow.resetSession();
        g_browserCameraCoordinator.setOpen(true);
    });
    nativeMenu.setCloseCallback([] {
        g_browserCameraCoordinator.setOpen(false);
        if (g_nativeSlotWorkflowRuntime.closeCatalogPreview(false)) {
            g_nativeSlotWorkflowRuntime.pump();
        }
    });
    nativeMenu.setCloseRequestCallback([] {
        if (g_nativeSlotWorkflowRuntime.closeCatalogPreview(true)) return true;
        if (g_nativeSlotWorkflow.tattooLoadoutActorPending()) return true;
        if (g_nativeSlotWorkflow.tattooLoadoutScreen() != native::TattooLoadoutScreen::none) {
            return !g_nativeSlotWorkflow.applyTattooLoadoutIntent(native::TattooLoadoutIntent::cancel);
        }
        if (!g_nativeSlotWorkflow.editAppearance()) {
            return false;
        }
        (void)g_nativeSlotWorkflow.requestEditAppearanceClose();
        return true;
    });
    g_nativeMenu = &nativeMenu;
    static native::OfficialFlickPort flickPort;
    static native::FlickLauncher flickLauncher(
        flickPort,
        [](native::FlickLauncher::Task task) {
            auto* taskInterface = SKSE::GetTaskInterface();
            if (!taskInterface) return false;
            taskInterface->AddTask(std::move(task));
            return true;
        },
        [] {
            if (g_nativeMenu && g_nativeMenu->isRegistered() && !g_nativeMenu->isOpen()) {
                g_nativeMenu->open();
            }
        });
    g_flickLauncher = &flickLauncher;
    if (const auto result = nativeMenu.registerMenu(menuFrameworkAdapter); !result) {
        logger::warn(
            "SlaveTatsUI: native menu unavailable: {}",
            native::registrationErrorName(result.error()));
    } else {
        g_hotkeyInputEvent = SKSEMenuFramework::AddInputEvent(&onMenuHotkeyInput);
        g_menuLifecycleEvent = SKSEMenuFramework::AddEvent(&onMenuLifecycleEvent, 0.0F);
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
