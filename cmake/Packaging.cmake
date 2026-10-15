# Installation layout and packaging.
#
# Install components:
#   app    the game: openbrc executable, licences, desktop integration
#   tools  developer tools (brctool)
#
# Layouts:
#   Windows  everything in one directory (openbrc.exe); licences as LICENSE.txt
#            and licenses\*.txt
#   macOS    OpenBRC.app bundle (SDL3 is linked statically); licences inside
#            the bundle's Resources
#   Linux    FHS: bin/, share/applications, share/metainfo,
#            share/icons/hicolor, share/doc/openbrc, share/licenses/openbrc
#
# Packages:
#   cpack                Linux: .tar.gz; macOS: .dmg; Windows: portable .zip
#                        (with portable.txt, so settings stay next to the exe)
#   package_installer    Windows: NSIS installer (packaging/windows/openbrc.nsi) and its .sha256
#   cpack --config CPackSourceConfig.cmake   source tarball (never includes local/)

include(GNUInstallDirs)

set(OPENBRC_APP_ID "io.github.lowlevelmetal.OpenBRC")
set(OPENBRC_PACKAGING_DIR "${PROJECT_SOURCE_DIR}/packaging")

if(WIN32)
    set(OPENBRC_INSTALL_BINDIR ".")
    set(OPENBRC_INSTALL_DOCDIR ".")
    set(OPENBRC_INSTALL_LICENSEDIR "licenses")
elseif(APPLE)
    set(OPENBRC_INSTALL_BINDIR ".")
    set(OPENBRC_INSTALL_DOCDIR "OpenBRC.app/Contents/Resources")
    set(OPENBRC_INSTALL_LICENSEDIR "OpenBRC.app/Contents/Resources/licenses")
else()
    set(OPENBRC_INSTALL_BINDIR "${CMAKE_INSTALL_BINDIR}")
    set(OPENBRC_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/openbrc")
    set(OPENBRC_INSTALL_LICENSEDIR "${CMAKE_INSTALL_DATAROOTDIR}/licenses/openbrc")
endif()

# --- Programs ----------------------------------------------------------------
if(TARGET openbrc)
    install(TARGETS openbrc
        RUNTIME DESTINATION "${OPENBRC_INSTALL_BINDIR}" COMPONENT app
        BUNDLE DESTINATION "." COMPONENT app)
endif()
if(TARGET brctool AND NOT APPLE)
    install(TARGETS brctool RUNTIME DESTINATION "${OPENBRC_INSTALL_BINDIR}" COMPONENT tools)
endif()

# MSVC runtime DLLs next to the executable, so no VC++ redistributable is needed.
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${OPENBRC_INSTALL_BINDIR}")
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT app)
    set(CMAKE_INSTALL_UCRT_LIBRARIES OFF) # part of Windows 10+
    include(InstallRequiredSystemLibraries)
endif()

# --- Documentation and licences ----------------------------------------------
if(WIN32)
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "${OPENBRC_INSTALL_DOCDIR}" RENAME LICENSE.txt
            COMPONENT app)
else()
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "${OPENBRC_INSTALL_LICENSEDIR}" COMPONENT app)
endif()
if(EXISTS "${PROJECT_SOURCE_DIR}/README.md")
    install(FILES "${PROJECT_SOURCE_DIR}/README.md" DESTINATION "${OPENBRC_INSTALL_DOCDIR}" COMPONENT app)
endif()

# Licences of bundled third-party code: every dependency fetched with
# FetchContent and linked into the binaries ships its licence text. Test-only
# dependencies are skipped; system packages are not bundled.
set(_openbrc_unbundled_deps googletest gtest)
if(NOT DEFINED FETCHCONTENT_BASE_DIR)
    set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")
endif()
file(GLOB _openbrc_dep_dirs LIST_DIRECTORIES true "${FETCHCONTENT_BASE_DIR}/*-src")
foreach(dep_dir IN LISTS _openbrc_dep_dirs)
    get_filename_component(dep "${dep_dir}" NAME)
    string(REGEX REPLACE "-src$" "" dep "${dep}")
    if(dep IN_LIST _openbrc_unbundled_deps)
        continue()
    endif()
    file(GLOB licenses LIST_DIRECTORIES false
         "${dep_dir}/LICENSE*" "${dep_dir}/LICENCE*" "${dep_dir}/COPYING*")
    list(SORT licenses)
    if(licenses)
        list(GET licenses 0 license)
        install(FILES "${license}" DESTINATION "${OPENBRC_INSTALL_LICENSEDIR}" RENAME "${dep}.txt" COMPONENT app)
    endif()
endforeach()

# Code adapted from projects that are not fetched dependencies.
file(GLOB _openbrc_extra_licenses "${OPENBRC_PACKAGING_DIR}/licenses/*.txt")
if(_openbrc_extra_licenses)
    install(FILES ${_openbrc_extra_licenses} DESTINATION "${OPENBRC_INSTALL_LICENSEDIR}" COMPONENT app)
endif()

# --- Linux desktop integration ---------------------------------------------
if(UNIX AND NOT APPLE)
    install(FILES "${OPENBRC_PACKAGING_DIR}/linux/${OPENBRC_APP_ID}.desktop"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications" COMPONENT app)
    install(FILES "${OPENBRC_PACKAGING_DIR}/linux/${OPENBRC_APP_ID}.metainfo.xml"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo" COMPONENT app)
    install(FILES "${OPENBRC_PACKAGING_DIR}/icons/openbrc.svg"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
            RENAME "${OPENBRC_APP_ID}.svg" COMPONENT app)
    foreach(size 16 24 32 48 64 128 256 512)
        install(FILES "${OPENBRC_PACKAGING_DIR}/icons/openbrc-${size}.png"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${size}x${size}/apps"
                RENAME "${OPENBRC_APP_ID}.png" COMPONENT app)
    endforeach()
endif()

# --- CPack: archives -----------------------------------------------------------
set(CPACK_PACKAGE_NAME "OpenBRC")
set(CPACK_PACKAGE_VENDOR "OpenBRC contributors")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "OpenBRC")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_STRIP_FILES ON)
# Ship only our components: fetched dependencies may have install() rules of
# their own (headers, static libraries) that must not end up in the package.
set(CPACK_COMPONENTS_ALL app tools)
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
set(CPACK_INSTALL_CMAKE_PROJECTS
    "${PROJECT_BINARY_DIR};${PROJECT_NAME};app;/"
    "${PROJECT_BINARY_DIR};${PROJECT_NAME};tools;/")
set(CPACK_VERBATIM_VARIABLES ON)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    set(_openbrc_arch arm64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_openbrc_arch x86_64)
else()
    set(_openbrc_arch x86)
endif()
if(APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64" AND CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
    set(_openbrc_arch universal)
endif()

if(WIN32)
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_FILE_NAME "OpenBRC-${PROJECT_VERSION}-windows-${_openbrc_arch}-portable")
    # Mark the archive as a portable install (see core/Paths.h).
    set(CPACK_PRE_BUILD_SCRIPTS "${OPENBRC_PACKAGING_DIR}/windows/CPackPortable.cmake")
    set(CPACK_OPENBRC_PORTABLE_TXT "${OPENBRC_PACKAGING_DIR}/windows/portable.txt")
elseif(APPLE)
    set(CPACK_GENERATOR DragNDrop)
    set(CPACK_PACKAGE_FILE_NAME "OpenBRC-${PROJECT_VERSION}-macos-${_openbrc_arch}")
    set(CPACK_DMG_VOLUME_NAME "OpenBRC")
    set(CPACK_COMPONENTS_ALL app)
    set(CPACK_INSTALL_CMAKE_PROJECTS "${PROJECT_BINARY_DIR};${PROJECT_NAME};app;/")
else()
    set(CPACK_GENERATOR TGZ)
    set(CPACK_PACKAGE_FILE_NAME "OpenBRC-${PROJECT_VERSION}-linux-${_openbrc_arch}")
endif()

# Source package: the repository minus build output and local game data.
set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "OpenBRC-${PROJECT_VERSION}-source")
set(CPACK_SOURCE_IGNORE_FILES
    "/\\.git/" "/\\.cache/" "/\\.vscode/" "/\\.idea/"
    "/build[^/]*/" "/out/" "/install/" "/local/" "/dist/"
    "\\.[Ii][Ss][Oo]$" "\\.[Oo][Mm][Tt]$" "\\.[Ss][Mm][Kk]$" "\\.bin$" "\\.cue$" "\\.img$"
    "/CMakeUserPresets\\.json$" "/compile_commands\\.json$" "~$" "\\.swp$")

include(CPack)

# --- NSIS installer (Windows targets) --------------------------------------------
# A hand-written NSIS script (game data page), so it is built by this target
# rather than CPack's NSIS generator. makensis may be the Windows build (NSIS
# installs it under Program Files), a native POSIX build when cross-compiling,
# or, with OPENBRC_MAKENSIS_WINE=ON, a Windows makensis.exe run through Wine
# (set OPENBRC_MAKENSIS to the .exe; paths are then passed as Z:\...).
option(OPENBRC_MAKENSIS_WINE "Run OPENBRC_MAKENSIS (a Windows makensis.exe) through Wine" OFF)

function(_openbrc_nsis_path out path)
    if(OPENBRC_MAKENSIS_WINE)
        string(REPLACE "/" "\\" path "Z:${path}")
    else()
        cmake_path(NATIVE_PATH path NORMALIZE path)
    endif()
    set(${out} "${path}" PARENT_SCOPE)
endfunction()

if(WIN32 AND TARGET openbrc)
    set(_pf86 "ProgramFiles(x86)")
    find_program(OPENBRC_MAKENSIS makensis
        PATHS "$ENV{${_pf86}}/NSIS" "$ENV{ProgramFiles}/NSIS" "$ENV{NSIS_HOME}"
        DOC "NSIS compiler used by the package_installer target")
    set(_makensis_cmd "${OPENBRC_MAKENSIS}")
    if(OPENBRC_MAKENSIS_WINE)
        find_program(OPENBRC_WINE NAMES wine wine64 REQUIRED)
        set(_makensis_cmd "${OPENBRC_WINE}" "${OPENBRC_MAKENSIS}")
    endif()
    if(OPENBRC_MAKENSIS)
        set(_stage "${PROJECT_BINARY_DIR}/installer-stage")
        set(_installer "${PROJECT_BINARY_DIR}/OpenBRC-${PROJECT_VERSION}-windows-${_openbrc_arch}-setup.exe")
        _openbrc_nsis_path(_source_native "${PROJECT_SOURCE_DIR}")
        _openbrc_nsis_path(_installer_native "${_installer}")
        _openbrc_nsis_path(_stage_native "${_stage}")
        _openbrc_nsis_path(_script_native "${OPENBRC_PACKAGING_DIR}/windows/openbrc.nsi")
        add_custom_target(package_installer
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_stage}"
            COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}" --config "$<CONFIG>"
                    --component app --prefix "${_stage}" --strip
            COMMAND ${_makensis_cmd} -V3 -INPUTCHARSET UTF8
                    "-DVERSION=${PROJECT_VERSION}"
                    "-DSTAGE_DIR=${_stage_native}"
                    "-DSOURCE_DIR=${_source_native}"
                    "-DOUTFILE=${_installer_native}"
                    "${_script_native}"
            COMMAND "${CMAKE_COMMAND}" "-DFILE=${_installer}" -P "${PROJECT_SOURCE_DIR}/cmake/WriteChecksum.cmake"
            BYPRODUCTS "${_installer}" "${_installer}.sha256"
            WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
            COMMENT "Building ${_installer}"
            VERBATIM)
        add_dependencies(package_installer openbrc)
    else()
        message(STATUS "makensis not found: the package_installer target is unavailable")
    endif()
endif()
