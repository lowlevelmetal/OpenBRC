// OpenGL: the #version line and the TEX / FRAG_COLOR macros are prepended at run time
// (OpenGLBackend.cpp).
uniform sampler2D u_index;
uniform sampler2D u_palette;
uniform vec4 src_size;
#include "resolve_common.glsl"

void main() {
    FRAG_COLOR = resolve_color(gl_FragCoord.xy);
}
