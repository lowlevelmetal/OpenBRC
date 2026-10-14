# Project-wide compiler configuration, applied through an interface target so
# third-party code fetched with FetchContent is not affected.

add_library(openbrc_options INTERFACE)
add_library(OpenBRC::options ALIAS openbrc_options)

target_compile_features(openbrc_options INTERFACE cxx_std_23)

# Floating point: every operation rounded as IEEE 754 says, in source order
# (no fused multiply-add contraction, no fast-math), so animation paths and
# timing computations match on every compiler and platform.
if(MSVC)
    target_compile_options(openbrc_options INTERFACE /fp:precise)
else()
    target_compile_options(openbrc_options INTERFACE -ffp-contract=off -fno-fast-math)
endif()

if(MSVC)
    target_compile_options(openbrc_options INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc
        /wd4324) # structure padded due to alignment specifier
    if(OPENBRC_WARNINGS_AS_ERRORS)
        target_compile_options(openbrc_options INTERFACE /WX)
    endif()
    target_compile_definitions(openbrc_options INTERFACE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
    target_compile_options(openbrc_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wcast-align
        -Woverloaded-virtual -Wimplicit-fallthrough -Wno-missing-field-initializers)
    if(OPENBRC_WARNINGS_AS_ERRORS)
        target_compile_options(openbrc_options INTERFACE -Werror)
    endif()
    if(WIN32)
        target_compile_definitions(openbrc_options INTERFACE
            NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    endif()
endif()

if(WIN32)
    # Windows 10 is the oldest target.
    target_compile_definitions(openbrc_options INTERFACE _WIN32_WINNT=0x0A00)
endif()

if(MINGW)
    # Ship a self-contained executable: no libstdc++/libgcc/winpthread DLLs.
    target_link_options(openbrc_options INTERFACE -static -static-libgcc -static-libstdc++)
endif()

if(MINGW AND NOT CMAKE_CXX_STANDARD_LIBRARIES MATCHES "stdc\\+\\+exp")
    # libstdc++ keeps the Windows console backend of std::print in libstdc++exp
    # (GCC 14+). It has to come after every object and static library, which
    # the standard-libraries variable guarantees. The cross toolchain file sets
    # this already; this covers native MSYS2 builds.
    string(APPEND CMAKE_CXX_STANDARD_LIBRARIES " -lstdc++exp")
endif()
