# The scenes register themselves with the node factory from static
# initialisers, so the game is an object library: every object file is linked
# into the executable, even those nothing references by name.
file(GLOB_RECURSE BRC_GAME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(brc_game OBJECT ${BRC_GAME_SOURCES})
target_link_libraries(brc_game PUBLIC OpenBRC::engine)
add_library(OpenBRC::game ALIAS brc_game)
