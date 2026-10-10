vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO alandtse/CommonLibSSE-NG
    REF "v${VERSION}"
    SHA512 f34561dc4bf58dc670d9c4c61e8142bc305043281ff041811e00e116d80387d4acbf4afaaab822ff88876ce8bc8862b7f4fb43131353ab82c8cda290fb9622ea
)

vcpkg_from_github(
    OUT_SOURCE_PATH OPENVR_SOURCE_PATH
    REPO ValveSoftware/openvr
    REF 60eb187801956ad277f1cae6680e3a410ee0873b
    SHA512 bb85b4705e7095ac65df9969112b2df8930cee7917cc5f14231c5a0ffeed7a73ffa60727fd32f8786a403656f95a3ec0f80bf3ceabc5b8ede964aefb920bc718
)

file(COPY "${OPENVR_SOURCE_PATH}/" DESTINATION "${SOURCE_PATH}/extern/openvr")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTS=OFF
        -DSKSE_SUPPORT_PATCH_SAFETY=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME CommonLibSSE CONFIG_PATH lib/cmake/CommonLibSSE)
vcpkg_copy_pdbs()

file(INSTALL
    "${SOURCE_PATH}/cmake/CommonLibSSE.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE"
)
file(INSTALL
    "${SOURCE_PATH}/extern/openvr/lib/win64/openvr_api.lib"
    DESTINATION "${CURRENT_PACKAGES_DIR}/lib"
)
file(COPY
    "${OPENVR_SOURCE_PATH}/headers/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include"
)

set(commonlibsse_targets "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE-targets.cmake")
vcpkg_replace_string(
    "${commonlibsse_targets}"
    "${SOURCE_PATH}/extern/openvr/lib/win64/openvr_api.lib"
    "\${_IMPORT_PREFIX}/lib/openvr_api.lib"
)

set(commonlibsse_config "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSEConfig.cmake")
vcpkg_replace_string(
    "${commonlibsse_config}"
    "find_dependency(spdlog CONFIG)"
    "find_dependency(spdlog CONFIG)\nfind_dependency(directxtk CONFIG)"
)

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING.txt")
