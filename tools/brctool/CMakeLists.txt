add_executable(brctool main.cpp cmd_sources.cpp cmd_omt.cpp cmd_image.cpp)
target_link_libraries(brctool PRIVATE OpenBRC::vfs OpenBRC::formats)
