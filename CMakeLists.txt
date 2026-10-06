cmake_minimum_required(VERSION 3.40)
set(NAME XEMIUtil CACHE STRING "")
set(VERSION 1.7.0.0 CACHE STRING "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Project ----

project(
 ${NAME}
 VERSION ${VERSION}
 LANGUAGES CXX
)

include(CheckIPOSupported)
include(GNUInstallDirs)

configure_file(
 ${CMAKE_CURRENT_SOURCE_DIR}/CMake/Plugin.h.in
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

# ---- Create DLL ----

add_library(
 ${PROJECT_NAME}
 SHARED
 ${headers}
 ${sources}
)

target_compile_features(
 ${PROJECT_NAME}
 PUBLIC
  cxx_std_23
)

target_include_directories(
 ${PROJECT_NAME}
 PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(CommonLibSSE CONFIG REQUIRED)
find_package(DirectXTK CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)

target_link_libraries(
 ${PROJECT_NAME}
 PRIVATE
  CommonLibSSE::CommonLibSSE
  reflectcpp::reflectcpp
)

target_precompile_headers(
 ${PROJECT_NAME}
 PRIVATE
  include/PCH.h
)

if(MSVC)
  target_compile_options(
    ${PROJECT_NAME}
    PUBLIC
		# Enhanced debug information for optimized code (includes all type info in PDB for RE/debugging)
		"$<$<CONFIG:Debug>:/Zo>"

		# Standards conformance
		/Zc:preprocessor # Enable conforming preprocessor (required for STATIC_ASSERT_SIZE variadic macro)
		/Z7
		# disable warnings
		"$<$<CONFIG:Debug>:/wd4702>" # unreachable code: debug builds don't optimize away compile-time conditionals
		/wd4005 # macro redefinition
		/wd4061 # enumerator 'identifier' in switch of enum 'enumeration' is not explicitly handled by a case label
		/wd4068 # unknown pragma
		/wd4200 # nonstandard extension used : zero-sized array in struct/union
		/wd4201 # nonstandard extension used : nameless struct/union
		/wd4265 # 'type': class has virtual functions, but its non-trivial destructor is not virtual; instances of this class may not be destructed correctly
		/wd4266 # 'function' : no override available for virtual member function from base 'type'; function is hidden
		/wd4371 # 'classname': layout of class may have changed from a previous version of the compiler due to better packing of member 'member'
		/wd4514 # 'function' : unreferenced inline function has been removed
		/wd4582 # 'type': constructor is not implicitly called
		/wd4583 # 'type': destructor is not implicitly called
		/wd4623 # 'derived class' : default constructor was implicitly defined as deleted because a base class default constructor is inaccessible or deleted
		/wd4625 # 'derived class' : copy constructor was implicitly defined as deleted because a base class copy constructor is inaccessible or deleted
		/wd4626 # 'derived class' : assignment operator was implicitly defined as deleted because a base class assignment operator is inaccessible or deleted
		/wd4710 # 'function' : function not inlined
		/wd4711 # function 'function' selected for inline expansion
		/wd4820 # 'bytes' bytes padding added after construct 'member_name'
		/wd4996 # 'deprecated function': required due to Microsoft STL itself using its own deprecated functions
		/wd5026 # 'type': move constructor was implicitly defined as deleted
		/wd5027 # 'type': move assignment operator was implicitly defined as deleted
		/wd5045 # Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
		/wd5053 # support for 'explicit(<expr>)' in C++17 and earlier is a vendor extension
		/wd5204 # 'type-name': class has virtual functions, but its trivial destructor is not virtual; instances of objects derived from this class may not be destructed correctly
		/wd5220 # 'member': a non-static data member with a volatile qualified type no longer implies that compiler generated copy / move constructors and copy / move assignment operators are not trivial
	)
	target_link_options(
		${PROJECT_NAME}
		PRIVATE
		/DEBUG
		/INCREMENTAL:NO
		/OPT:REF
		/OPT:ICF
	)
endif()
