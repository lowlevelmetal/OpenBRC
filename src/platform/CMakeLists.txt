add_library(brc_platform STATIC
    Clock.cpp
    Dialogs.cpp
    Mouse.cpp
    Sdl.cpp)
target_link_libraries(brc_platform PUBLIC OpenBRC::core SDL3::SDL3)
add_library(OpenBRC::platform ALIAS brc_platform)
