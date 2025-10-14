cmake_minimum_required(VERSION 3.20)
set(NAME "XEMIUtil" CACHE STRING "")
set(VERSION 1.2.3 CACHE STRING "")
set(AE_VERSION 1)
set(SE_VERSION 1)
set(VR_VERSION 1)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)

# ---- Options ----
option(BUILD_SKYRIMAE "Build for Skyrim AE" OFF)
option(BUILD_SKYRIMVR "Build for Skyrim VR" OFF)

# ---- Cache build vars ----

macro(set_from_environment VARIABLE)
	if (NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
		set(${VARIABLE} $ENV{${VARIABLE}})
	endif ()
endmacro()
set_from_environment(VCPKG_ROOT)

if(BUILD_SKYRIMAE)
	add_compile_definitions(SKYRIM_AE)
	add_compile_definitions(SKYRIM_SUPPORT_AE)
	set(CommonLibName "CommonLibSSE")
	set(SkyrimVersion "Skyrim AE")
	set(VERSION ${VERSION}.${AE_VERSION})
	set(VCPKG_MANIFEST_FEATURES "ae")
elseif(BUILD_SKYRIMVR)
	add_compile_definitions(SKYRIM_VR)
	add_compile_definitions(SKYRIMVR)
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
	set(VCPKG_MANIFEST_FEATURES "vr")
	set(CommonLibName "CommonLibVR")
	set(VERSION ${VERSION}.${VR_VERSION})
	set(SkyrimVersion "Skyrim VR")
else()
	add_compile_definitions(SKYRIM_SE)
	set(VCPKG_MANIFEST_FEATURES "sse")
	set(CommonLibName "CommonLibSSE")
	set(VERSION ${VERSION}.${SE_VERSION})
	set(SkyrimVersion "Skyrim SSE")
endif()


if (DEFINED VCPKG_ROOT)
	set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
	set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
else ()
	message(
		WARNING
		"Variable VCPKG_ROOT is not set. Continuing without vcpkg."
	)
endif ()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")

# ---- Project ----

project(
	${NAME}
	VERSION ${VERSION}
	LANGUAGES CXX
)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Plugin.h.in
	${CMAKE_CURRENT_BINARY_DIR}/include/Plugin.h
	@ONLY
)

# ---- Include guards ----

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(
		FATAL_ERROR
			"In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
	)
endif()


if (MSVC)
	if (NOT ${CMAKE_GENERATOR} STREQUAL "Ninja")
		add_compile_options(
			/MP	# Build with Multiple Processes
		)
	endif ()
endif ()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

# ---- Add source files ----

include(CMake/headerlist.cmake)
include(CMake/sourcelist.cmake)

source_group(
	TREE
		${CMAKE_CURRENT_SOURCE_DIR}
	FILES
		${headers}
		${sources}
)

source_group(
	TREE
		${CMAKE_CURRENT_BINARY_DIR}
)

find_path(MERGEMAPPER_INCLUDE_DIRS "MergeMapperPluginAPI.h")

# ---- Create DLL ----

add_library(
	${PROJECT_NAME}
	SHARED
	${headers}
	${sources}
	${MERGEMAPPER_INCLUDE_DIRS}/MergeMapperPluginAPI.cpp
)

target_compile_features(
	${PROJECT_NAME}
	PRIVATE
		cxx_std_23
)

target_include_directories(
	${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${MERGEMAPPER_INCLUDE_DIRS}
)

find_package(${CommonLibName} CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)
find_package(ctre CONFIG REQUIRED)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
		${CommonLibName}::${CommonLibName}
		reflectcpp::reflectcpp
		ctre::ctre
)

target_precompile_headers(
	${PROJECT_NAME}
	PRIVATE
		include/PCH.h
)

if (MSVC)
	target_compile_options(
		${PROJECT_NAME}
		PRIVATE
			/sdl             # Enable Additional Security Checks
			/utf-8           # Set Source and Executable character sets to UTF-8
			/Zi              # Debug Information Format

			/permissive-     # Standards conformance
			/Zc:preprocessor # Enable preprocessor conformance mode

			/wd4200          # nonstandard extension used : zero-sized array in struct/union

			"$<$<CONFIG:DEBUG>:>"
			"$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3>"
	)

	target_link_options(
		${PROJECT_NAME}
		PRIVATE
			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif ()
