cmake_minimum_required(VERSION 3.20)
set(NAME "XEMIUtil" CACHE STRING "")
set(VERSION 1.4.0 CACHE STRING "")

# ---- Cache build vars ----

macro(set_from_environment VARIABLE)
  if (NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
    set(${VARIABLE} $ENV{${VARIABLE}})
  endif ()
endmacro()
set_from_environment(VCPKG_ROOT)


if (DEFINED VCPKG_ROOT)
  set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
else ()
  message(
  WARNING
  "Variable VCPKG_ROOT is not set. Continuing without vcpkg."
 )
endif ()

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

add_subdirectory(extern/CommonLibSSE)
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

find_package(reflectcpp CONFIG REQUIRED)
find_package(ctre CONFIG REQUIRED)

target_link_libraries(
 ${PROJECT_NAME}
 PRIVATE
  CommonLibSSE::CommonLibSSE
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
   /MD
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
