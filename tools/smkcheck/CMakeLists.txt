add_executable(smkcheck main.cpp)
target_link_libraries(smkcheck PRIVATE OpenBRC::vfs OpenBRC::formats)
