# header-only library
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/alandtse/MergeMapper
    HEAD_REF main
    REF ad758592e2c4fa77f0c0e985348f17af8fa24371
)

# Install codes
set(MERGEMAPPER_SOURCE ${SOURCE_PATH}/src/MergeMapperPluginAPI.cpp ${SOURCE_PATH}/include/MergeMapperPluginAPI.h)

file(INSTALL ${MERGEMAPPER_SOURCE} DESTINATION ${CURRENT_PACKAGES_DIR}/include)

file(INSTALL ${SOURCE_PATH}/LICENSE DESTINATION ${CURRENT_PACKAGES_DIR}/share/${PORT} RENAME copyright)
