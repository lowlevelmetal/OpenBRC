# The scenes register themselves with the node factory from static
# initialisers, so the game is an object library: every object file is linked
# into the executable, even those nothing references by name.
file(GLOB_RECURSE BRC_GAME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Development aid: OPENBRC_GAME_ONLY=<regex> builds only the base files and the
# scene files whose names match, so work-in-progress scenes elsewhere in the
# tree cannot break a build. Unset (the default) builds everything.
set(OPENBRC_GAME_ONLY "" CACHE STRING "Build only these scene source files (regex on the file name)")
if(OPENBRC_GAME_ONLY)
    set(_base "^(Godiva|PlayerRecord|LogoNodes|ToolbarNode|Widgets|Popups|Camera|Preferences)\\.cpp$")
    set(_kept)
    foreach(src IN LISTS BRC_GAME_SOURCES)
        get_filename_component(name "${src}" NAME)
        if(name MATCHES "${_base}" OR name MATCHES "${OPENBRC_GAME_ONLY}")
            list(APPEND _kept "${src}")
        endif()
    endforeach()
    set(BRC_GAME_SOURCES ${_kept})
endif()

add_library(brc_game OBJECT ${BRC_GAME_SOURCES})
target_link_libraries(brc_game PUBLIC OpenBRC::engine)
add_library(OpenBRC::game ALIAS brc_game)
