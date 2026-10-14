# Third-party dependencies.
#
# Every dependency is pinned to an upstream release tag. When
# OPENBRC_USE_SYSTEM_DEPS is ON, an installed package is preferred where one is
# commonly available (Linux distributions, Homebrew); otherwise the source is
# fetched and built as part of the project, which is what Windows builds do.

include(FetchContent)

set(FETCHCONTENT_QUIET ON)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
if(OPENBRC_USE_SYSTEM_DEPS)
    set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE OPT_IN)
else()
    set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)
endif()

# Let our set() calls below override option() defaults in fetched projects.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# --- SDL3: window, input, audio device, file dialogs, Vulkan/GL loading -------
set(SDL_SHARED OFF)
set(SDL_STATIC ON)
set(SDL_TEST_LIBRARY OFF)
set(SDL_EXAMPLES OFF)
set(SDL_TESTS OFF)
set(SDL_INSTALL OFF)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE
    SYSTEM
    FIND_PACKAGE_ARGS 3.2 CONFIG)
FetchContent_MakeAvailable(SDL3)

# --- Vulkan: headers and the volk loader (no link-time dependency on a loader;
# on macOS the loader comes from MoltenVK / the Vulkan SDK at run time) --------
if(OPENBRC_ENABLE_VULKAN)
    FetchContent_Declare(VulkanHeaders
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG vulkan-sdk-1.4.363.0
        GIT_SHALLOW TRUE
        SYSTEM)
    set(VOLK_PULL_IN_VULKAN OFF)
    FetchContent_Declare(volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG vulkan-sdk-1.4.363.0
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(VulkanHeaders volk)
    target_link_libraries(volk PUBLIC Vulkan::Headers)
endif()

# --- Tests -------------------------------------------------------------------
if(OPENBRC_BUILD_TESTS)
    set(gtest_force_shared_crt ON)
    set(INSTALL_GTEST OFF)
    set(BUILD_GMOCK OFF)
    FetchContent_Declare(GTest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.18.0
        GIT_SHALLOW TRUE
        SYSTEM
        FIND_PACKAGE_ARGS 1.14 NAMES GTest)
    FetchContent_MakeAvailable(GTest)
    if(NOT TARGET GTest::gtest_main AND TARGET gtest_main)
        add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
endif()

find_package(Threads REQUIRED)
