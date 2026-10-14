// OpenGL presentation backend, the fallback for systems without a usable Vulkan driver.
//
// Contexts, in order of preference:
//   macOS:        4.1 core (forward compatible), 3.2 core, 2.1 legacy
//   Linux/Windows 3.3 core, 2.1 with framebuffer objects (old drivers)
// with the matching GLSL preamble (#version 410 core / 150 core / 330 core / 120).
// Entry points are loaded through SDL_GL_GetProcAddress, so the program does not link
// against an OpenGL library. Same passes as VulkanBackend.cpp: palette resolve into an
// RGBA texture at source size, then scale to the window with the selected filter and
// draw the toast overlay.
//
// Ported from the BarbieRecomp runtime (runtime/render/render_gl.c, the maintainer's own
// code).

#include "core/Log.h"
#include "render/Backend.h"
#include "render/Geometry.h"
#include "render/Overlay.h"
#include "render/shaders/GlslShaders.h"

#include <SDL3/SDL_video.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace brc::render {
namespace {

// ---- minimal OpenGL declarations ---------------------------------------------------------
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLbitfield = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLubyte = unsigned char;
using GLfloat = float;
using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;

#if defined(_WIN32) && !defined(_WIN64)
#define BRC_GLAPI __stdcall
#else
#define BRC_GLAPI
#endif

constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_TEXTURE1 = 0x84C1;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLint GL_NEAREST = 0x2600;
constexpr GLint GL_LINEAR = 0x2601;
constexpr GLint GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum GL_PACK_ALIGNMENT = 0x0D05;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_RED = 0x1903;
constexpr GLint GL_R8 = 0x8229;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLint GL_RGBA8 = 0x8058;
constexpr GLenum GL_BGRA = 0x80E1;
constexpr GLenum GL_LUMINANCE = 0x1909;
constexpr GLint GL_LUMINANCE8 = 0x8040;
constexpr GLbitfield GL_COLOR_BUFFER_BIT = 0x4000;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_SRC_ALPHA = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_STATIC_DRAW = 0x88E4;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_VENDOR = 0x1F00;
constexpr GLenum GL_RENDERER = 0x1F01;
constexpr GLenum GL_VERSION = 0x1F02;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
constexpr GLenum GL_DEPTH_TEST = 0x0B71;
constexpr GLenum GL_CULL_FACE = 0x0B44;
constexpr GLenum GL_CONTEXT_LOST = 0x0507;

// clang-format off
#define BRC_GL_FUNCS(X)                                                                                  \
    X(const GLubyte*, GetString, (GLenum))                                                               \
    X(GLenum, GetError, ())                                                                              \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                                  \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                            \
    X(void, Clear, (GLbitfield))                                                                         \
    X(void, Enable, (GLenum))                                                                            \
    X(void, Disable, (GLenum))                                                                           \
    X(void, BlendFunc, (GLenum, GLenum))                                                                 \
    X(void, GenTextures, (GLsizei, GLuint*))                                                             \
    X(void, DeleteTextures, (GLsizei, const GLuint*))                                                    \
    X(void, BindTexture, (GLenum, GLuint))                                                               \
    X(void, ActiveTexture, (GLenum))                                                                     \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))    \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                                      \
    X(void, PixelStorei, (GLenum, GLint))                                                                \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                                        \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))                         \
    X(GLuint, CreateShader, (GLenum))                                                                    \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))                         \
    X(void, CompileShader, (GLuint))                                                                     \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                                       \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                      \
    X(void, DeleteShader, (GLuint))                                                                      \
    X(GLuint, CreateProgram, ())                                                                         \
    X(void, AttachShader, (GLuint, GLuint))                                                              \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar*))                                         \
    X(void, LinkProgram, (GLuint))                                                                       \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                                      \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                     \
    X(void, UseProgram, (GLuint))                                                                        \
    X(void, DeleteProgram, (GLuint))                                                                     \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                                                \
    X(void, Uniform1i, (GLint, GLint))                                                                   \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                                      \
    X(void, GenBuffers, (GLsizei, GLuint*))                                                              \
    X(void, DeleteBuffers, (GLsizei, const GLuint*))                                                     \
    X(void, BindBuffer, (GLenum, GLuint))                                                                \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                                       \
    X(void, EnableVertexAttribArray, (GLuint))                                                           \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))               \
    X(void, GenFramebuffers, (GLsizei, GLuint*))                                                         \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*))                                                \
    X(void, BindFramebuffer, (GLenum, GLuint))                                                           \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                               \
    X(GLenum, CheckFramebufferStatus, (GLenum))

// Vertex array objects: required by core profiles, absent from some 2.1 drivers.
#define BRC_GL_OPTIONAL_FUNCS(X)                                                                         \
    X(void, GenVertexArrays, (GLsizei, GLuint*))                                                         \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*))                                                \
    X(void, BindVertexArray, (GLuint))
// clang-format on

struct GlApi {
#define BRC_GL_DECLARE(ret, name, args) ret(BRC_GLAPI* name) args = nullptr;
    BRC_GL_FUNCS(BRC_GL_DECLARE)
    BRC_GL_OPTIONAL_FUNCS(BRC_GL_DECLARE)
#undef BRC_GL_DECLARE

    bool load() {
        bool ok = true;
#define BRC_GL_LOAD(ret, name, args)                                                                         \
    name = reinterpret_cast<decltype(name)>(SDL_GL_GetProcAddress("gl" #name));                              \
    if (!name) {                                                                                             \
        log::warn("render: OpenGL: missing entry point gl" #name);                                           \
        ok = false;                                                                                          \
    }
        BRC_GL_FUNCS(BRC_GL_LOAD)
#undef BRC_GL_LOAD
#define BRC_GL_LOAD_OPTIONAL(ret, name, args)                                                                \
    name = reinterpret_cast<decltype(name)>(SDL_GL_GetProcAddress("gl" #name));
        BRC_GL_OPTIONAL_FUNCS(BRC_GL_LOAD_OPTIONAL)
#undef BRC_GL_LOAD_OPTIONAL
        return ok;
    }
};

// One way to get a context, and the GLSL dialect that goes with it.
struct ContextAttempt {
    int major;
    int minor;
    bool core;
    bool forwardCompatible;
    const char* glslVersion;
    const char* label;
};

#ifdef __APPLE__
constexpr ContextAttempt kAttempts[] = {
    {4, 1, true, true, "410 core", "4.1 core"},
    {3, 2, true, true, "150 core", "3.2 core"},
    {2, 1, false, false, "120", "2.1"},
};
#else
constexpr ContextAttempt kAttempts[] = {
    {3, 3, true, false, "330 core", "3.3 core"},
    {2, 1, false, false, "120", "2.1"},
};
#endif

void setContextAttributes(const ContextAttempt& a) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, a.major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, a.minor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, a.core ? SDL_GL_CONTEXT_PROFILE_CORE : 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                        a.forwardCompatible ? SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG : 0);
}

bool isCore(const ContextAttempt* a) {
    return a && a->core;
}

struct Program {
    GLuint prog = 0;
    GLint srcSize = -1;
    GLint outSize = -1;
    GLint uvRect = -1;
    GLint params = -1;
};

} // namespace

class OpenGLBackend final : public Backend {
public:
    OpenGLBackend() = default;
    ~OpenGLBackend() override { destroy(); }

    OpenGLBackend(const OpenGLBackend&) = delete;
    OpenGLBackend& operator=(const OpenGLBackend&) = delete;

    BackendKind kind() const override { return BackendKind::OpenGL; }
    std::string_view name() const override { return "OpenGL"; }
    std::string_view deviceName() const override { return m_deviceName; }
    SDL_WindowFlags windowFlags() const override { return SDL_WINDOW_OPENGL; }

    void prepareWindow() override;
    bool init(SDL_Window* window, int outW, int outH, bool vsync) override;
    void attach() override { SDL_GL_MakeCurrent(m_window, m_context); }
    DrawResult draw(const FrameData& f) override;
    void detach() override { SDL_GL_MakeCurrent(m_window, nullptr); }

private:
    GLuint compile(GLenum type, const char* body, bool vertex);
    bool linkProgram(Program& p, const char* frag, std::initializer_list<const char*> samplers);
    GLuint makeTexture(GLint filter);
    void allocR8(GLuint tex, int w, int h);
    bool createSource(int w, int h);
    void freeObjects();
    void destroy();
    void setUniforms(const Program& p, const float src[4], const float out[4], const float uv[4],
                     const float params[4]);
    void viewport(const Rect& r, int outH) { m_gl.Viewport(r.x, outH - r.y - r.h, r.w, r.h); }

    GlApi m_gl;
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;
    const ContextAttempt* m_attempt = nullptr;
    std::string m_deviceName;
    int m_swapInterval = -2; // currently applied (-2: not yet)
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_texIndex = 0;
    GLuint m_texPalette = 0;
    GLuint m_texOverlay = 0;
    GLuint m_texResolved = 0;
    GLuint m_fbo = 0;
    int m_srcW = 0;
    int m_srcH = 0;
    Program m_resolve;
    Program m_present;
    std::uint64_t m_uploadedSeq = 0;
    std::uint64_t m_overlaySeq = 0;
    bool m_reportedError = false;
};

void OpenGLBackend::prepareWindow() {
    SDL_GL_ResetAttributes();
    setContextAttributes(kAttempts[0]);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
}

GLuint OpenGLBackend::compile(GLenum type, const char* body, bool vertex) {
    std::string pre = std::string("#version ") + m_attempt->glslVersion + "\n";
    if (m_attempt->core) {
        pre += "#define ATTR in\n#define TEX texture\n";
        pre += vertex ? "#define VARYING out\n"
                      : "#define VARYING in\nout vec4 o_frag;\n#define FRAG_COLOR o_frag\n";
    } else {
        pre += "#define ATTR attribute\n#define VARYING varying\n#define TEX texture2D\n"
               "#define FRAG_COLOR gl_FragColor\n";
    }
    const GLchar* src[2] = {pre.c_str(), body};
    GLuint s = m_gl.CreateShader(type);
    m_gl.ShaderSource(s, 2, src, nullptr);
    m_gl.CompileShader(s);
    GLint ok = 0;
    m_gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[2048] = {};
        m_gl.GetShaderInfoLog(s, static_cast<GLsizei>(sizeof info), nullptr, info);
        log::warn("render: OpenGL: shader compile failed: {}", info);
        m_gl.DeleteShader(s);
        return 0;
    }
    return s;
}

bool OpenGLBackend::linkProgram(Program& p, const char* frag, std::initializer_list<const char*> samplers) {
    GLuint vs = compile(GL_VERTEX_SHADER, shaders::kGlslFullscreenVert, true);
    GLuint fs = compile(GL_FRAGMENT_SHADER, frag, false);
    if (!vs || !fs) {
        if (vs)
            m_gl.DeleteShader(vs);
        if (fs)
            m_gl.DeleteShader(fs);
        return false;
    }
    p.prog = m_gl.CreateProgram();
    m_gl.AttachShader(p.prog, vs);
    m_gl.AttachShader(p.prog, fs);
    m_gl.BindAttribLocation(p.prog, 0, "a_uv");
    m_gl.LinkProgram(p.prog);
    m_gl.DeleteShader(vs);
    m_gl.DeleteShader(fs);
    GLint ok = 0;
    m_gl.GetProgramiv(p.prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[2048] = {};
        m_gl.GetProgramInfoLog(p.prog, static_cast<GLsizei>(sizeof info), nullptr, info);
        log::warn("render: OpenGL: program link failed: {}", info);
        return false;
    }
    m_gl.UseProgram(p.prog);
    GLint unit = 0;
    for (const char* name : samplers)
        m_gl.Uniform1i(m_gl.GetUniformLocation(p.prog, name), unit++);
    p.srcSize = m_gl.GetUniformLocation(p.prog, "src_size");
    p.outSize = m_gl.GetUniformLocation(p.prog, "out_size");
    p.uvRect = m_gl.GetUniformLocation(p.prog, "uv_rect");
    p.params = m_gl.GetUniformLocation(p.prog, "params");
    return true;
}

GLuint OpenGLBackend::makeTexture(GLint filter) {
    GLuint t = 0;
    m_gl.GenTextures(1, &t);
    m_gl.BindTexture(GL_TEXTURE_2D, t);
    m_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    m_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    m_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void OpenGLBackend::allocR8(GLuint tex, int w, int h) {
    m_gl.BindTexture(GL_TEXTURE_2D, tex);
    if (isCore(m_attempt))
        m_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    else
        m_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE8, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);
}

bool OpenGLBackend::createSource(int w, int h) {
    allocR8(m_texIndex, w, h);
    m_gl.BindTexture(GL_TEXTURE_2D, m_texResolved);
    m_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_gl.BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texResolved, 0);
    GLenum status = m_gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
    m_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        log::error("render: OpenGL: resolve framebuffer incomplete ({:#x})", status);
        return false;
    }
    m_srcW = w;
    m_srcH = h;
    m_uploadedSeq = 0;
    return true;
}

void OpenGLBackend::freeObjects() {
    if (!m_gl.DeleteTextures)
        return; // functions never loaded
    const GLuint textures[] = {m_texIndex, m_texPalette, m_texOverlay, m_texResolved};
    for (GLuint t : textures)
        if (t)
            m_gl.DeleteTextures(1, &t);
    if (m_fbo)
        m_gl.DeleteFramebuffers(1, &m_fbo);
    if (m_vbo)
        m_gl.DeleteBuffers(1, &m_vbo);
    if (m_vao && m_gl.DeleteVertexArrays)
        m_gl.DeleteVertexArrays(1, &m_vao);
    if (m_resolve.prog)
        m_gl.DeleteProgram(m_resolve.prog);
    if (m_present.prog)
        m_gl.DeleteProgram(m_present.prog);
    m_texIndex = m_texPalette = m_texOverlay = m_texResolved = m_fbo = m_vbo = m_vao = 0;
    m_resolve = {};
    m_present = {};
}

bool OpenGLBackend::init(SDL_Window* window, int /*outW*/, int /*outH*/, bool /*vsync*/) {
    m_window = window;
    m_swapInterval = -2;
    for (const ContextAttempt& a : kAttempts) {
        setContextAttributes(a);
        m_context = SDL_GL_CreateContext(window);
        if (m_context) {
            m_attempt = &a;
            break;
        }
        log::info("render: OpenGL {} context: {}", a.label, SDL_GetError());
    }
    if (!m_context) {
        log::warn("render: OpenGL: cannot create a context");
        return false;
    }
    bool ok = m_gl.load();
    if (ok && isCore(m_attempt) && (!m_gl.GenVertexArrays || !m_gl.BindVertexArray)) {
        log::warn("render: OpenGL: core context without vertex array objects");
        ok = false;
    }
    ok = ok && linkProgram(m_resolve, shaders::kGlslResolveFrag, {"u_index", "u_palette"}) &&
         linkProgram(m_present, shaders::kGlslPresentFrag, {"u_image", "u_overlay"});
    if (ok) {
        static const GLfloat kTriangle[6] = {0, 0, 2, 0, 0, 2}; // one triangle covering the viewport
        if (isCore(m_attempt)) {
            m_gl.GenVertexArrays(1, &m_vao);
            m_gl.BindVertexArray(m_vao);
        }
        m_gl.GenBuffers(1, &m_vbo);
        m_gl.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
        m_gl.BufferData(GL_ARRAY_BUFFER, sizeof kTriangle, kTriangle, GL_STATIC_DRAW);
        m_gl.EnableVertexAttribArray(0);
        m_gl.VertexAttribPointer(0, 2, GL_FLOAT, 0, 0, nullptr);

        m_gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        m_gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
        m_texIndex = makeTexture(GL_NEAREST);
        m_texPalette = makeTexture(GL_NEAREST);
        static const std::uint8_t kBlackPalette[256 * 4] = {};
        m_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, kBlackPalette);
        m_texOverlay = makeTexture(GL_NEAREST);
        allocR8(m_texOverlay, kOverlayTexW, kOverlayTexH);
        m_texResolved = makeTexture(GL_LINEAR);
        m_gl.GenFramebuffers(1, &m_fbo);
        ok = createSource(640, 480);
    }
    if (ok) {
        m_gl.Disable(GL_DEPTH_TEST);
        m_gl.Disable(GL_CULL_FACE);
        m_gl.Disable(GL_SCISSOR_TEST);
        if (GLenum e = m_gl.GetError())
            log::warn("render: OpenGL: error {:#x} during setup", e);
        auto str = [this](GLenum what) {
            const GLubyte* s = m_gl.GetString(what);
            return s ? std::string(reinterpret_cast<const char*>(s)) : std::string("?");
        };
        m_deviceName = str(GL_RENDERER);
        log::info("render: OpenGL {} on {} ({}, {})", m_attempt->label, m_deviceName, str(GL_VENDOR),
                  str(GL_VERSION));
        SDL_GL_MakeCurrent(window, nullptr); // the render thread takes the context
        return true;
    }
    destroy();
    return false;
}

void OpenGLBackend::destroy() {
    if (!m_context)
        return;
    SDL_GL_MakeCurrent(m_window, m_context);
    freeObjects();
    SDL_GL_MakeCurrent(m_window, nullptr);
    SDL_GL_DestroyContext(m_context);
    m_context = nullptr;
    m_attempt = nullptr;
}

void OpenGLBackend::setUniforms(const Program& p, const float src[4], const float out[4], const float uv[4],
                                const float params[4]) {
    if (p.srcSize >= 0)
        m_gl.Uniform4f(p.srcSize, src[0], src[1], src[2], src[3]);
    if (p.outSize >= 0)
        m_gl.Uniform4f(p.outSize, out[0], out[1], out[2], out[3]);
    if (p.uvRect >= 0)
        m_gl.Uniform4f(p.uvRect, uv[0], uv[1], uv[2], uv[3]);
    if (p.params >= 0)
        m_gl.Uniform4f(p.params, params[0], params[1], params[2], params[3]);
}

DrawResult OpenGLBackend::draw(const FrameData& f) {
    const int outW = f.outW;
    const int outH = f.outH;
    if (outW <= 0 || outH <= 0)
        return DrawResult::Skipped;
    int want = f.vsync ? 1 : 0;
    if (want != m_swapInterval) {
        // Prefer adaptive vsync where available (late frames tear instead of stuttering).
        if (!(want && SDL_GL_SetSwapInterval(-1)))
            SDL_GL_SetSwapInterval(want);
        m_swapInterval = want;
    }
    if ((f.srcW != m_srcW || f.srcH != m_srcH) && !createSource(f.srcW, f.srcH))
        return DrawResult::Lost;
    const bool core = isCore(m_attempt);
    if (core) {
        m_gl.BindVertexArray(m_vao);
    } else {
        m_gl.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
        m_gl.EnableVertexAttribArray(0);
        m_gl.VertexAttribPointer(0, 2, GL_FLOAT, 0, 0, nullptr);
    }

    const float src[4] = {static_cast<float>(m_srcW), static_cast<float>(m_srcH),
                          1.0f / static_cast<float>(m_srcW), 1.0f / static_cast<float>(m_srcH)};
    const float uv[4] = {0, 0, 1, 1};
    // The default framebuffer is not sRGB-encoding (GL_FRAMEBUFFER_SRGB stays off).
    const float params[4] = {static_cast<float>(static_cast<int>(f.filter)), 1.0f, 0.0f, 0.0f};

    // ---- palette resolve at source resolution
    if (f.frameSeq && f.index && f.frameSeq != m_uploadedSeq) {
        m_gl.ActiveTexture(GL_TEXTURE0);
        m_gl.BindTexture(GL_TEXTURE_2D, m_texIndex);
        m_gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_srcW, m_srcH, core ? GL_RED : GL_LUMINANCE,
                           GL_UNSIGNED_BYTE, f.index);
        m_gl.ActiveTexture(GL_TEXTURE1);
        m_gl.BindTexture(GL_TEXTURE_2D, m_texPalette);
        m_gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_BGRA, GL_UNSIGNED_BYTE, f.palette);
        m_gl.BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        m_gl.Viewport(0, 0, m_srcW, m_srcH);
        m_gl.Disable(GL_BLEND);
        m_gl.UseProgram(m_resolve.prog);
        setUniforms(m_resolve, src, src, uv, params);
        m_gl.DrawArrays(GL_TRIANGLES, 0, 3);
        m_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        m_uploadedSeq = f.frameSeq;
    }
    if (f.overlay && f.overlaySeq && f.overlaySeq != m_overlaySeq) {
        m_gl.ActiveTexture(GL_TEXTURE1);
        m_gl.BindTexture(GL_TEXTURE_2D, m_texOverlay);
        m_gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kOverlayTexW, kOverlayTexH, core ? GL_RED : GL_LUMINANCE,
                           GL_UNSIGNED_BYTE, f.overlay);
        m_overlaySeq = f.overlaySeq;
    }

    // ---- scale to the window
    m_gl.Viewport(0, 0, outW, outH);
    m_gl.ClearColor(0, 0, 0, 1);
    m_gl.Clear(GL_COLOR_BUFFER_BIT);
    m_gl.UseProgram(m_present.prog);
    m_gl.ActiveTexture(GL_TEXTURE0);
    m_gl.BindTexture(GL_TEXTURE_2D, m_texResolved);
    m_gl.ActiveTexture(GL_TEXTURE1);
    m_gl.BindTexture(GL_TEXTURE_2D, m_texOverlay);
    Rect view = computeView(outW, outH, m_srcW, m_srcH, f.aspect);
    if (!view.empty() && m_uploadedSeq) {
        const float out[4] = {static_cast<float>(view.w), static_cast<float>(view.h),
                              1.0f / static_cast<float>(view.w), 1.0f / static_cast<float>(view.h)};
        viewport(view, outH);
        setUniforms(m_present, src, out, uv, params);
        m_gl.DrawArrays(GL_TRIANGLES, 0, 3);
    }
    if (f.overlayAlpha > 0.0f && m_overlaySeq) {
        Rect o = overlayRect(outW, outH, f.overlayW, f.overlayH);
        if (!o.empty()) {
            const float out[4] = {static_cast<float>(o.w), static_cast<float>(o.h),
                                  1.0f / static_cast<float>(o.w), 1.0f / static_cast<float>(o.h)};
            const float ouv[4] = {0, 0, static_cast<float>(f.overlayW) / kOverlayTexW,
                                  static_cast<float>(f.overlayH) / kOverlayTexH};
            const float op[4] = {kModeOverlay, f.overlayAlpha, 0.0f, 0.0f};
            m_gl.Enable(GL_BLEND);
            m_gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            viewport(o, outH);
            setUniforms(m_present, src, out, ouv, op);
            m_gl.DrawArrays(GL_TRIANGLES, 0, 3);
            m_gl.Disable(GL_BLEND);
        }
    }

    // ---- optional screenshot of the output (bottom-up in GL)
    if (f.capture) {
        Capture& cap = *f.capture;
        std::size_t row = static_cast<std::size_t>(outW) * 4;
        std::vector<std::uint8_t> raw(row * static_cast<std::size_t>(outH));
        m_gl.ReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
        cap.width = outW;
        cap.height = outH;
        cap.rgba.resize(raw.size());
        for (int y = 0; y < outH; ++y)
            std::memcpy(cap.rgba.data() + static_cast<std::size_t>(y) * row,
                        raw.data() + static_cast<std::size_t>(outH - 1 - y) * row, row);
        for (std::size_t i = 3; i < cap.rgba.size(); i += 4)
            cap.rgba[i] = 255;
        cap.ok = true;
    }
    SDL_GL_SwapWindow(m_window);

    if (GLenum e = m_gl.GetError()) {
        if (e == GL_CONTEXT_LOST) {
            log::error("render: OpenGL: the context was lost");
            return DrawResult::Lost;
        }
        if (!m_reportedError) {
            log::warn("render: OpenGL: error {:#x} while drawing", e);
            m_reportedError = true;
        }
    }
    return DrawResult::Ok;
}

std::unique_ptr<Backend> createOpenGLBackend() {
    return std::make_unique<OpenGLBackend>();
}

} // namespace brc::render
