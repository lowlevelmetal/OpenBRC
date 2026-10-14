// OpenGL: the #version line and the VARYING / TEX / FRAG_COLOR macros are prepended at
// run time (OpenGLBackend.cpp).
uniform sampler2D u_image;
uniform sampler2D u_overlay;
uniform vec4 src_size;
uniform vec4 out_size;
uniform vec4 uv_rect;
uniform vec4 params;
VARYING vec2 v_uv;
#include "present_common.glsl"

void main() {
    FRAG_COLOR = present_color(v_uv, gl_FragCoord.xy);
}
