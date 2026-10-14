add_library(brc_formats STATIC
    Canvas.cpp
    HcAnim.cpp
    OmtFile.cpp
    OmtWriter.cpp
    Script.cpp
    Smacker.cpp
    Wave.cpp)
target_link_libraries(brc_formats PUBLIC OpenBRC::core)
add_library(OpenBRC::formats ALIAS brc_formats)
