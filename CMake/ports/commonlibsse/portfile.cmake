vcpkg_from_git(
  OUT_SOURCE_PATH SOURCE_PATH
  URL https://github.com/powerof3/CommonLibSSE
  HEAD_REF dev
  REF 1533381f7e431c2d1c37ace868ffd9442ccbb2c3
)

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
        FEATURES
                skyrimae SKYRIM_AE
)

if(SKYRIM_AE)
        vcpkg_configure_cmake(
                SOURCE_PATH "${SOURCE_PATH}"
                PREFER_NINJA
                OPTIONS -DSKSE_SUPPORT_XBYAK=on -DSKYRIM_SUPPORT_AE=on
        )
else()
        vcpkg_configure_cmake(
                SOURCE_PATH "${SOURCE_PATH}"
                PREFER_NINJA
                OPTIONS -DSKSE_SUPPORT_XBYAK=on -DSKYRIM_SUPPORT_AE=off
        )
endif()

vcpkg_install_cmake()
vcpkg_cmake_config_fixup(PACKAGE_NAME CommonLibSSE CONFIG_PATH lib/cmake)
vcpkg_copy_pdbs()

file(GLOB CMAKE_CONFIGS "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE/*.cmake")
file(INSTALL ${CMAKE_CONFIGS} DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE")

file(
        INSTALL "${SOURCE_PATH}/LICENSE"
        DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
        RENAME copyright
)
