configure_file(Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/core/Version.h" @ONLY)

add_library(brc_core STATIC
    File.cpp
    Ini.cpp
    Log.cpp
    Paths.cpp
    Png.cpp
    Sha256.cpp
    StringUtil.cpp)
target_include_directories(brc_core PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(brc_core PUBLIC OpenBRC::options Threads::Threads)
if(WIN32)
    target_link_libraries(brc_core PUBLIC shell32 ole32)
endif()
add_library(OpenBRC::core ALIAS brc_core)
