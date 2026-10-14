// Scaling filters and the toast overlay, shared by the Vulkan (GLSL 450) and OpenGL
// (GLSL 410 / 330 / 150 / 120) present shaders. The including shader defines
// TEX(sampler, uv) and declares sampler2D u_image (resolved frame, bilinear), sampler2D
// u_overlay (toast coverage, nearest) and the vec4 parameters
//   src_size = (w, h, 1/w, 1/h) of the source image
//   out_size = (w, h, 1/w, 1/h) of the viewport the image is drawn into
//   uv_rect  = overlay sub-rectangle of u_overlay: offset.xy, scale.zw
//   params   = (mode, alpha, sRGB output, unused); mode 0 sharp, 1 nearest, 2 smooth,
//              3 CRT, 4 overlay
// uv is (0,0) at the top-left of the viewport, frag the window pixel (gl_FragCoord.xy).

vec3 sample_nearest(vec2 uv) {
    return TEX(u_image, (floor(uv * src_size.xy) + 0.5) * src_size.zw).rgb;
}

// Sharp bilinear: nearest-neighbour prescale by the integer part of the scale factor,
// then bilinear filtering of the remaining fraction. Pixels stay crisp and evenly sized
// at any output size.
vec2 sharp_uv(vec2 uv, vec2 prescale) {
    vec2 texel = uv * src_size.xy;
    vec2 region = 0.5 - 0.5 / prescale;
    vec2 d = fract(texel) - 0.5;
    vec2 f = (d - clamp(d, -region, region)) * prescale + 0.5;
    return (floor(texel) + f) * src_size.zw;
}

vec3 sample_sharp(vec2 uv) {
    vec2 prescale = max(floor(out_size.xy * src_size.zw), vec2(1.0));
    return TEX(u_image, sharp_uv(uv, prescale)).rgb;
}

// A light CRT look: slightly soft horizontal reconstruction, scanlines whose strength
// follows the vertical scale (they would only alias below ~2x) and a faint
// aperture-grille mask, with the average brightness kept close to the original.
vec3 sample_crt(vec2 uv, vec2 frag) {
    vec2 scale = out_size.xy * src_size.zw;
    vec2 prescale = vec2(max(floor(scale.x * 0.5), 1.0), 1.0);
    vec2 suv = sharp_uv(uv, prescale);
    float line = uv.y * src_size.y;
    suv.y = (floor(line) + 0.5) * src_size.w;
    vec3 c = TEX(u_image, suv).rgb;

    float strength = clamp((scale.y - 1.5) / 1.5, 0.0, 1.0);
    float d = fract(line) - 0.5;
    float w = clamp(0.5 + 0.5 * cos(d * 6.2831853), 0.0, 1.0);
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    float depth = 0.4 * strength;
    float beam = mix(1.0 - depth, 1.0, pow(w, mix(1.4, 0.6, luma)));

    float m = mod(floor(frag.x), 3.0);
    vec3 mask = vec3(1.0 - 0.1 * strength);
    if (m < 0.5) mask.r = 1.0;
    else if (m < 1.5) mask.g = 1.0;
    else mask.b = 1.0;
    return clamp(c * beam * mask * (1.0 + 0.25 * strength), 0.0, 1.0);
}

// The image holds sRGB-encoded values; an sRGB render target encodes again on write.
vec3 output_encode(vec3 c) {
    return params.z > 0.5 ? pow(c, vec3(2.2)) : c;
}

vec4 present_color(vec2 uv, vec2 frag) {
    float mode = params.x;
    if (mode > 3.5) {
        float cov = TEX(u_overlay, uv_rect.xy + uv * uv_rect.zw).r;
        return vec4(output_encode(vec3(cov)), mix(0.6, 1.0, cov) * params.y);
    }
    vec3 c;
    if (mode < 0.5) c = sample_sharp(uv);
    else if (mode < 1.5) c = sample_nearest(uv);
    else if (mode < 2.5) c = TEX(u_image, uv).rgb;
    else c = sample_crt(uv, frag);
    return vec4(output_encode(c), 1.0);
}
