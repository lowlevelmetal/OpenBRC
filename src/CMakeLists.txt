# Modules, in dependency order. A module is built when its directory has a
# CMakeLists.txt, so subsystems can land independently.
#
#   core      logging, INI, files, paths, strings, hashing (no third-party deps)
#   vfs       disc images, directories, mount stack, game-source detection
#   formats   the original data formats: OMT containers, canvases, sprites,
#             animations, scripts, WAVE, Smacker
#   platform  SDL3 window, displays, input, timing, dialogs
#   render    presentation of the 8-bit game screen: Vulkan and OpenGL backends
#   audio     software mixer and SDL3 output
#   engine    the Human Code / OMT runtime the game is written against: nodes,
#             animations, scripts, sounds, movies, palette, cursor
#   game      the Barbie Riding Club scenes ("Godiva" nodes) and game state
#   app       the openbrc executable, settings and first-run setup
#
# OPENBRC_MODULES is defined in the top-level CMakeLists.txt.
foreach(module IN LISTS OPENBRC_MODULES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${module}/CMakeLists.txt")
        add_subdirectory(${module})
    endif()
endforeach()
