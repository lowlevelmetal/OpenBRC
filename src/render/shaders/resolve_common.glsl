// Palette resolve, shared by the Vulkan (GLSL 450) and OpenGL (GLSL 410 / 330 / 150 /
// 120) shaders. The including shader defines TEX(sampler, uv) and declares sampler2D
// u_index (8-bit indices, nearest), sampler2D u_palette (256x1 colours, nearest) and
// vec4 src_size = (w, h, 1/w, 1/h). Runs once per source pixel; row 0 is the top line
// of the game image in both APIs.

vec4 resolve_color(vec2 frag) {
    float i = TEX(u_index, (floor(frag) + 0.5) * src_size.zw).r;
    float x = (floor(i * 255.0 + 0.5) + 0.5) * (1.0 / 256.0);
    return vec4(TEX(u_palette, vec2(x, 0.5)).rgb, 1.0);
}
