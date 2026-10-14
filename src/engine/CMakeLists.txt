add_library(brc_engine STATIC
    Anim.cpp
    Broadcaster.cpp
    Element.cpp
    Elements.cpp
    Engine.cpp
    Font.cpp
    Image8.cpp
    Movie.cpp
    Node.cpp
    Resources.cpp
    ScriptExecutor.cpp
    Scripts.cpp
    SoundManager.cpp
    Task.cpp
    Timer.cpp)
target_link_libraries(brc_engine PUBLIC OpenBRC::formats OpenBRC::vfs OpenBRC::audio)
add_library(OpenBRC::engine ALIAS brc_engine)
