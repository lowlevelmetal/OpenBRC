# The presentation shaders are precompiled into committed headers
# (src/render/shaders/*.h), so building OpenBRC needs no shader compiler.
# After editing a shader, regenerate them with the `shaders` target, which
# needs glslc (shaderc / Vulkan SDK) and Python 3.
find_program(OPENBRC_GLSLC glslc)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(OPENBRC_GLSLC AND Python3_Interpreter_FOUND)
    add_custom_target(shaders
        COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/gen_shaders.py" --glslc "${OPENBRC_GLSLC}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Regenerating src/render/shaders/*.h"
        VERBATIM)
endif()
