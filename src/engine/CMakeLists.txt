add_library(brc_engine STATIC
    Anim.cpp
    Broadcaster.cpp
    Element.cpp
    Engine.cpp
    Image8.cpp
    Node.cpp
    Resources.cpp
    ScriptExecutor.cpp
    Scripts.cpp
    SoundManager.cpp
    Task.cpp
    Timer.cpp)
target_link_libraries(brc_engine PUBLIC OpenBRC::formats OpenBRC::vfs OpenBRC::audio)
add_library(OpenBRC::engine ALIAS brc_engine)
