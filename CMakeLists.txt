cmake_minimum_required(VERSION 3.16..3.30)

############################################
# Project setup
############################################
include(cmake/compilers.cmake)

if(DEFINED ENV{CMAKE_C_COMPILER} AND DEFINED ENV{CMAKE_CXX_COMPILER})
    message(STATUS "Setting C and C++ compiler from environment variables")
    set(CMAKE_C_COMPILER $ENV{CMAKE_C_COMPILER})
    set(CMAKE_CXX_COMPILER $ENV{CMAKE_CXX_COMPILER})
endif()

if(CMAKE_CXX_COMPILER AND CMAKE_C_COMPILER)
    message(STATUS "Using specifed C++ compiler: ${CMAKE_CXX_COMPILER}")
    message(STATUS "Using specifed C compiler: ${CMAKE_C_COMPILER}")
else()
    message(STATUS "No C or C++ compiler specified, defaulting to Clang-17")
    FIND_AND_SET_CLANG17()
endif()

project(
    NoCModel
    VERSION 0.00.1
    DESCRIPTION "Tenstorrent NoC Model"
    HOMEPAGE_URL "https://github.com/tenstorrent/tt-noc-model"
    LANGUAGES
        CXX
)

if(${PROJECT_SOURCE_DIR} STREQUAL ${PROJECT_BINARY_DIR})
    message(
        FATAL_ERROR
        "CMake generation is not allowed within source directory!! Please set a build folder with '-B'!!"
    )
endif()

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(project_options)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(ENABLE_LIBCXX)
    add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,Clang>:-stdlib=libc++>)
    #add_link_options(
    #    $<$<LINK_LANG_AND_ID:CXX,Clang>:-lc++>
    #    $<$<LINK_LANG_AND_ID:CXX,Clang>:-lc++abi>
    #)
else()
    # required when linking with libstdc++ with clang and gcc
    add_compile_options(-fsized-deallocation)
endif()

# Using below until we can move to CMake >= 3.18 for LINK_LANG_AND_ID
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND ENABLE_LIBCXX)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -v -lc++ -lc++abi")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -v -lc++ -lc++abi")
endif()

include(CTest)

# Global settings if we're the top-level project
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_CONFIGURATION_TYPES
        Release
        RelWithDebInfo
        Debug
        CI
    )
    if(NOT CMAKE_BUILD_TYPE AND NOT isMultiConfig)
        message(STATUS "Setting build type to 'Release' as none was specified.")
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Release build is the default" FORCE)
    endif()

    set_property(
        GLOBAL
        PROPERTY
            GLOBAL_DEPENDS_NO_CYCLES
                ON
    )

    if(ENABLE_CCACHE)
        include(cmake/ccache.cmake)
    endif()
endif()

CHECK_COMPILERS()

if(NOT isMultiConfig AND NOT CMAKE_BUILD_TYPE IN_LIST CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "Invalid build type: ${CMAKE_BUILD_TYPE}. Valid options are: ${CMAKE_CONFIGURATION_TYPES}")
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -DDEBUG")
set(CMAKE_CXX_FLAGS_CI "-O3 -DDEBUG")

############################################################################################################################
# Project Options
#   The following options and their defaults impact what artifacts get built
############################################################################################################################
#message(STATUS "Build with ASAN:   ${ENABLE_ASAN}")
#message(STATUS "Build with MSAN:   ${ENABLE_MSAN}")
#message(STATUS "Build with TSAN:   ${ENABLE_TSAN}")
#message(STATUS "Build with UBSAN:  ${ENABLE_UBSAN}")
############################################################################################################################

set(SANITIZER_ENABLED ${ENABLE_ASAN})
if(SANITIZER_ENABLED AND ENABLE_MSAN)
    message(FATAL_ERROR "Multiple sanitizers are not supported")
elseif(ENABLE_MSAN)
    set(SANITIZER_ENABLED ${ENABLE_MSAN})
endif()

if(SANITIZER_ENABLED AND ENABLE_TSAN)
    message(FATAL_ERROR "Multiple sanitizers are not supported")
elseif(ENABLE_TSAN)
    set(SANITIZER_ENABLED ${ENABLE_TSAN})
endif()

if(SANITIZER_ENABLED AND ENABLE_UBSAN)
    message(FATAL_ERROR "Multiple sanitizers are not supported")
endif()

unset(SANITIZER_ENABLED)

############################################################################################################################
# Find all required libraries to build
############################################################################################################################
include(dependencies)

############################################################################################################################
# Constructing interface libs for common compiler flags, header directories, and libraries
#   These interface libs are linked with PUBLIC scope at lowest common target (tt_metal/common) and at tt_metal_libs level
#   in order to propogate to the rest of tt_metal, tt_eager, etc.
############################################################################################################################
add_library(metal_common_libs INTERFACE)
target_link_libraries(
    metal_common_libs
    INTERFACE
        dl
        z
        pthread
        atomic
        hwloc
        numa
)

add_compile_options(
    -Werror
    -Wdelete-non-virtual-dtor
    -Wreturn-type
    -Wswitch
    -Wuninitialized
    -Wno-unused-parameter
    -mavx2
    -fPIC
    -fvisibility-inlines-hidden
    -fno-lto # FIXME: This seems to be here for ttnn; it should go to TTNN, then.
    "$<$<CXX_COMPILER_ID:Clang>:-Wsometimes-uninitialized>"
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-c++11-narrowing>"
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=local-type-template-args>"
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-delete-non-abstract-non-virtual-dtor>"
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-c99-designator>"
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-shift-op-parentheses>"
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-non-c-typedef-for-linkage>"
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated-this-capture>"
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated-volatile>"
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated-builtins>"
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated-declarations>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-deprecated>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-deprecated-declarations>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-attributes>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-stringop-overread>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-stringop-overflow>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-maybe-uninitialized>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-missing-requires>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-narrowing>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-non-template-friend>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=non-template-friend>"
    "$<$<BOOL:${ENABLE_ASAN}>:-fsanitize=address>"
    "$<$<BOOL:${ENABLE_MSAN}>:-fsanitize=memory>"
    "$<$<BOOL:${ENABLE_TSAN}>:-fsanitize=thread>"
    "$<$<BOOL:${ENABLE_UBSAN}>:-fsanitize=undefined>"
)
add_link_options(
    "$<$<BOOL:${ENABLE_ASAN}>:-fsanitize=address>"
    "$<$<BOOL:${ENABLE_MSAN}>:-fsanitize=memory>"
    "$<$<BOOL:${ENABLE_TSAN}>:-fsanitize=thread>"
    "$<$<BOOL:${ENABLE_UBSAN}>:-fsanitize=undefined>"
)

add_subdirectory(tt_noc_model)
