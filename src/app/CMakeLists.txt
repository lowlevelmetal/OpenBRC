add_executable(openbrc WIN32 MACOSX_BUNDLE
    main.cpp
    Autoplay.cpp
    CommandLine.cpp
    GameDataSetup.cpp
    Settings.cpp)
target_link_libraries(openbrc PRIVATE brc_game OpenBRC::render OpenBRC::platform OpenBRC::audio)

if(WIN32)
    configure_file(openbrc.rc.in "${CMAKE_CURRENT_BINARY_DIR}/openbrc.rc" @ONLY)
    target_sources(openbrc PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/openbrc.rc")
    if(MINGW)
        # SDL provides main(); keep a console-less GUI program.
        target_link_options(openbrc PRIVATE -mwindows)
    endif()
endif()

if(APPLE)
    set(_icns "${PROJECT_SOURCE_DIR}/packaging/icons/openbrc.icns")
    target_sources(openbrc PRIVATE "${_icns}")
    set_source_files_properties("${_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    configure_file("${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in" "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
    set_target_properties(openbrc PROPERTIES
        OUTPUT_NAME openbrc
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
    # Bundle MoltenVK when it is available, so the Vulkan renderer works out of
    # the box (SDL looks for libMoltenVK.dylib in the bundle's Frameworks).
    find_library(OPENBRC_MOLTENVK NAMES MoltenVK PATHS /opt/homebrew/lib /usr/local/lib)
    if(OPENBRC_MOLTENVK)
        add_custom_command(TARGET openbrc POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:openbrc>/Frameworks"
            COMMAND "${CMAKE_COMMAND}" -E copy "${OPENBRC_MOLTENVK}" "$<TARGET_BUNDLE_CONTENT_DIR:openbrc>/Frameworks/libMoltenVK.dylib"
            VERBATIM)
    endif()
endif()
