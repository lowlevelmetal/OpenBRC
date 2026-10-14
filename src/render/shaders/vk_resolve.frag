#version 450
#extension GL_GOOGLE_include_directive : require
layout(set = 0, binding = 0) uniform sampler2D u_index;
layout(set = 0, binding = 1) uniform sampler2D u_palette;
layout(push_constant) uniform Push {
    vec4 src_size;
    vec4 out_size;
    vec4 uv_rect;
    vec4 params;
};
layout(location = 0) out vec4 o_color;
#define TEX(s, c) texture(s, c)
#include "resolve_common.glsl"

void main() {
    o_color = resolve_color(gl_FragCoord.xy);
}
