# Cross-compile for 64-bit Windows with MinGW-w64 from Linux.
#
#   cmake -S . -B build-mingw -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
#         -DOPENBRC_USE_SYSTEM_DEPS=OFF
#
# Host packages (SDL3 etc.) are Linux builds and cannot be linked, so the
# dependencies are always fetched and built for the target
# (OPENBRC_USE_SYSTEM_DEPS=OFF). Host tools (glslc, Python, git) still come from
# the host. When Wine is installed, test executables run through it.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_TRIPLE x86_64-w64-mingw32 CACHE STRING "MinGW-w64 target triple")

# Prefer the POSIX threading model variant when a distribution ships both
# (Debian/Ubuntu: x86_64-w64-mingw32-g++-posix); std::thread needs it there.
find_program(_mingw_cxx NAMES ${MINGW_TRIPLE}-g++-posix ${MINGW_TRIPLE}-g++ REQUIRED)
find_program(_mingw_cc NAMES ${MINGW_TRIPLE}-gcc-posix ${MINGW_TRIPLE}-gcc REQUIRED)
set(CMAKE_C_COMPILER "${_mingw_cc}")
set(CMAKE_CXX_COMPILER "${_mingw_cxx}")
find_program(CMAKE_RC_COMPILER NAMES ${MINGW_TRIPLE}-windres windres)
find_program(CMAKE_AR NAMES ${MINGW_TRIPLE}-gcc-ar ${MINGW_TRIPLE}-ar)
find_program(CMAKE_RANLIB NAMES ${MINGW_TRIPLE}-gcc-ranlib ${MINGW_TRIPLE}-ranlib)

# Sysroot of the cross toolchain (Arch: /usr/x86_64-w64-mingw32, Debian: same).
set(CMAKE_FIND_ROOT_PATH "/usr/${MINGW_TRIPLE}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER) # host tools (glslc, python, git)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# libstdc++ keeps the Windows console backend of std::print/std::println in
# libstdc++exp (GCC 14+); it must follow all objects and static libraries.
# The Windows-GNU platform module overwrites *_INIT, so seed the cache entry
# with its usual defaults plus stdc++exp.
set(CMAKE_CXX_STANDARD_LIBRARIES
    "-lstdc++exp -lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32"
    CACHE STRING "Libraries linked by default with all C++ applications.")

set(OPENBRC_USE_SYSTEM_DEPS OFF CACHE BOOL "Cross builds must build their own dependencies" FORCE)

find_program(_wine NAMES wine wine64)
if(_wine)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${_wine}" CACHE FILEPATH "Run Windows test binaries")
endif()
