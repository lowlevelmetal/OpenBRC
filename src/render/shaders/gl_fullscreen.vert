// OpenGL: the #version line (410 core, 330 core, 150 core or 120) and the ATTR / VARYING
// macros are prepended at run time (OpenGLBackend.cpp).
// One triangle covering the viewport; v_uv is (0,0) at its top-left corner.
ATTR vec2 a_uv;
VARYING vec2 v_uv;

void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_uv.x * 2.0 - 1.0, 1.0 - a_uv.y * 2.0, 0.0, 1.0);
}
