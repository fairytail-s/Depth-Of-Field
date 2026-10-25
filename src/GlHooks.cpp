#include "GlHooks.hpp"

#include "Config.hpp"

#include <pl/memory/Hook.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace dof {
namespace {

constexpr bool kHalfSizeColorCopy = true;

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLfloat = float;
using GLchar = char;

constexpr GLenum GL_READ_FRAMEBUFFER = 0x8CA8, GL_DRAW_FRAMEBUFFER = 0x8CA9;
constexpr GLenum GL_DRAW_FRAMEBUFFER_BINDING = 0x8CA6, GL_READ_FRAMEBUFFER_BINDING = 0x8CAA, GL_RENDERBUFFER = 0x8D41;
constexpr GLenum GL_RENDERBUFFER_BINDING = 0x8CA7, GL_RENDERBUFFER_WIDTH = 0x8D42, GL_RENDERBUFFER_HEIGHT = 0x8D43;
constexpr GLenum GL_RENDERBUFFER_INTERNAL_FORMAT = 0x8D44, GL_RENDERBUFFER_SAMPLES = 0x8CAB;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_DEPTH_ATTACHMENT = 0x8D00, GL_DEPTH_STENCIL_ATTACHMENT = 0x821A;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE0 = 0x84C0, GL_ACTIVE_TEXTURE = 0x84E0, GL_TEXTURE_BINDING_2D = 0x8069;
constexpr GLenum GL_SAMPLER_BINDING = 0x8919;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLint GL_NEAREST = 0x2600, GL_LINEAR = 0x2601, GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_TEXTURE_WIDTH = 0x1000, GL_TEXTURE_HEIGHT = 0x1001, GL_TEXTURE_INTERNAL_FORMAT = 0x1003;
constexpr GLenum GL_DEPTH_COMPONENT16 = 0x81A5, GL_DEPTH_COMPONENT24 = 0x81A6, GL_DEPTH_COMPONENT32F = 0x8CAC;
constexpr GLenum GL_DEPTH24_STENCIL8 = 0x88F0, GL_DEPTH32F_STENCIL8 = 0x8CAD;
constexpr GLenum GL_TEXTURE = 0x1702;
constexpr GLenum GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE = 0x8CD0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME = 0x8CD1,
                 GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL = 0x8CD2;
constexpr GLbitfield GL_COLOR_BUFFER_BIT = 0x4000, GL_DEPTH_BUFFER_BIT = 0x0100;
constexpr GLenum GL_NONE = 0;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11, GL_DEPTH_TEST = 0x0B71, GL_BLEND = 0x0BE2, GL_CULL_FACE = 0x0B44, GL_STENCIL_TEST = 0x0B90;
constexpr GLenum GL_DITHER = 0x0BD0, GL_RASTERIZER_DISCARD = 0x8C89, GL_DEPTH_WRITEMASK = 0x0B72, GL_COLOR_WRITEMASK = 0x0C23;
constexpr GLenum GL_VIEWPORT = 0x0BA2, GL_CURRENT_PROGRAM = 0x8B8D, GL_VERTEX_ARRAY_BINDING = 0x85B5;
constexpr GLenum GL_TRIANGLES = 0x0004, GL_VERTEX_SHADER = 0x8B31, GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_RGBA8 = 0x8058, GL_RGB8 = 0x8051, GL_RGB10_A2 = 0x8059, GL_RGBA16F = 0x881A, GL_R11F_G11F_B10F = 0x8C3A,
                 GL_RGB565 = 0x8D62, GL_SRGB8_ALPHA8 = 0x8C43;
constexpr GLenum GL_RGBA = 0x1908, GL_UNSIGNED_BYTE = 0x1401, GL_FLOAT = 0x1406, GL_PIXEL_PACK_BUFFER_BINDING = 0x88ED;
constexpr GLenum GL_STENCIL_ATTACHMENT = 0x8D20, GL_TEXTURE_COMPARE_MODE = 0x884C;
constexpr GLenum GL_SRC_ALPHA = 0x0302, GL_ONE_MINUS_SRC_ALPHA = 0x0303, GL_ZERO = 0, GL_ONE = 1, GL_FUNC_ADD = 0x8006;
constexpr GLenum GL_BLEND_DST_RGB = 0x80C8, GL_BLEND_SRC_RGB = 0x80C9, GL_BLEND_DST_ALPHA = 0x80CA, GL_BLEND_SRC_ALPHA = 0x80CB;
constexpr GLenum GL_BLEND_EQUATION_RGB = 0x8009, GL_BLEND_EQUATION_ALPHA = 0x883D;
constexpr GLenum GL_BACK = 0x0405, GL_DEPTH = 0x1802, GL_STENCIL = 0x1801, GL_SAMPLES = 0x80A9;
constexpr GLenum GL_ATTACHMENT_RED_SIZE = 0x8212, GL_ATTACHMENT_ALPHA_SIZE = 0x8215, GL_ATTACHMENT_DEPTH_SIZE = 0x8216,
                 GL_ATTACHMENT_STENCIL_SIZE = 0x8217;

struct Gl {
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    void (*DrawElements)(GLenum, GLsizei, GLenum, const void*);
    void (*DrawArrays)(GLenum, GLint, GLsizei);
    void (*DrawElementsInstanced)(GLenum, GLsizei, GLenum, const void*, GLsizei);
    void (*DrawArraysInstanced)(GLenum, GLint, GLsizei, GLsizei);
    void (*GenTextures)(GLsizei, GLuint*);
    void (*DeleteTextures)(GLsizei, const GLuint*);
    void (*BindTexture)(GLenum, GLuint);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*TexStorage2D)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
    void (*ActiveTexture)(GLenum);
    void (*GenFramebuffers)(GLsizei, GLuint*);
    void (*BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
    GLenum (*CheckFramebufferStatus)(GLenum);
    void (*GenVertexArrays)(GLsizei, GLuint*);
    void (*BindVertexArray)(GLuint);
    GLuint (*CreateShader)(GLenum);
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void (*CompileShader)(GLuint);
    void (*GetShaderiv)(GLuint, GLenum, GLint*);
    GLuint (*CreateProgram)();
    void (*AttachShader)(GLuint, GLuint);
    void (*LinkProgram)(GLuint);
    void (*GetProgramiv)(GLuint, GLenum, GLint*);
    void (*DeleteShader)(GLuint);
    GLint (*GetUniformLocation)(GLuint, const GLchar*);
    void (*UseProgram)(GLuint);
    void (*Uniform1i)(GLint, GLint);
    void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*GetIntegerv)(GLenum, GLint*);
    void (*GetBooleanv)(GLenum, GLboolean*);
    GLboolean (*IsEnabled)(GLenum);
    void (*Enable)(GLenum);
    void (*Disable)(GLenum);
    void (*DepthMask)(GLboolean);
    void (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    GLenum (*GetError)();
    void (*BindSampler)(GLuint, GLuint);
    void (*GetTexLevelParameteriv)(GLenum, GLint, GLenum, GLint*);
    void (*GetRenderbufferParameteriv)(GLenum, GLenum, GLint*);
    void (*Clear)(GLbitfield);
    void (*DrawBuffers)(GLsizei, const GLenum*);
    void (*BindRenderbuffer)(GLenum, GLuint);
    void (*GetFramebufferAttachmentParameteriv)(GLenum, GLenum, GLenum, GLint*);
    void (*GenSamplers)(GLsizei, GLuint*);
    void (*SamplerParameteri)(GLuint, GLenum, GLint);
    void (*BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
    void (*BlendEquationSeparate)(GLenum, GLenum);
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
};
Gl real{};

enum Slot { kDrawElements, kDrawArrays, kDrawElementsInstanced, kDrawArraysInstanced, kClear, kSlotCount };
std::atomic<void*> gOrig[kSlotCount];
std::atomic<void*> gSwapOrig{nullptr};
std::atomic<void*> gEglOrig{nullptr};
std::atomic_bool gActive{false};
std::atomic_bool gSwapViaGot{false};
std::atomic_bool gSwapFallbackTried{false};
void* (*gGetCurrentContext)() = nullptr;
std::atomic<unsigned> gSwaps{0};

template <class F>
F resolve(Slot slot, F fallback) {
    void* p = gOrig[slot].load(std::memory_order_acquire);
    return p ? reinterpret_cast<F>(p) : fallback;
}

struct Attach {
    GLuint object = 0;
    bool texture = false;
};
struct Prog {
    GLuint id = 0;
    GLint color = -1, depth = -1, focus = -1, src = -1, blur = -1, a = -1, b = -1, c = -1, d = -1, e = -1, f = -1;
};
struct Own {
    Prog prep, gather, comp;
    GLuint focusProgram = 0;
    GLuint vao = 0;
    GLuint blitFbo = 0;
    GLuint colorFbo = 0;
    GLuint colorTex = 0;
    int colorW = 0, colorH = 0;
    GLuint depthTex = 0;
    GLuint texA = 0, texB = 0, fboA = 0, fboB = 0;
    int lowW = 0, lowH = 0;
    GLenum interFormat = 0, interFor = 0;
    GLuint focusTex[2] = {0, 0}, focusFbo[2] = {0, 0};
    int focusCur = 0;
    bool focusReady = false;
    bool focusFailed = false;
    bool focusSeeded = false;
    double lastFocusTime = 0.0;
    GLenum colorFormat = 0, depthFormat = 0;
    int width = 0, height = 0;
    GLint fDepth = -1, fPrev = -1, fA = -1;
    GLuint depthSampler = 0, colorSampler = 0;
    bool directFailed = false;
    bool failed = false;
};
struct State {
    int contextKnown = -1;
    unsigned frame = 0;

    unsigned frameDraws = 0, depthDraws = 0, prevDepthDraws = 0;
    GLint lastDepthFbo = -1;
    unsigned long long totalDraws = 0;
    bool blurredThisFrame = false;

    bool blackMode = false;
    unsigned blackTick = 0;
    unsigned blurs = 0, skips = 0;

    Own own;
};
State& state() {
    static thread_local State s;
    return s;
}

constexpr int kInterBUnit = 11, kInterAUnit = 12, kFocusUnit = 13, kColorUnit = 14, kDepthUnit = 15;
constexpr int kUnits[5] = {kInterBUnit, kInterAUnit, kFocusUnit, kColorUnit, kDepthUnit};
constexpr int kUnitCount = 5;

const char* kVertexShader = R"GLSL(#version 300 es
uniform highp sampler2D uFocus;
uniform vec4 uA;
uniform vec4 uB;
out vec2 uv;
flat out highp float vFocus;
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    uv = p;
    float focus = uA.x;
    if (uB.z > 0.5) {
        vec4 t = textureLod(uFocus, vec2(0.5), 0.0);
        float hi = floor(t.r * 255.0 + 0.5);
        float lo = floor(t.g * 255.0 + 0.5);
        focus = clamp((hi * 256.0 + lo) / 65535.0, 0.0, 1.0) * uA.w;
    }
    vFocus = focus;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

const char* kShaderHeader = "#version 300 es\nprecision highp float;\nprecision highp int;\n";

const char* kDepthCommon = R"GLSL(
uniform highp sampler2D uDepth;
uniform vec4 uA;
uniform vec4 uB;
uniform vec4 uC;
uniform vec4 uF;
flat in highp float vFocus;

float gFocus;
float gNear;

float toBlocks(float z)
{
    if (uB.w > 0.5) z = 1.0 - z;
    const float n = 0.1;
    return n * uA.w / max(uA.w - z * (uA.w - n), 0.0001);
}

float focusDistance()
{
    return vFocus;
}

float cocAt(vec2 p)
{
    float edge = 0.0;
    if (uF.x > 0.001) {
        float aspect = uC.w / max(uC.z, 0.000001);
        vec2 q = (p - 0.5) * 2.0 * vec2(aspect, 1.0);
        float r = length(q) / length(vec2(aspect, 1.0));
        float e = smoothstep(uF.y, 1.0, r);
        edge = e * uF.x * uB.x;
    }
    gNear = 0.0;
    float z = texture(uDepth, p).r;
    float zz = uB.w > 0.5 ? 1.0 - z : z;
    float d;
    if (zz > 0.99995) {
        if (uC.y < 0.5) return 0.0;
        d = uA.w;
    } else {
        d = toBlocks(z);
    }
    float farT = clamp((d - gFocus - uA.y) / max(uA.z, 0.001), 0.0, 1.0);
    float nearT = 0.0;
    if (uC.x > 0.5) {
        float span = max((gFocus - uA.y) * 0.7, 0.5);
        nearT = clamp((gFocus - uA.y - d) / span, 0.0, 1.0);
    }
    float t = max(farT, nearT);
    float depthBlur = t * t * (3.0 - 2.0 * t) * uB.x;
    gNear = (nearT > farT && depthBlur >= edge) ? 1.0 : 0.0;
    return max(depthBlur, edge);
}
)GLSL";

const char* kPrepBody = R"GLSL(
uniform highp sampler2D uColor;
uniform vec4 uE;
in vec2 uv;
out vec4 outColor;

void main()
{
    gFocus = focusDistance();
    vec2 o = 0.25 * uE.xy;
    vec2 p0 = uv + vec2(-o.x, -o.y);
    vec2 p1 = uv + vec2(o.x, -o.y);
    vec2 p2 = uv + vec2(-o.x, o.y);
    vec2 p3 = uv + vec2(o.x, o.y);
    vec3 col;
    if (uE.w > 0.5) {
        col = texture(uColor, uv).rgb;
    } else {
        col = (texture(uColor, p0).rgb + texture(uColor, p1).rgb + texture(uColor, p2).rgb + texture(uColor, p3).rgb) * 0.25;
    }
    float c0 = cocAt(p0); float n0 = gNear;
    float c1 = cocAt(p1); float n1 = gNear;
    float c2 = cocAt(p2); float n2 = gNear;
    float c3 = cocAt(p3); float n3 = gNear;
    float nearMax = max(max(c0 * n0, c1 * n1), max(c2 * n2, c3 * n3));
    float farAvg = (c0 * (1.0 - n0) + c1 * (1.0 - n1) + c2 * (1.0 - n2) + c3 * (1.0 - n3)) * 0.25;
    bool isNear = nearMax > 0.0 && nearMax >= farAvg;
    float v = clamp((isNear ? nearMax : farAvg) / max(uB.x, 0.001), 0.0, 1.0);
    outColor = vec4(col, 0.5 + 0.5 * (isNear ? -v : v));
}
)GLSL";

const char* kGatherBody = R"GLSL(
uniform highp sampler2D uSrc;
uniform vec4 uB;
uniform vec4 uC;
uniform vec4 uE;
in vec2 uv;
out vec4 outColor;

void main()
{
    vec4 base = texture(uSrc, uv);
    float k = uB.x / max(uE.z, 0.001);
    float sg = base.a * 2.0 - 1.0;
    bool baseNear = sg < -0.02;
    float c0 = abs(sg) * k;
    bool bleed = uC.x > 0.5 && !baseNear && k > 0.5;
    float angle0 = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    const vec2 turn = vec2(-0.7373688579969029, 0.675490316182507);
    vec2 dir = vec2(cos(angle0), sin(angle0));
    float radius = c0;
    if (bleed) {
        bool anyNear = false;
        vec2 pd = dir;
        for (int i = 0; i < 8; ++i) {
            float rr = (i < 4) ? 0.9 * k : 0.45 * k;
            vec4 t = texture(uSrc, uv + pd * (rr * uE.xy));
            if (t.a < 0.48 && (1.0 - 2.0 * t.a) * k >= rr - 1.0) anyNear = true;
            pd = vec2(pd.x * turn.x - pd.y * turn.y, pd.x * turn.y + pd.y * turn.x);
        }
        if (anyNear) radius = k;
        else bleed = false;
    }
    if (radius < 0.3) {
        outColor = vec4(base.rgb, 0.0);
        return;
    }
    int n = int(clamp(4.0 + radius * 1.5, 4.0, clamp(uB.y, 4.0, 64.0)));
    vec3 acc = base.rgb;
    float wsum = 1.0;
    vec3 accN = vec3(0.0);
    float wN = 0.0;
    float cov = 0.0;
    float radiusStep = radius * inversesqrt(float(n));
    for (int i = 0; i < n; ++i) {
        float r = sqrt(float(i) + 0.5) * radiusStep;
        vec4 s = texture(uSrc, uv + dir * (r * uE.xy));
        float sgs = s.a * 2.0 - 1.0;
        bool sNear = sgs < -0.02;
        float cs = abs(sgs) * k;
        float reach = smoothstep(r - 1.0, r + 0.5, cs);
        if (baseNear) {
            float w = sNear ? reach : 1.0;
            acc += s.rgb * w;
            wsum += w;
        } else if (sNear && bleed) {
            accN += s.rgb * reach;
            wN += reach;
            cov += reach * min((k * k) / max(cs * cs, 1.0), 6.0);
        } else {
            float w = (r <= c0) ? reach : 0.0;
            acc += s.rgb * w;
            wsum += w;
        }
        dir = vec2(dir.x * turn.x - dir.y * turn.y, dir.x * turn.y + dir.y * turn.x);
    }
    vec3 own = acc / wsum;
    if (baseNear) {
        outColor = vec4(own, smoothstep(0.25, 1.5, c0 * uE.z));
        return;
    }
    if (!bleed || wN <= 0.0) {
        outColor = vec4(own, 0.0);
        return;
    }
    float a = clamp(cov / float(n), 0.0, 1.0);
    vec3 nearCol = accN / wN;
    outColor = vec4(c0 < 0.3 ? nearCol : mix(own, nearCol, a), a);
}
)GLSL";

const char* kCompositeBody = R"GLSL(
uniform highp sampler2D uBlur;
uniform vec4 uD;
in vec2 uv;
out vec4 outColor;

void main()
{
    gFocus = focusDistance();
    float c = cocAt(uv);
    float f = smoothstep(0.25, 1.5, c);
    vec4 b = vec4(0.0);
    if (f > 0.0 || uC.x > 0.5) {
        b = texture(uBlur, uv);
        if (uC.x > 0.5) f = 1.0 - (1.0 - f) * (1.0 - b.a);
    }
    if (f < 0.004) discard;
    vec3 col = b.rgb;
    float a = f;
    if (uD.x > 0.5) {
        float t = clamp(max(c, b.a * uB.x) / max(uB.x, 0.001), 0.0, 1.0) * 0.6;
        a = 1.0 - (1.0 - f) * (1.0 - t);
        col = (col * f * (1.0 - t) + vec3(1.0, 0.1, 0.1) * t) / a;
    }
    outColor = vec4(col, a);
}
)GLSL";

const char* kFocusFragmentShader = R"GLSL(#version 300 es
precision highp float;
precision highp int;
uniform highp sampler2D uDepth;
uniform highp sampler2D uPrev;
uniform vec4 uA;
out vec4 outColor;

float toBlocks(float z)
{
    if (uA.w > 0.5) z = 1.0 - z;
    const float n = 0.1;
    return n * uA.x / max(uA.x - z * (uA.x - n), 0.0001);
}

void main()
{
    float sum = 0.0;
    float cnt = 0.0;
    for (int i = -1; i <= 1; ++i) {
        for (int j = -1; j <= 1; ++j) {
            float z = texture(uDepth, vec2(0.5) + vec2(float(i), float(j)) * 0.015).r;
            float zz = uA.w > 0.5 ? 1.0 - z : z;
            if (zz < 0.99995) {
                sum += toBlocks(z);
                cnt += 1.0;
            }
        }
    }
    vec4 t = texture(uPrev, vec2(0.5));
    float prev = (floor(t.r * 255.0 + 0.5) * 256.0 + floor(t.g * 255.0 + 0.5)) / 65535.0;
    float want = cnt > 0.5 ? clamp(sum / cnt / uA.x, 0.0, 1.0) : 1.0;
    float f = uA.z > 0.5 ? want : mix(prev, want, uA.y);
    float v = floor(clamp(f, 0.0, 1.0) * 65535.0 + 0.5);
    float hi = floor(v / 256.0);
    float lo = v - hi * 256.0;
    outColor = vec4(hi / 255.0, lo / 255.0, 0.0, 1.0);
}
)GLSL";

bool compile(GLuint shader, const char* source) {
    real.ShaderSource(shader, 1, &source, nullptr);
    real.CompileShader(shader);
    GLint ok = 0;
    real.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    return ok != 0;
}

GLuint buildProgram(const char* fragmentSource) {
    const GLuint vs = real.CreateShader(GL_VERTEX_SHADER);
    const GLuint fs = real.CreateShader(GL_FRAGMENT_SHADER);
    GLuint program = 0;
    if (compile(vs, kVertexShader) && compile(fs, fragmentSource)) {
        program = real.CreateProgram();
        real.AttachShader(program, vs);
        real.AttachShader(program, fs);
        real.LinkProgram(program);
        GLint ok = 0;
        real.GetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) program = 0;
    }
    real.DeleteShader(vs);
    real.DeleteShader(fs);
    return program;
}

bool makeProg(Prog& p, const std::string& source) {
    p.id = buildProgram(source.c_str());
    if (!p.id) return false;
    p.color = real.GetUniformLocation(p.id, "uColor");
    p.depth = real.GetUniformLocation(p.id, "uDepth");
    p.focus = real.GetUniformLocation(p.id, "uFocus");
    p.src = real.GetUniformLocation(p.id, "uSrc");
    p.blur = real.GetUniformLocation(p.id, "uBlur");
    p.a = real.GetUniformLocation(p.id, "uA");
    p.b = real.GetUniformLocation(p.id, "uB");
    p.c = real.GetUniformLocation(p.id, "uC");
    p.d = real.GetUniformLocation(p.id, "uD");
    p.e = real.GetUniformLocation(p.id, "uE");
    p.f = real.GetUniformLocation(p.id, "uF");
    return true;
}

bool ensureProgram(Own& own) {
    if (own.comp.id) return true;
    const std::string head = kShaderHeader;
    Prog prep, gather, comp;
    if (!makeProg(prep, head + kDepthCommon + kPrepBody) || !makeProg(gather, head + kGatherBody) ||
        !makeProg(comp, head + kDepthCommon + kCompositeBody)) {
        return false;
    }
    own.prep = prep;
    own.gather = gather;
    own.comp = comp;
    real.GenVertexArrays(1, &own.vao);
    real.GenFramebuffers(1, &own.blitFbo);
    real.GenFramebuffers(1, &own.fboA);
    real.GenFramebuffers(1, &own.fboB);
    return true;
}

bool ensureFocus(Own& own) {
    if (own.focusReady) return true;
    if (own.focusFailed) return false;
    own.focusFailed = true;
    const GLuint program = buildProgram(kFocusFragmentShader);
    if (!program) return false;
    own.fDepth = real.GetUniformLocation(program, "uDepth");
    own.fPrev = real.GetUniformLocation(program, "uPrev");
    own.fA = real.GetUniformLocation(program, "uA");
    for (int i = 0; i < 2; ++i) {
        real.GenTextures(1, &own.focusTex[i]);
        real.ActiveTexture(GL_TEXTURE0 + kFocusUnit);
        real.BindTexture(GL_TEXTURE_2D, own.focusTex[i]);
        real.TexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1, 1);
        real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        real.GenFramebuffers(1, &own.focusFbo[i]);
        real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, own.focusFbo[i]);
        real.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, own.focusTex[i], 0);
        const GLenum status = real.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            return false;
        }
    }
    own.focusProgram = program;
    own.focusCur = 0;
    own.focusSeeded = false;
    own.focusReady = true;
    own.focusFailed = false;
    return true;
}

bool queryAttachment(const Attach& a, int& w, int& h, GLenum& format, int& samples) {
    GLint iw = 0, ih = 0, fmt = 0, ms = 0;
    if (a.texture) {
        real.ActiveTexture(GL_TEXTURE0 + kDepthUnit);
        real.BindTexture(GL_TEXTURE_2D, a.object);
        real.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &iw);
        real.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &ih);
        real.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
    } else {
        real.BindRenderbuffer(GL_RENDERBUFFER, a.object);
        real.GetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &iw);
        real.GetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &ih);
        real.GetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &fmt);
        real.GetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &ms);
    }
    samples = ms;
    w = iw;
    h = ih;
    format = static_cast<GLenum>(fmt);
    return iw > 0 && ih > 0 && fmt != 0;
}

bool depthFormatOk(GLenum f) {
    return f == GL_DEPTH_COMPONENT16 || f == GL_DEPTH_COMPONENT24 || f == GL_DEPTH_COMPONENT32F || f == GL_DEPTH24_STENCIL8 ||
           f == GL_DEPTH32F_STENCIL8;
}

bool currentAttachment(GLenum attachment, Attach& out) {
    GLint type = 0, name = 0, level = 0;
    real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    if (type != static_cast<GLint>(GL_TEXTURE) && type != static_cast<GLint>(GL_RENDERBUFFER)) return false;
    real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
    const bool isTexture = type == static_cast<GLint>(GL_TEXTURE);
    if (isTexture) {
        real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, attachment, GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL, &level);
        if (level != 0) return false;
    }
    out = {static_cast<GLuint>(name), isTexture};
    return name != 0;
}

bool colorFormatOk(GLenum f) {
    return f == GL_RGBA8 || f == GL_RGB8 || f == GL_RGB10_A2 || f == GL_RGBA16F || f == GL_R11F_G11F_B10F || f == GL_RGB565 || f == GL_SRGB8_ALPHA8;
}

bool isDepthStencil(GLenum f) { return f == GL_DEPTH24_STENCIL8 || f == GL_DEPTH32F_STENCIL8; }

bool ensureInter(Own& own, int lw, int lh, GLenum sceneColor) {
    if (own.texA && own.texB && own.lowW == lw && own.lowH == lh && own.interFor == sceneColor) return true;
    if (own.texA) real.DeleteTextures(1, &own.texA);
    if (own.texB) real.DeleteTextures(1, &own.texB);
    own.texA = own.texB = 0;
    const GLenum first = (sceneColor == GL_RGBA16F || sceneColor == GL_R11F_G11F_B10F) ? GL_RGBA16F : (sceneColor == GL_SRGB8_ALPHA8 ? GL_SRGB8_ALPHA8 : GL_RGBA8);
    const GLenum candidates[2] = {first, GL_RGBA8};
    for (int attempt = 0; attempt < (first == GL_RGBA8 ? 1 : 2); ++attempt) {
        const GLenum format = candidates[attempt];
        GLuint* texs[2] = {&own.texA, &own.texB};
        const GLuint fbos[2] = {own.fboA, own.fboB};
        const int units[2] = {kInterAUnit, kInterBUnit};
        bool complete = true;
        for (int i = 0; i < 2; ++i) {
            real.GenTextures(1, texs[i]);
            real.ActiveTexture(GL_TEXTURE0 + units[i]);
            real.BindTexture(GL_TEXTURE_2D, *texs[i]);
            real.TexStorage2D(GL_TEXTURE_2D, 1, format, lw, lh);
            real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[i]);
            real.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texs[i], 0);
            if (real.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) complete = false;
        }
        if (complete) {
            own.lowW = lw;
            own.lowH = lh;
            own.interFormat = format;
            own.interFor = sceneColor;
            return true;
        }
        real.DeleteTextures(1, &own.texA);
        real.DeleteTextures(1, &own.texB);
        own.texA = own.texB = 0;
    }
    return false;
}

void ensureSamplers(Own& own) {
    if (own.depthSampler && own.colorSampler) return;
    GLuint s[2] = {0, 0};
    real.GenSamplers(2, s);
    own.depthSampler = s[0];
    own.colorSampler = s[1];
    const GLint filters[2] = {GL_NEAREST, GL_LINEAR};
    for (int i = 0; i < 2; ++i) {
        real.SamplerParameteri(s[i], GL_TEXTURE_MIN_FILTER, filters[i]);
        real.SamplerParameteri(s[i], GL_TEXTURE_MAG_FILTER, filters[i]);
        real.SamplerParameteri(s[i], GL_TEXTURE_COMPARE_MODE, 0);
        real.SamplerParameteri(s[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        real.SamplerParameteri(s[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

bool ensureTextures(Own& own, int w, int h, GLenum depthFormat, GLenum colorFormat, int cw, int ch) {
    if (own.depthTex && own.colorTex && own.width == w && own.height == h && own.colorW == cw && own.colorH == ch && own.depthFormat == depthFormat &&
        own.colorFormat == colorFormat)
        return true;
    if (own.depthTex) real.DeleteTextures(1, &own.depthTex);
    if (own.colorTex) real.DeleteTextures(1, &own.colorTex);
    own.depthTex = own.colorTex = 0;

    real.GenTextures(1, &own.colorTex);
    real.ActiveTexture(GL_TEXTURE0 + kColorUnit);
    real.BindTexture(GL_TEXTURE_2D, own.colorTex);
    real.TexStorage2D(GL_TEXTURE_2D, 1, colorFormat, cw, ch);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    real.GenTextures(1, &own.depthTex);
    real.ActiveTexture(GL_TEXTURE0 + kDepthUnit);
    real.BindTexture(GL_TEXTURE_2D, own.depthTex);
    real.TexStorage2D(GL_TEXTURE_2D, 1, depthFormat, w, h);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    real.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const bool split = cw != w || ch != h;
    const GLenum color0 = GL_COLOR_ATTACHMENT0, none = GL_NONE;
    GLenum status = GL_FRAMEBUFFER_COMPLETE;
    real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, own.blitFbo);
    real.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, split ? 0 : own.colorTex, 0);
    real.DrawBuffers(1, split ? &none : &color0);
    real.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, isDepthStencil(depthFormat) ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                              own.depthTex, 0);
    status = real.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE && split) {
        if (!own.colorFbo) real.GenFramebuffers(1, &own.colorFbo);
        real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, own.colorFbo);
        real.DrawBuffers(1, &color0);
        real.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, own.colorTex, 0);
        status = real.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    }
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        return false;
    }
    own.width = w;
    own.height = h;
    own.colorW = cw;
    own.colorH = ch;
    own.depthFormat = depthFormat;
    own.colorFormat = colorFormat;
    return true;
}

struct Saved {
    GLint drawFbo, readFbo, program, vao, activeTexture, renderbuffer;
    GLint tex[kUnitCount], sampler[kUnitCount];
    GLint viewport[4];
    GLint blendFunc[4], blendEq[2];
    GLboolean scissor, depthTest, blend, cull, stencil, dither, discard, depthMask, colorMask[4];
};

void save(Saved& s) {
    real.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s.drawFbo);
    real.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &s.readFbo);
    real.GetIntegerv(GL_CURRENT_PROGRAM, &s.program);
    real.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vao);
    real.GetIntegerv(GL_RENDERBUFFER_BINDING, &s.renderbuffer);
    real.GetIntegerv(GL_ACTIVE_TEXTURE, &s.activeTexture);
    for (int i = 0; i < kUnitCount; ++i) {
        real.ActiveTexture(GL_TEXTURE0 + kUnits[i]);
        real.GetIntegerv(GL_TEXTURE_BINDING_2D, &s.tex[i]);
        real.GetIntegerv(GL_SAMPLER_BINDING, &s.sampler[i]);
    }
    real.GetIntegerv(GL_VIEWPORT, s.viewport);
    real.GetIntegerv(GL_BLEND_SRC_RGB, &s.blendFunc[0]);
    real.GetIntegerv(GL_BLEND_DST_RGB, &s.blendFunc[1]);
    real.GetIntegerv(GL_BLEND_SRC_ALPHA, &s.blendFunc[2]);
    real.GetIntegerv(GL_BLEND_DST_ALPHA, &s.blendFunc[3]);
    real.GetIntegerv(GL_BLEND_EQUATION_RGB, &s.blendEq[0]);
    real.GetIntegerv(GL_BLEND_EQUATION_ALPHA, &s.blendEq[1]);
    s.scissor = real.IsEnabled(GL_SCISSOR_TEST);
    s.depthTest = real.IsEnabled(GL_DEPTH_TEST);
    s.blend = real.IsEnabled(GL_BLEND);
    s.cull = real.IsEnabled(GL_CULL_FACE);
    s.stencil = real.IsEnabled(GL_STENCIL_TEST);
    s.dither = real.IsEnabled(GL_DITHER);
    s.discard = real.IsEnabled(GL_RASTERIZER_DISCARD);
    real.GetBooleanv(GL_DEPTH_WRITEMASK, &s.depthMask);
    real.GetBooleanv(GL_COLOR_WRITEMASK, s.colorMask);
}

void setEnabled(GLenum cap, GLboolean on) {
    if (on) real.Enable(cap);
    else real.Disable(cap);
}

void restore(const Saved& s) {
    real.UseProgram(static_cast<GLuint>(s.program));
    real.BindVertexArray(static_cast<GLuint>(s.vao));
    for (int i = 0; i < kUnitCount; ++i) {
        real.ActiveTexture(GL_TEXTURE0 + kUnits[i]);
        real.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(s.tex[i]));
        real.BindSampler(kUnits[i], static_cast<GLuint>(s.sampler[i]));
    }
    real.ActiveTexture(static_cast<GLenum>(s.activeTexture));
    real.BindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(s.renderbuffer));
    real.BindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(s.readFbo));
    real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(s.drawFbo));
    real.Viewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
    real.BlendFuncSeparate(static_cast<GLenum>(s.blendFunc[0]), static_cast<GLenum>(s.blendFunc[1]), static_cast<GLenum>(s.blendFunc[2]),
                           static_cast<GLenum>(s.blendFunc[3]));
    real.BlendEquationSeparate(static_cast<GLenum>(s.blendEq[0]), static_cast<GLenum>(s.blendEq[1]));
    setEnabled(GL_SCISSOR_TEST, s.scissor);
    setEnabled(GL_DEPTH_TEST, s.depthTest);
    setEnabled(GL_BLEND, s.blend);
    setEnabled(GL_CULL_FACE, s.cull);
    setEnabled(GL_STENCIL_TEST, s.stencil);
    setEnabled(GL_DITHER, s.dither);
    setEnabled(GL_RASTERIZER_DISCARD, s.discard);
    real.DepthMask(s.depthMask);
    real.ColorMask(s.colorMask[0], s.colorMask[1], s.colorMask[2], s.colorMask[3]);
}

void noteSkip(State& st) {
    if (++st.skips >= 5 && !st.own.failed) {
        st.own.failed = true;
    }
}

void skipFrame(State& st, const Saved& saved) {
    noteSkip(st);
    restore(saved);
}

bool sourceLooksBlack(const Own& own, int lw, int lh) {
    GLint packBuffer = 0;
    real.GetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
    if (packBuffer != 0 || lw <= 0 || lh <= 0) return false;
    const bool half = own.interFormat == GL_RGBA16F;
    std::vector<float> floats(half ? static_cast<std::size_t>(lw) * 4 : 0);
    std::vector<unsigned char> bytes(half ? 0 : static_cast<std::size_t>(lw) * 4);
    real.BindFramebuffer(GL_READ_FRAMEBUFFER, own.fboA);
    int blurred = 0, black = 0;
    const int rows[3] = {lh / 4, lh / 2, (lh * 3) / 4};
    for (const int row : rows) {
        if (half) real.ReadPixels(0, row, lw, 1, GL_RGBA, GL_FLOAT, floats.data());
        else real.ReadPixels(0, row, lw, 1, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
        if (real.GetError() != 0) {
            while (real.GetError() != 0) {}
            return false;
        }
        for (int x = 0; x < lw; ++x) {
            const std::size_t i = static_cast<std::size_t>(x) * 4;
            const bool isBlurred = half ? std::fabs(floats[i + 3] - 0.5f) > 0.025f : std::abs(static_cast<int>(bytes[i + 3]) - 128) > 12;
            if (!isBlurred) continue;
            ++blurred;
            const bool isBlack = half ? (floats[i] <= 0.0f && floats[i + 1] <= 0.0f && floats[i + 2] <= 0.0f)
                                      : (bytes[i] == 0 && bytes[i + 1] == 0 && bytes[i + 2] == 0);
            if (isBlack) ++black;
        }
    }
    return blurred >= 48 && black * 100 >= blurred * 95;
}

void runBlur(State& st, GLuint sceneFbo) {
    Own& own = st.own;
    if (own.failed) return;
    if (st.blackMode && (++st.blackTick % 16) != 0) return;
    const auto& c = config();

    while (real.GetError() != 0) {}

    Saved saved{};
    save(saved);
    real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo);

    if (!ensureProgram(own)) {
        own.failed = true;
        restore(saved);
        return;
    }
    int w = 0, h = 0, depthSamples = 0, cw = 0, ch = 0, colorSamples = 0;
    GLenum depthFormat = 0, colorFormat = 0;
    Attach depthAttach, colorAttach;
    int vx = 0, vy = 0;
    if (sceneFbo == 0) {
        GLint redBits = 0, alphaBits = 0, depthBits = 0, stencilBits = 0, samples = 0;
        real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_BACK, GL_ATTACHMENT_RED_SIZE, &redBits);
        real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_BACK, GL_ATTACHMENT_ALPHA_SIZE, &alphaBits);
        real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_DEPTH, GL_ATTACHMENT_DEPTH_SIZE, &depthBits);
        real.GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_STENCIL, GL_ATTACHMENT_STENCIL_SIZE, &stencilBits);
        real.GetIntegerv(GL_SAMPLES, &samples);
        if (real.GetError() != 0) {
            skipFrame(st, saved);
            return;
        }
        if (depthBits == 0) {
            GLint db = 0, sb = 0;
            real.GetIntegerv(0x0D56  , &db);
            real.GetIntegerv(0x0D57  , &sb);
            while (real.GetError() != 0) {}
            if (db > 0) {
                depthBits = db;
                stencilBits = sb;
            } else {
                depthBits = 24;
                stencilBits = 8;
            }
        }
        vx = saved.viewport[0];
        vy = saved.viewport[1];
        w = cw = saved.viewport[2];
        h = ch = saved.viewport[3];
        depthSamples = colorSamples = samples;
        depthFormat = depthBits == 24 ? (stencilBits == 8 ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24)
                                      : (depthBits == 16 && stencilBits == 0 ? GL_DEPTH_COMPONENT16 : 0);
        colorFormat = redBits == 8 ? (alphaBits > 0 ? GL_RGBA8 : GL_RGB8) : (redBits == 5 ? GL_RGB565 : (redBits == 10 ? GL_RGB10_A2 : 0));
    } else {
        if (!currentAttachment(GL_DEPTH_ATTACHMENT, depthAttach) || !currentAttachment(GL_COLOR_ATTACHMENT0, colorAttach)) {
            skipFrame(st, saved);
            return;
        }
        const bool queried = queryAttachment(depthAttach, w, h, depthFormat, depthSamples) && queryAttachment(colorAttach, cw, ch, colorFormat, colorSamples);
        if (!queried || real.GetError() != 0) {
            skipFrame(st, saved);
            return;
        }
    }
    if (depthSamples != 0 || colorSamples != 0 || !depthFormatOk(depthFormat) || !colorFormatOk(colorFormat) || cw != w || ch != h || w < 64 || h < 64) {
        char why[160];
        std::snprintf(why, sizeof(why), "size %dx%d (colour %dx%d), samples %d/%d, depth format 0x%x, colour format 0x%x are not supported", w, h, cw, ch,
                      depthSamples, colorSamples, depthFormat, colorFormat);
        skipFrame(st, saved);
        return;
    }
    const bool wantDirect = c.directSampling.load() && !own.directFailed && sceneFbo != 0;
    bool directDepth = wantDirect && depthAttach.texture;
    bool directColor = wantDirect && colorAttach.texture;
    const int down = std::clamp(c.downscale.load(), kMinDownscale, kMaxDownscale);
    const bool halfColor = kHalfSizeColorCopy && down >= 2 && w >= 128 && h >= 128;
    const int copyW = halfColor ? (w + 1) / 2 : w, copyH = halfColor ? (h + 1) / 2 : h;
    if (!directDepth || !directColor) {
        if (!ensureTextures(own, w, h, depthFormat, colorFormat, copyW, copyH)) {
            own.failed = true;
            restore(saved);
            return;
        }
    }
    ensureSamplers(own);

    const int lw = std::max(1, (w + down - 1) / down), lh = std::max(1, (h + down - 1) / down);
    if (!ensureInter(own, lw, lh, colorFormat)) {
        own.failed = true;
        restore(saved);
        return;
    }

    Attach stencilAttach;
    bool sharedStencil = false;
    const auto attachDepth = [&](GLuint texture) {
        real.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
        if (sharedStencil) real.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
    };
    if (directDepth) {
        real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo);
        sharedStencil = currentAttachment(GL_STENCIL_ATTACHMENT, stencilAttach) && stencilAttach.texture && stencilAttach.object == depthAttach.object;
        attachDepth(0);
        const GLenum status = real.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE || real.GetError() != 0) {
            attachDepth(depthAttach.object);
            while (real.GetError() != 0) {}
            own.directFailed = true;
            skipFrame(st, saved);
            return;
        }
    }

    real.Disable(GL_SCISSOR_TEST);
    const GLbitfield copyMask = (directColor ? 0 : GL_COLOR_BUFFER_BIT) | (directDepth ? 0 : GL_DEPTH_BUFFER_BIT);
    if (copyMask != 0) {
        real.BindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo);
        if (!directColor && halfColor) {
            real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, own.colorFbo);
            real.BlitFramebuffer(vx, vy, vx + w, vy + h, 0, 0, copyW, copyH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            if (!directDepth) {
                real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, own.blitFbo);
                real.BlitFramebuffer(vx, vy, vx + w, vy + h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            }
        } else {
            real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, own.blitFbo);
            real.BlitFramebuffer(vx, vy, vx + w, vy + h, 0, 0, w, h, copyMask, GL_NEAREST);
        }
    }
    const GLuint sampledColor = directColor ? colorAttach.object : own.colorTex;
    const GLuint sampledDepth = directDepth ? depthAttach.object : own.depthTex;

    real.Disable(GL_DEPTH_TEST);
    real.Disable(GL_BLEND);
    real.Disable(GL_CULL_FACE);
    real.Disable(GL_STENCIL_TEST);
    real.Disable(GL_RASTERIZER_DISCARD);
    real.ColorMask(1, 1, 1, 1);
    real.BindVertexArray(own.vao);

    const bool wantAuto = c.autoFocus.load();
    bool autoActive = false;
    if (wantAuto && ensureFocus(own)) {
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const double dt = own.lastFocusTime > 0.0 ? std::clamp(now - own.lastFocusTime, 0.0, 0.25) : 0.0;
        own.lastFocusTime = now;
        const float rate = 1.0f - std::exp(-c.focusSpeed.load() * static_cast<float>(dt));
        const int prev = own.focusCur, next = 1 - own.focusCur;
        real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, own.focusFbo[next]);
        real.Viewport(0, 0, 1, 1);
        real.UseProgram(own.focusProgram);
        real.ActiveTexture(GL_TEXTURE0 + kDepthUnit);
        real.BindTexture(GL_TEXTURE_2D, sampledDepth);
        real.BindSampler(kDepthUnit, own.depthSampler);
        real.ActiveTexture(GL_TEXTURE0 + kFocusUnit);
        real.BindTexture(GL_TEXTURE_2D, own.focusTex[prev]);
        real.BindSampler(kFocusUnit, 0);
        real.Uniform1i(own.fDepth, kDepthUnit);
        real.Uniform1i(own.fPrev, kFocusUnit);
        real.Uniform4f(own.fA, c.farPlane.load(), rate, own.focusSeeded ? 0.0f : 1.0f, c.reversedDepth.load() ? 1.0f : 0.0f);
        real.DrawArrays(GL_TRIANGLES, 0, 3);
        own.focusCur = next;
        own.focusSeeded = true;
        autoActive = true;
    }

    real.ActiveTexture(GL_TEXTURE0 + kColorUnit);
    real.BindTexture(GL_TEXTURE_2D, sampledColor);
    real.BindSampler(kColorUnit, own.colorSampler);
    real.ActiveTexture(GL_TEXTURE0 + kDepthUnit);
    real.BindTexture(GL_TEXTURE_2D, sampledDepth);
    real.BindSampler(kDepthUnit, own.depthSampler);
    real.ActiveTexture(GL_TEXTURE0 + kInterAUnit);
    real.BindTexture(GL_TEXTURE_2D, own.texA);
    real.BindSampler(kInterAUnit, 0);
    real.ActiveTexture(GL_TEXTURE0 + kInterBUnit);
    real.BindTexture(GL_TEXTURE_2D, own.texB);
    real.BindSampler(kInterBUnit, 0);
    if (autoActive) {
        real.ActiveTexture(GL_TEXTURE0 + kFocusUnit);
        real.BindTexture(GL_TEXTURE_2D, own.focusTex[own.focusCur]);
        real.BindSampler(kFocusUnit, 0);
    }
    const float maxBlurPx = c.maxBlur.load() * static_cast<float>(h) / 1080.0f;
    const auto setCommon = [&](const Prog& p) {
        real.Uniform1i(p.color, kColorUnit);
        real.Uniform1i(p.depth, kDepthUnit);
        real.Uniform1i(p.focus, kFocusUnit);
        real.Uniform1i(p.src, kInterAUnit);
        real.Uniform1i(p.blur, kInterBUnit);
        real.Uniform4f(p.a, c.focus.load(), c.zone.load(), c.falloff.load(), c.farPlane.load());
        real.Uniform4f(p.b, maxBlurPx, static_cast<float>(c.quality.load()), autoActive ? 1.0f : 0.0f, c.reversedDepth.load() ? 1.0f : 0.0f);
        real.Uniform4f(p.c, c.nearBlur.load() ? 1.0f : 0.0f, c.blurSky.load() ? 1.0f : 0.0f, 1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h));
        real.Uniform4f(p.f, c.edgeBlur.load(), c.edgeClear.load(), 0.0f, 0.0f);
        real.Uniform4f(p.e, 1.0f / static_cast<float>(lw), 1.0f / static_cast<float>(lh), static_cast<float>(w) / static_cast<float>(lw), down <= 2 ? 1.0f : 0.0f);
    };

    real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, own.fboA);
    real.Viewport(0, 0, lw, lh);
    real.UseProgram(own.prep.id);
    setCommon(own.prep);
    real.DrawArrays(GL_TRIANGLES, 0, 3);
    const bool checkSource = st.blurs < 4 || st.blurs % 64 == 0 || st.blackMode;
    const bool black = checkSource && sourceLooksBlack(own, lw, lh);
    if (checkSource && !black) st.blackMode = false;
    if (black) {
        if (directDepth || directColor) {
            own.directFailed = true;
        } else {
            st.blackMode = true;
        }
        if (directDepth) attachDepth(depthAttach.object);
        restore(saved);
        return;
    }
    real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, own.fboB);
    real.UseProgram(own.gather.id);
    setCommon(own.gather);
    real.DrawArrays(GL_TRIANGLES, 0, 3);
    real.ActiveTexture(GL_TEXTURE0 + kColorUnit);
    real.BindTexture(GL_TEXTURE_2D, 0);
    real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo);
    real.Viewport(vx, vy, w, h);
    real.Enable(GL_BLEND);
    real.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    real.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    real.ColorMask(1, 1, 1, 0);
    real.UseProgram(own.comp.id);
    setCommon(own.comp);
    real.Uniform4f(own.comp.d, c.debugView.load() ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    real.DrawArrays(GL_TRIANGLES, 0, 3);
    if (directDepth) attachDepth(depthAttach.object);

    restore(saved);

    const GLenum error = real.GetError();
    while (real.GetError() != 0) {}
    if (error != 0 && (directDepth || directColor)) {
        own.directFailed = true;
        return;
    }
    if (error != 0) {
        own.failed = true;
        return;
    }
    st.skips = 0;
    ++st.blurs;
}

bool hasContext(State& st) {
    if (st.contextKnown < 0) st.contextKnown = gGetCurrentContext && gGetCurrentContext() ? 1 : 0;
    return st.contextKnown == 1;
}

bool tooEarly(const State& st) {
    return st.prevDepthDraws >= 20 && static_cast<unsigned long long>(st.depthDraws) * 10ull < static_cast<unsigned long long>(st.prevDepthDraws) * 7ull;
}

bool canShow(const State& st) {
    if (!gActive.load(std::memory_order_relaxed) || st.blurredThisFrame || st.own.failed) return false;
    const auto& c = config();
    if (!c.enabled.load() || st.depthDraws < static_cast<unsigned>(c.minDraws.load())) return false;
    return !tooEarly(st);
}

void triggerBlur(State& st, GLuint fbo) {
    st.blurredThisFrame = true;
    runBlur(st, fbo);
}

void endFrame(State& st);
void watchdogSwapHook(unsigned long long totalDraws);

constexpr unsigned kStuckFrameDraws = 200000;
void watchdogStuckFrame(State& st) {
    if (st.frameDraws < kStuckFrameDraws) return;
    endFrame(st);
}

void onDraw() {
    State& st = state();
    if (!hasContext(st)) return;
    if ((st.totalDraws & 4095) == 0) watchdogSwapHook(st.totalDraws);
    watchdogStuckFrame(st);
    ++st.totalDraws;
    ++st.frameDraws;
    if ((st.frameDraws & 7u) != 0) return;
    if (!real.IsEnabled(GL_DEPTH_TEST)) return;
    st.depthDraws += 8;
    GLint wfbo = 0;
    real.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &wfbo);
    st.lastDepthFbo = wfbo;
}

void clearDetour(GLbitfield mask) {
    State& st = state();
    if (hasContext(st)) {
        GLint fbo = 0;
        const bool depthClear = (mask & GL_DEPTH_BUFFER_BIT) != 0;
        if (depthClear) real.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
        if (depthClear && canShow(st)) {
            GLuint target = static_cast<GLuint>(fbo);
            if (st.lastDepthFbo >= 0 && st.lastDepthFbo != fbo) {
                target = static_cast<GLuint>(st.lastDepthFbo);
            }
            triggerBlur(st, target);
        }
    }
    resolve(kClear, real.Clear)(mask);
}

void drawElementsDetour(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    onDraw();
    resolve(kDrawElements, real.DrawElements)(mode, count, type, indices);
}
void drawArraysDetour(GLenum mode, GLint first, GLsizei count) {
    onDraw();
    resolve(kDrawArrays, real.DrawArrays)(mode, first, count);
}
void drawElementsInstancedDetour(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instances) {
    onDraw();
    resolve(kDrawElementsInstanced, real.DrawElementsInstanced)(mode, count, type, indices, instances);
}
void drawArraysInstancedDetour(GLenum mode, GLint first, GLsizei count, GLsizei instances) {
    onDraw();
    resolve(kDrawArraysInstanced, real.DrawArraysInstanced)(mode, first, count, instances);
}

void endFrame(State& st) {
    st.prevDepthDraws = st.depthDraws;
    st.frameDraws = 0;
    st.depthDraws = 0;
    st.blurredThisFrame = false;
    st.lastDepthFbo = -1;
    ++st.frame;
}

using SwapFn = unsigned (*)(void*, void*);
unsigned eglSwapBuffersDetour(void* display, void* surface) {
    gSwaps.fetch_add(1, std::memory_order_relaxed);
    endFrame(state());
    auto original = reinterpret_cast<SwapFn>(gSwapOrig.load(std::memory_order_acquire));
    return original ? original(display, surface) : 0;
}

struct HookName {
    const char* name;
    void* detour;
};
const HookName kHooks[kSlotCount] = {
    {"glDrawElements", reinterpret_cast<void*>(&drawElementsDetour)},
    {"glDrawArrays", reinterpret_cast<void*>(&drawArraysDetour)},
    {"glDrawElementsInstanced", reinterpret_cast<void*>(&drawElementsInstancedDetour)},
    {"glDrawArraysInstanced", reinterpret_cast<void*>(&drawArraysInstancedDetour)},
    {"glClear", reinterpret_cast<void*>(&clearDetour)},
};

using EglGetProcAddressFn = void* (*)(const char*);
void* eglGetProcAddressDetour(const char* name) {
    auto original = reinterpret_cast<EglGetProcAddressFn>(gEglOrig.load(std::memory_order_acquire));
    if (!original) return nullptr;
    void* address = original(name);
    if (!address || !name) return address;
    for (int i = 0; i < kSlotCount; ++i) {
        if (std::strcmp(name, kHooks[i].name) != 0) continue;
        void* expected = nullptr;
        gOrig[i].compare_exchange_strong(expected, address);
        return kHooks[i].detour;
    }
    if (!std::strcmp(name, "eglSwapBuffers")) {
        void* expected = nullptr;
        gSwapOrig.compare_exchange_strong(expected, address);
        return reinterpret_cast<void*>(&eglSwapBuffersDetour);
    }
    return address;
}

struct SlotPatch {
    void** slot;
    void* original;
    void* detour;
};
std::vector<SlotPatch> gPatches;
void* gInlineTarget = nullptr;
void* gSwapInlineTarget = nullptr;

int protectionAt(std::uintptr_t address) {
    std::FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) return PROT_READ;
    char line[4096];
    int prot = 0;
    while (std::fgets(line, sizeof(line), f)) {
        unsigned long long start = 0, end = 0;
        char perms[8] = {};
        if (std::sscanf(line, "%llx-%llx %7s", &start, &end, perms) != 3) continue;
        if (address >= start && address < end) {
            if (perms[0] == 'r') prot |= PROT_READ;
            if (perms[1] == 'w') prot |= PROT_WRITE;
            if (perms[2] == 'x') prot |= PROT_EXEC;
            break;
        }
    }
    std::fclose(f);
    return prot ? prot : PROT_READ;
}

bool writeSlot(void** slot, void* value) {
    const std::uintptr_t page = static_cast<std::uintptr_t>(::sysconf(_SC_PAGESIZE));
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(slot) & ~(page - 1);
    const int oldProtection = protectionAt(reinterpret_cast<std::uintptr_t>(slot));
    if (::mprotect(reinterpret_cast<void*>(start), page, PROT_READ | PROT_WRITE) != 0) return false;
    __atomic_store_n(slot, value, __ATOMIC_RELEASE);
    ::mprotect(reinterpret_cast<void*>(start), page, oldProtection);
    return true;
}

std::uintptr_t findModuleBase(const char* soName) {
    std::FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[4096];
    std::uintptr_t best = 0;
    while (std::fgets(line, sizeof(line), f)) {
        unsigned long long start = 0, end = 0, offset = 0;
        char perms[8] = {};
        if (std::sscanf(line, "%llx-%llx %7s %llx", &start, &end, perms, &offset) != 4) continue;
        char* path = std::strchr(line, '/');
        if (!path) continue;
        char* newline = std::strpbrk(path, "\r\n");
        if (newline) *newline = '\0';
        const char* slash = std::strrchr(path, '/');
        if (!slash || std::strcmp(slash + 1, soName) != 0) continue;
        if (offset == 0 && (best == 0 || start < best)) best = static_cast<std::uintptr_t>(start);
    }
    std::fclose(f);
    return best;
}

#if defined(__aarch64__)
constexpr unsigned kRelGlobDat = 1025;
constexpr unsigned kRelJumpSlot = 1026;
constexpr unsigned kMachine = EM_AARCH64;
#elif defined(__x86_64__)
constexpr unsigned kRelGlobDat = 6;
constexpr unsigned kRelJumpSlot = 7;
constexpr unsigned kMachine = EM_X86_64;
#else
#error "unsupported architecture"
#endif

int patchImport(std::uintptr_t base, const char* name, void* detour, std::atomic<void*>& originalOut) {
    const auto* eh = reinterpret_cast<const ElfW(Ehdr)*>(base);
    if (std::memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64 || eh->e_machine != kMachine) return 0;

    const auto* phdr = reinterpret_cast<const ElfW(Phdr)*>(base + eh->e_phoff);
    std::uintptr_t span = 0;
    const ElfW(Dyn)* dynamic = nullptr;
    for (unsigned i = 0; i < eh->e_phnum; ++i) {
        if (phdr[i].p_type == PT_LOAD) span = std::max<std::uintptr_t>(span, phdr[i].p_vaddr + phdr[i].p_memsz);
        if (phdr[i].p_type == PT_DYNAMIC) dynamic = reinterpret_cast<const ElfW(Dyn)*>(base + phdr[i].p_vaddr);
    }
    if (!dynamic) return 0;

    auto absolute = [&](std::uintptr_t v) { return (v >= base && v < base + span) ? v : base + v; };

    std::uintptr_t jmprel = 0, rela = 0, symtab = 0, strtab = 0;
    std::size_t jmprelSize = 0, relaSize = 0;
    for (const ElfW(Dyn)* d = dynamic; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_JMPREL: jmprel = absolute(d->d_un.d_ptr); break;
            case DT_PLTRELSZ: jmprelSize = d->d_un.d_val; break;
            case DT_RELA: rela = absolute(d->d_un.d_ptr); break;
            case DT_RELASZ: relaSize = d->d_un.d_val; break;
            case DT_SYMTAB: symtab = absolute(d->d_un.d_ptr); break;
            case DT_STRTAB: strtab = absolute(d->d_un.d_ptr); break;
            default: break;
        }
    }
    if (!symtab || !strtab) return 0;

    int patched = 0;
    auto scan = [&](std::uintptr_t table, std::size_t bytes) {
        if (!table || !bytes) return;
        const auto* entries = reinterpret_cast<const ElfW(Rela)*>(table);
        for (std::size_t i = 0; i < bytes / sizeof(ElfW(Rela)); ++i) {
            const unsigned type = ELF64_R_TYPE(entries[i].r_info);
            if (type != kRelGlobDat && type != kRelJumpSlot) continue;
            const auto* symbol = reinterpret_cast<const ElfW(Sym)*>(symtab) + ELF64_R_SYM(entries[i].r_info);
            const char* symbolName = reinterpret_cast<const char*>(strtab) + symbol->st_name;
            if (std::strcmp(symbolName, name) != 0) continue;

            void** slot = reinterpret_cast<void**>(base + entries[i].r_offset);
            void* current = *slot;
            if (current == detour) continue;
            void* expected = nullptr;
            originalOut.compare_exchange_strong(expected, current);
            if (writeSlot(slot, detour)) {
                gPatches.push_back({slot, current, detour});
                ++patched;
            }
        }
    };
    scan(jmprel, jmprelSize);
    scan(rela, relaSize);
    return patched;
}

bool installSwapInlineHook() {
    void* swap = ::dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (!swap) return false;
    void* original = nullptr;
    if (pl::memory::hook(swap, reinterpret_cast<void*>(&eglSwapBuffersDetour), &original) != 0 || !original) return false;
    gSwapOrig.store(original);
    gSwapInlineTarget = swap;
    return true;
}

void watchdogSwapHook(unsigned long long totalDraws) {
    if (!gSwapViaGot.load(std::memory_order_relaxed)) return;
    if (gSwaps.load(std::memory_order_relaxed) != 0) return;
    if (totalDraws < 20000) return;
    if (gSwapFallbackTried.exchange(true, std::memory_order_relaxed)) return;
    installSwapInlineHook();
}

}

bool installGlHooks() {
    if (gActive.load()) return true;

    void* gles = ::dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
    if (!gles) gles = ::dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL);
    if (!gles) return false;
    bool complete = true;
    auto load = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(::dlsym(gles, name));
        if (!fn) complete = false;
    };
#define LOAD(n) load(real.n, "gl" #n)
    LOAD(BindFramebuffer); LOAD(FramebufferTexture2D); LOAD(DrawElements); LOAD(DrawArrays);
    LOAD(DrawElementsInstanced); LOAD(DrawArraysInstanced);
    LOAD(GenTextures); LOAD(DeleteTextures); LOAD(BindTexture); LOAD(TexParameteri); LOAD(TexStorage2D); LOAD(ActiveTexture);
    LOAD(GenFramebuffers); LOAD(BlitFramebuffer); LOAD(CheckFramebufferStatus); LOAD(GenVertexArrays); LOAD(BindVertexArray);
    LOAD(CreateShader); LOAD(ShaderSource); LOAD(CompileShader); LOAD(GetShaderiv); LOAD(CreateProgram);
    LOAD(AttachShader); LOAD(LinkProgram); LOAD(GetProgramiv); LOAD(DeleteShader); LOAD(GetUniformLocation);
    LOAD(UseProgram); LOAD(Uniform1i); LOAD(Uniform4f); LOAD(GetIntegerv); LOAD(GetBooleanv); LOAD(IsEnabled); LOAD(Enable);
    LOAD(Disable); LOAD(Clear); LOAD(DrawBuffers); LOAD(DepthMask); LOAD(ColorMask); LOAD(Viewport); LOAD(GetError); LOAD(BindSampler);
    LOAD(GetTexLevelParameteriv); LOAD(GetRenderbufferParameteriv); LOAD(BindRenderbuffer); LOAD(GetFramebufferAttachmentParameteriv);
    LOAD(GenSamplers); LOAD(SamplerParameteri); LOAD(BlendFuncSeparate); LOAD(BlendEquationSeparate); LOAD(ReadPixels);
#undef LOAD
    if (!complete) return false;

    gGetCurrentContext = reinterpret_cast<void* (*)()>(::dlsym(RTLD_DEFAULT, "eglGetCurrentContext"));
    int gotSlots = 0;
    bool eglViaGot = false, swapViaGot = false;
    const std::uintptr_t base = findModuleBase("libminecraftpe.so");
    if (base) {
        for (int i = 0; i < kSlotCount; ++i) {
            gotSlots += patchImport(base, kHooks[i].name, kHooks[i].detour, gOrig[i]);
        }
        swapViaGot = patchImport(base, "eglSwapBuffers", reinterpret_cast<void*>(&eglSwapBuffersDetour), gSwapOrig) > 0;
        eglViaGot = patchImport(base, "eglGetProcAddress", reinterpret_cast<void*>(&eglGetProcAddressDetour), gEglOrig) > 0;
    }

    bool eglInline = false;
    if (!eglViaGot) {
        void* egl = ::dlsym(RTLD_DEFAULT, "eglGetProcAddress");
        if (egl) {
            void* original = nullptr;
            if (pl::memory::hook(egl, reinterpret_cast<void*>(&eglGetProcAddressDetour), &original) == 0 && original) {
                gEglOrig.store(original);
                gInlineTarget = egl;
                eglInline = true;
            }
        }
    }
    bool swapInline = false;
    if (!swapViaGot) swapInline = installSwapInlineHook();

    if (!swapViaGot && !swapInline) return false;
    if (gotSlots == 0 && !eglViaGot && !eglInline) return false;
    gSwapViaGot.store(swapViaGot);
    gActive.store(true);
    return true;
}

void removeGlHooks() {
    if (!gActive.exchange(false)) return;
    for (const SlotPatch& p : gPatches) {
        if (*p.slot == p.detour) (void)writeSlot(p.slot, p.original);
    }
    gPatches.clear();
    if (gInlineTarget) {
        (void)pl::memory::unhook(gInlineTarget, reinterpret_cast<void*>(&eglGetProcAddressDetour));
        gInlineTarget = nullptr;
    }
    if (gSwapInlineTarget) {
        (void)pl::memory::unhook(gSwapInlineTarget, reinterpret_cast<void*>(&eglSwapBuffersDetour));
        gSwapInlineTarget = nullptr;
    }
}

}
