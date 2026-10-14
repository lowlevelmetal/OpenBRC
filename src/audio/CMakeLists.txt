add_library(brc_audio STATIC
    Device.cpp
    Mixer.cpp)
target_link_libraries(brc_audio PUBLIC OpenBRC::core PRIVATE SDL3::SDL3)
add_library(OpenBRC::audio ALIAS brc_audio)
