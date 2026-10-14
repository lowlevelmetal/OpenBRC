# Every tools/<name>/ with a CMakeLists.txt is a developer tool.
file(GLOB _tool_dirs LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT _tool_dirs)
foreach(dir IN LISTS _tool_dirs)
    if(IS_DIRECTORY "${dir}" AND EXISTS "${dir}/CMakeLists.txt")
        add_subdirectory("${dir}")
    endif()
endforeach()
