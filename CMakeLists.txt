cmake_minimum_required(VERSION 3.21)
message("Using toolchain file ${CMAKE_TOOLCHAIN_FILE}.")

project(SlaveTatsUI VERSION 0.1.0 DESCRIPTION "Native SlaveTats UI plugin" LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CTest)

set(headers
    include/SKSEMenuFramework.h
    include/SlaveTatsNG_Interface.h
    include/JContainers/jc_interface.h
    include/JContainers/jcontainers_constants.h
    include/jcontainers_mini.h
    src/pch.h
    src/native/MenuFrameworkPort.h
    src/native/NativeMenu.h
    src/native/OfficialMenuFrameworkAdapter.h
    src/native/NativeCatalogBrowserModel.h
    src/native/NativeSlotWorkflowModel.h
    src/native/NativeSlotWorkflowRuntime.h
    src/native/NativeThumbnailController.h
    src/native/NativeThumbnailRuntime.h
    src/native/D3D11NativeThumbnailSource.h
    src/core/ITattooRuntime.h
    src/core/SlaveTatsService.h
    src/core/TattooModels.h
    src/runtime/OverlaySlotConfiguration.h
    src/runtime/AppliedTattooHandleMembership.h
    src/runtime/SlaveTatsAlpha.h
    src/runtime/ApplicationRuntime.h
    src/runtime/HotkeyBinding.h
    src/runtime/PluginConfigFile.h
    src/runtime/FavoriteStore.h
    src/runtime/RecentTattooStore.h
    src/runtime/AppearancePresetStore.h
    src/runtime/UpdateTattooAppearanceOrchestration.h
    src/runtime/SlaveTatsRuntime.h
    src/repository/TattooCatalogLoader.h
    src/repository/TattooCatalogStore.h
    src/repository/TattooRepository.h
    src/repository/FavoriteIdentity.h
    src/repository/RecentTattooIdentity.h
    src/repository/TattooSourceParser.h
    src/textures/D3D11TextureManager.h
    src/textures/D3D11TextureUploader.h
    src/textures/DdsDecoder.h
    src/repository/TattooSourceScanner.h
    src/textures/ExactStreamReader.h
    src/textures/TextureManager.h
    src/textures/TextureResolver.h
)

set(sources
    src/main.cpp
    src/native/NativeMenu.cpp
    src/native/OfficialMenuFrameworkAdapter.cpp
    src/native/NativeCatalogBrowserModel.cpp
    src/native/NativeSlotWorkflowModel.cpp
    src/native/NativeSlotWorkflowRuntime.cpp
    src/native/ActorTargetProvider.cpp
    src/native/NativeThumbnailController.cpp
    src/native/NativeThumbnailRuntime.cpp
    src/native/D3D11NativeThumbnailSource.cpp
    src/core/SlaveTatsService.cpp
    src/runtime/OverlaySlotConfiguration.cpp
    src/runtime/ApplicationRuntime.cpp
    src/runtime/HotkeyBinding.cpp
    src/runtime/PluginConfigFile.cpp
    src/runtime/FavoriteStore.cpp
    src/runtime/RecentTattooStore.cpp
    src/runtime/AppearancePresetStore.cpp
    src/runtime/UpdateTattooAppearanceOrchestration.cpp
    src/runtime/SlaveTatsRuntime.cpp
    src/repository/TattooCatalogLoader.cpp
    src/repository/TattooCatalogStore.cpp
    src/textures/D3D11TextureManager.cpp
    src/textures/D3D11TextureUploader.cpp
    src/textures/DdsDecoder.cpp
    src/repository/TattooRepository.cpp
    src/repository/TattooSourceParser.cpp
    src/repository/TattooSourceScanner.cpp
    src/textures/TextureResolver.cpp
)

find_package(CommonLibSSE CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME} SOURCES ${headers} ${sources})
add_library("${PROJECT_NAME}::${PROJECT_NAME}" ALIAS "${PROJECT_NAME}")

target_include_directories(${PROJECT_NAME}
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    nlohmann_json::nlohmann_json
    d3d11
    Microsoft::DirectXTex
)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)

if(BUILD_TESTING)
    add_executable(HotkeyBindingTests
        src/runtime/HotkeyBinding.cpp
        src/runtime/PluginConfigFile.cpp
        tests/runtime/HotkeyBindingTests.cpp
    )
    target_include_directories(HotkeyBindingTests PRIVATE src)
    target_link_libraries(HotkeyBindingTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME HotkeyBindingTests COMMAND HotkeyBindingTests)

    add_executable(PluginConfigFileTests
        src/runtime/PluginConfigFile.cpp
        tests/runtime/PluginConfigFileTests.cpp
    )
    target_include_directories(PluginConfigFileTests PRIVATE src)
    target_link_libraries(PluginConfigFileTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME PluginConfigFileTests COMMAND PluginConfigFileTests)

    add_executable(FavoriteStoreTests
        src/runtime/PluginConfigFile.cpp
        src/runtime/FavoriteStore.cpp
        tests/runtime/FavoriteStoreTests.cpp
    )
    target_include_directories(FavoriteStoreTests PRIVATE src)
    target_link_libraries(FavoriteStoreTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME FavoriteStoreTests COMMAND FavoriteStoreTests)

    add_executable(RecentTattooStoreTests
        src/runtime/PluginConfigFile.cpp
        src/runtime/RecentTattooStore.cpp
        tests/runtime/RecentTattooStoreTests.cpp
    )
    target_include_directories(RecentTattooStoreTests PRIVATE src)
    target_link_libraries(RecentTattooStoreTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME RecentTattooStoreTests COMMAND RecentTattooStoreTests)

    add_executable(AppearancePresetStoreTests
        src/runtime/PluginConfigFile.cpp
        src/runtime/AppearancePresetStore.cpp
        src/runtime/HotkeyBinding.cpp
        src/runtime/FavoriteStore.cpp
        src/runtime/RecentTattooStore.cpp
        tests/runtime/AppearancePresetStoreTests.cpp
    )
    target_include_directories(AppearancePresetStoreTests PRIVATE src)
    target_link_libraries(AppearancePresetStoreTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME AppearancePresetStoreTests COMMAND AppearancePresetStoreTests)

    add_executable(NativeThumbnailRuntimeTests
        src/native/NativeThumbnailController.cpp
        src/native/NativeThumbnailRuntime.cpp
        src/repository/TattooRepository.cpp
        tests/native/NativeThumbnailRuntimeTests.cpp
    )
    target_include_directories(NativeThumbnailRuntimeTests PRIVATE src)
    add_test(NAME NativeThumbnailRuntimeTests COMMAND NativeThumbnailRuntimeTests)

    add_executable(D3D11NativeThumbnailSourceTests
        src/native/D3D11NativeThumbnailSource.cpp
        src/textures/D3D11TextureManager.cpp
        src/textures/D3D11TextureUploader.cpp
        src/textures/DdsDecoder.cpp
        src/textures/TextureResolver.cpp
        tests/native/D3D11NativeThumbnailSourceTests.cpp
    )
    target_include_directories(D3D11NativeThumbnailSourceTests PRIVATE src)
    target_link_libraries(D3D11NativeThumbnailSourceTests PRIVATE Microsoft::DirectXTex d3d11)
    add_test(NAME D3D11NativeThumbnailSourceTests COMMAND D3D11NativeThumbnailSourceTests)

    add_executable(NativeThumbnailControllerTests
        src/native/NativeThumbnailController.cpp
        src/repository/TattooRepository.cpp
        tests/native/NativeThumbnailControllerTests.cpp
    )
    target_include_directories(NativeThumbnailControllerTests PRIVATE src)
    add_test(NAME NativeThumbnailControllerTests COMMAND NativeThumbnailControllerTests)

    add_executable(NativeCatalogBrowserModelTests
        src/native/NativeCatalogBrowserModel.cpp
        src/repository/TattooRepository.cpp
        tests/native/NativeCatalogBrowserModelTests.cpp
    )
    target_include_directories(NativeCatalogBrowserModelTests PRIVATE src)
    add_test(NAME NativeCatalogBrowserModelTests COMMAND NativeCatalogBrowserModelTests)

    add_executable(NativeSlotWorkflowModelTests
        src/native/NativeSlotWorkflowModel.cpp
        src/native/NativeCatalogBrowserModel.cpp
        src/repository/TattooRepository.cpp
        tests/native/NativeSlotWorkflowModelTests.cpp
    )
    target_include_directories(NativeSlotWorkflowModelTests PRIVATE src)
    target_link_libraries(NativeSlotWorkflowModelTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME NativeSlotWorkflowModelTests COMMAND NativeSlotWorkflowModelTests)

    add_executable(ActorTargetProviderTests
        src/native/ActorTargetProvider.cpp
        tests/native/ActorTargetProviderTests.cpp
    )
    target_include_directories(ActorTargetProviderTests PRIVATE src)
    target_link_libraries(ActorTargetProviderTests PRIVATE CommonLibSSE::CommonLibSSE)
    add_test(NAME ActorTargetProviderTests COMMAND ActorTargetProviderTests)

    add_executable(NativeSlotWorkflowRuntimeTests
        src/native/NativeSlotWorkflowRuntime.cpp
        src/native/NativeSlotWorkflowModel.cpp
        src/native/NativeCatalogBrowserModel.cpp
        src/repository/TattooRepository.cpp
        tests/native/NativeSlotWorkflowRuntimeTests.cpp
    )
    target_include_directories(NativeSlotWorkflowRuntimeTests PRIVATE src)
    target_link_libraries(NativeSlotWorkflowRuntimeTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME NativeSlotWorkflowRuntimeTests COMMAND NativeSlotWorkflowRuntimeTests)

    add_executable(NativeMenuTests
        src/native/NativeMenu.cpp
        tests/native/NativeMenuTests.cpp
    )
    target_include_directories(NativeMenuTests PRIVATE src)
    add_test(NAME NativeMenuTests COMMAND NativeMenuTests)

    add_executable(OfficialMenuFrameworkAdapterTests
        src/native/OfficialMenuFrameworkAdapter.cpp
        src/runtime/HotkeyBinding.cpp
        src/runtime/PluginConfigFile.cpp
        src/native/NativeCatalogBrowserModel.cpp
        src/native/NativeSlotWorkflowModel.cpp
        src/native/NativeSlotWorkflowRuntime.cpp
        src/native/NativeThumbnailController.cpp
        src/native/NativeThumbnailRuntime.cpp
        src/repository/TattooRepository.cpp
        tests/native/OfficialMenuFrameworkAdapterTests.cpp
    )
    target_include_directories(OfficialMenuFrameworkAdapterTests PRIVATE src include)
    target_link_libraries(OfficialMenuFrameworkAdapterTests PRIVATE CommonLibSSE::CommonLibSSE nlohmann_json::nlohmann_json)
    add_test(NAME OfficialMenuFrameworkAdapterTests COMMAND OfficialMenuFrameworkAdapterTests)

    add_executable(SlaveTatsUICoreTests
        src/core/SlaveTatsService.cpp
        tests/core/SlaveTatsServiceTests.cpp
    )
    target_include_directories(SlaveTatsUICoreTests PRIVATE src)
    add_test(NAME SlaveTatsUICoreTests COMMAND SlaveTatsUICoreTests)

    add_executable(OverlaySlotConfigurationTests
        src/runtime/OverlaySlotConfiguration.cpp
        tests/runtime/OverlaySlotConfigurationTests.cpp
    )
    target_include_directories(OverlaySlotConfigurationTests PRIVATE src)
    add_test(NAME OverlaySlotConfigurationTests COMMAND OverlaySlotConfigurationTests)

    add_executable(SlaveTatsAlphaTests
        tests/runtime/SlaveTatsAlphaTests.cpp
    )
    target_include_directories(SlaveTatsAlphaTests PRIVATE src)
    add_test(NAME SlaveTatsAlphaTests COMMAND SlaveTatsAlphaTests)

    add_test(
        NAME SlaveTatsApplyUpgradePolicyTests
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/SlaveTatsRuntime.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/VerifySlaveTatsApplyUpgradePolicy.cmake
    )

    add_executable(AppliedTattooHandleMembershipTests
        tests/runtime/AppliedTattooHandleMembershipTests.cpp
    )
    target_include_directories(AppliedTattooHandleMembershipTests PRIVATE src)
    add_test(
        NAME AppliedTattooHandleMembershipTests
        COMMAND AppliedTattooHandleMembershipTests
    )

    add_executable(UpdateTattooAppearanceOrchestrationTests
        src/runtime/UpdateTattooAppearanceOrchestration.cpp
        tests/runtime/UpdateTattooAppearanceOrchestrationTests.cpp
    )
    target_include_directories(UpdateTattooAppearanceOrchestrationTests PRIVATE src)
    add_test(
        NAME UpdateTattooAppearanceOrchestrationTests
        COMMAND UpdateTattooAppearanceOrchestrationTests
    )

    add_executable(SlaveTatsRuntimeAppearanceTests
        src/runtime/OverlaySlotConfiguration.cpp
        src/runtime/SlaveTatsRuntime.cpp
        src/runtime/UpdateTattooAppearanceOrchestration.cpp
        tests/runtime/SlaveTatsRuntimeAppearanceTests.cpp
    )
    target_include_directories(SlaveTatsRuntimeAppearanceTests PRIVATE src include)
    target_link_libraries(SlaveTatsRuntimeAppearanceTests PRIVATE CommonLibSSE::CommonLibSSE)
    add_test(
        NAME SlaveTatsRuntimeAppearanceTests
        COMMAND SlaveTatsRuntimeAppearanceTests
    )

    add_executable(ApplicationRuntimeTests
        src/runtime/ApplicationRuntime.cpp
        src/runtime/OverlaySlotConfiguration.cpp
        src/runtime/SlaveTatsRuntime.cpp
        src/runtime/UpdateTattooAppearanceOrchestration.cpp
        src/core/SlaveTatsService.cpp
        tests/runtime/ApplicationRuntimeTests.cpp
    )
    target_include_directories(ApplicationRuntimeTests PRIVATE src include)
    target_link_libraries(ApplicationRuntimeTests PRIVATE CommonLibSSE::CommonLibSSE)
    add_test(NAME ApplicationRuntimeTests COMMAND ApplicationRuntimeTests)

    add_executable(JContainersMiniTests
        tests/runtime/JContainersMiniTests.cpp
    )
    target_include_directories(JContainersMiniTests PRIVATE tests/support include)
    add_test(NAME JContainersMiniTests COMMAND JContainersMiniTests)

    add_executable(TattooSourceScannerTests
        src/repository/TattooSourceScanner.cpp
        tests/repository/TattooSourceScannerTests.cpp
    )
    target_include_directories(TattooSourceScannerTests PRIVATE src)
    add_test(NAME TattooSourceScannerTests COMMAND TattooSourceScannerTests)

    add_executable(ExactStreamReaderTests
        tests/textures/ExactStreamReaderTests.cpp
    )
    target_include_directories(ExactStreamReaderTests PRIVATE src)
    add_test(NAME ExactStreamReaderTests COMMAND ExactStreamReaderTests)

    add_executable(TattooSourceParserTests
        src/repository/TattooSourceParser.cpp
        tests/repository/TattooSourceParserTests.cpp
    )
    target_include_directories(TattooSourceParserTests PRIVATE src)
    target_link_libraries(TattooSourceParserTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME TattooSourceParserTests COMMAND TattooSourceParserTests)

    add_executable(TattooRepositoryTests
        src/repository/TattooRepository.cpp
        tests/repository/TattooRepositoryTests.cpp
    )
    target_include_directories(TattooRepositoryTests PRIVATE src)
    add_test(NAME TattooRepositoryTests COMMAND TattooRepositoryTests)

    add_executable(TattooCatalogLoaderTests
        src/repository/TattooCatalogLoader.cpp
        src/repository/TattooRepository.cpp
        src/repository/TattooSourceParser.cpp
        src/repository/TattooSourceScanner.cpp
        tests/repository/TattooCatalogLoaderTests.cpp
    )
    target_include_directories(TattooCatalogLoaderTests PRIVATE src)
    target_link_libraries(TattooCatalogLoaderTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME TattooCatalogLoaderTests COMMAND TattooCatalogLoaderTests)

    add_executable(TattooCatalogStoreTests
        src/repository/TattooCatalogLoader.cpp
        src/repository/TattooCatalogStore.cpp
        src/repository/TattooRepository.cpp
        src/repository/TattooSourceParser.cpp
        src/repository/TattooSourceScanner.cpp
        tests/repository/TattooCatalogStoreTests.cpp
    )
    target_include_directories(TattooCatalogStoreTests PRIVATE src)
    target_link_libraries(TattooCatalogStoreTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME TattooCatalogStoreTests COMMAND TattooCatalogStoreTests)

    add_executable(TextureResolverTests
        src/textures/TextureResolver.cpp
        tests/textures/TextureResolverTests.cpp
    )
    target_include_directories(TextureResolverTests PRIVATE src)
    add_test(NAME TextureResolverTests COMMAND TextureResolverTests)

    add_executable(TextureCacheTests
        tests/textures/TextureCacheTests.cpp
    )
    target_include_directories(TextureCacheTests PRIVATE src)
    add_test(NAME TextureCacheTests COMMAND TextureCacheTests)

    add_executable(TextureManagerTests
        tests/textures/TextureManagerTests.cpp
        src/textures/TextureResolver.cpp
    )
    target_include_directories(TextureManagerTests PRIVATE src)
    add_test(NAME TextureManagerTests COMMAND TextureManagerTests)

    add_executable(DdsDecoderTests
        tests/textures/DdsDecoderTests.cpp
        src/textures/DdsDecoder.cpp
    )
    target_include_directories(DdsDecoderTests PRIVATE src)
    target_link_libraries(DdsDecoderTests PRIVATE Microsoft::DirectXTex)
    add_test(NAME DdsDecoderTests COMMAND DdsDecoderTests)

    add_executable(D3D11TextureUploaderTests
        tests/textures/D3D11TextureUploaderTests.cpp
        src/textures/D3D11TextureUploader.cpp
        src/textures/DdsDecoder.cpp
    )
    target_include_directories(D3D11TextureUploaderTests PRIVATE src)
    target_link_libraries(D3D11TextureUploaderTests PRIVATE Microsoft::DirectXTex d3d11)
    add_test(NAME D3D11TextureUploaderTests COMMAND D3D11TextureUploaderTests)

    add_executable(D3D11TextureManagerTests
        tests/textures/D3D11TextureManagerTests.cpp
        src/textures/D3D11TextureManager.cpp
        src/textures/D3D11TextureUploader.cpp
        src/textures/DdsDecoder.cpp
        src/textures/TextureResolver.cpp
    )
    target_include_directories(D3D11TextureManagerTests PRIVATE src)
    target_link_libraries(D3D11TextureManagerTests PRIVATE Microsoft::DirectXTex d3d11)
    add_test(NAME D3D11TextureManagerTests COMMAND D3D11TextureManagerTests)
endif()
