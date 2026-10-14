add_library(brc_render STATIC
    DisplaySettings.cpp
    Geometry.cpp
    Overlay.cpp
    Presenter.cpp
    Screenshot.cpp)
target_link_libraries(brc_render PUBLIC OpenBRC::platform)

# Each backend loads its API at run time (volk through SDL's vkGetInstanceProcAddr,
# OpenGL through SDL_GL_GetProcAddress): no link-time dependency on a Vulkan loader or
# an OpenGL library.
if(OPENBRC_ENABLE_VULKAN)
    target_sources(brc_render PRIVATE VulkanBackend.cpp)
    target_link_libraries(brc_render PRIVATE volk)
    target_compile_definitions(brc_render PUBLIC OPENBRC_HAVE_VULKAN=1)
endif()
if(OPENBRC_ENABLE_OPENGL)
    target_sources(brc_render PRIVATE OpenGLBackend.cpp)
    target_compile_definitions(brc_render PUBLIC OPENBRC_HAVE_OPENGL=1)
endif()

add_library(OpenBRC::render ALIAS brc_render)
