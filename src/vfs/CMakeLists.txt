add_library(brc_vfs STATIC
    DirectoryFs.cpp
    DiscManifest.cpp
    GameSource.cpp
    IsoImage.cpp
    Vfs.cpp)
target_link_libraries(brc_vfs PUBLIC OpenBRC::core)
if(WIN32)
    target_link_libraries(brc_vfs PRIVATE advapi32)
endif()
add_library(OpenBRC::vfs ALIAS brc_vfs)
