#include "TiltPassElement.hpp"
#include "OverviewRender.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

#include <array>
#include <cmath>

namespace {

// Clip-space corners (with w, for perspective-correct texturing) and the
// scratch framebuffer UVs they came from.
const char* TILT_VERT = R"GLSL(#version 300 es
precision highp float;
layout(location = 0) in vec4 a_clip;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() {
    gl_Position = a_clip;
    v_uv        = a_uv;
}
)GLSL";

// The scratch framebuffer holds premultiplied colour, like everything else.
const char* TILT_FRAG = R"GLSL(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D tex;
out vec4 fragColor;
void main() {
    fragColor = texture(tex, v_uv);
}
)GLSL";

SP<CShader>              s_shader;
GLint                    s_texLoc = -1;
GLuint                   s_vao = 0, s_vbo = 0;
bool                     s_failed = false;
SP<Render::IFramebuffer> s_scratch;
UP<Hyprutils::Utils::CScopeGuard> s_restore; // rebinds the real target
bool                     s_active = false;

bool ensureShader() {
    if (s_shader)
        return true;
    if (s_failed)
        return false;

    auto shader = makeShared<CShader>();
    if (!shader->createProgram(TILT_VERT, TILT_FRAG, true, true)) {
        s_failed = true;
        HyprlandAPI::addNotification(SCROLLOVERVIEW_HANDLE, "[hlsovpp] tilt shader failed to compile; tilt is off", CHyprColor{1.0, 0.2, 0.2, 1.0}, 6000);
        return false;
    }

    s_texLoc = glGetUniformLocation(shader->program(), "tex");
    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    s_shader = shader;
    return true;
}

// Monitor px -> clip space, using Hyprland's own projection for the target.
struct SProjector {
    std::array<float, 9> m;
    Vector2D             size;

    Vector2D clip(const Vector2D& px) const {
        const double U = px.x / size.x, V = px.y / size.y;
        return {m[0] * U + m[1] * V + m[2], m[3] * U + m[4] * V + m[5]};
    }
};

SProjector projector(PHLMONITOR monitor) {
    const Vector2D SIZE = monitor->m_transformedSize;
    return {g_pHyprRenderer->projectBoxToTarget(CBox{{}, SIZE}).getMatrix(), SIZE};
}

}

bool TiltRender::begin(PHLMONITOR monitor) {
    if (s_active || !monitor || monitor->m_transform != WL_OUTPUT_TRANSFORM_NORMAL || !ensureShader())
        return false;

    const auto TARGET = g_pHyprRenderer->m_renderData.currentFB;
    if (!TARGET || TARGET->m_size.x < 1 || TARGET->m_size.y < 1)
        return false;

    // Whatever is queued belongs to the real target.
    OverviewRender::flushPass(monitor);

    if (!s_scratch)
        s_scratch = g_pHyprRenderer->createFB("hlsovpp-tilt");
    if (s_scratch->m_size != TARGET->m_size || s_scratch->m_drmFormat != TARGET->m_drmFormat)
        s_scratch->alloc(sc<int>(TARGET->m_size.x), sc<int>(TARGET->m_size.y), TARGET->m_drmFormat);
    if (!s_scratch->getTexture())
        return false;

    // Colour-managed shaders (shadow, border, surfaces) read the bound
    // target's image description; a fresh framebuffer has none.
    auto desc = TARGET->imageDescription();
    if (!desc)
        desc = g_pHyprRenderer->workBufferImageDescription();
    if (!desc)
        return false;
    s_scratch->setImageDescription(desc);

    s_restore = g_pHyprRenderer->bindTempFB(s_scratch);
    Render::GL::g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);
    glClearColor(0.F, 0.F, 0.F, 0.F);
    glClear(GL_COLOR_BUFFER_BIT);
    s_active = true;
    return true;
}

void TiltRender::end(PHLMONITOR monitor, const CBox& card, const Vector2D& axis, float angle) {
    if (!s_active)
        return;

    OverviewRender::flushPass(monitor); // anything still queued goes into the scratch image
    s_restore.reset();                  // back to the real target
    s_active = false;

    g_pHyprRenderer->m_renderPass.add(makeUnique<CTiltPassElement>(CTiltPassElement::SData{.card = card, .axis = axis, .angle = angle}));
    OverviewRender::flushPass(monitor); // draw now: the next tilted window reuses the scratch image
}

void TiltRender::destroy() {
    if (s_shader)
        s_shader->destroy();
    s_shader.reset();
    if (s_vbo)
        glDeleteBuffers(1, &s_vbo);
    if (s_vao)
        glDeleteVertexArrays(1, &s_vao);
    s_vao = s_vbo = 0;
    s_restore.reset();
    s_scratch.reset();
    s_active = false;
    s_failed = false;
}

CTiltPassElement::CTiltPassElement(const SData& data) : m_data(data) {}

namespace {

struct SCorner {
    Vector2D screen; // monitor px after tilt + perspective
    float    w = 1.F;
};

// Rotate a card corner about `axis` through the card centre, then project
// with a pinhole camera `focal` px in front of the screen. The leading edge
// of the travel recedes (positive z = away from the viewer).
SCorner tiltCorner(const Vector2D& center, const Vector2D& corner, const Vector2D& axis, float angle, float focal) {
    const double RX = corner.x - center.x, RY = corner.y - center.y;
    const double C = std::cos(angle), S = std::sin(angle);
    const double DOT = axis.x * RX + axis.y * RY;

    const double X = RX * C + axis.x * DOT * (1.0 - C);
    const double Y = RY * C + axis.y * DOT * (1.0 - C);
    const double Z = -(axis.x * RY - axis.y * RX) * S;

    const double DEPTH = std::max(focal + Z, focal * 0.1);
    const double SCALE = focal / DEPTH;
    return {{center.x + X * SCALE, center.y + Y * SCALE}, sc<float>(DEPTH / focal)};
}

std::array<SCorner, 4> tiltCorners(PHLMONITOR monitor, const CBox& card, const Vector2D& axis, float angle) {
    const float    FOCAL  = sc<float>(monitor->m_transformedSize.y * 1.5);
    const Vector2D CENTER = card.middle();
    // strip order: top-left, top-right, bottom-left, bottom-right
    return {
        tiltCorner(CENTER, {card.x, card.y}, axis, angle, FOCAL),
        tiltCorner(CENTER, {card.x + card.w, card.y}, axis, angle, FOCAL),
        tiltCorner(CENTER, {card.x, card.y + card.h}, axis, angle, FOCAL),
        tiltCorner(CENTER, {card.x + card.w, card.y + card.h}, axis, angle, FOCAL),
    };
}

}

std::optional<CBox> CTiltPassElement::boundingBox() {
    const auto MONITOR = g_pHyprRenderer->m_renderData.pMonitor.lock();
    if (!MONITOR)
        return std::nullopt;

    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (const auto& corner : tiltCorners(MONITOR, m_data.card, m_data.axis, m_data.angle)) {
        minX = std::min(minX, corner.screen.x);
        minY = std::min(minY, corner.screen.y);
        maxX = std::max(maxX, corner.screen.x);
        maxY = std::max(maxY, corner.screen.y);
    }
    // logical, monitor-relative
    return CBox{minX, minY, maxX - minX, maxY - minY}.scale(1.0 / MONITOR->m_scale).expand(2);
}

std::vector<UP<IPassElement>> CTiltPassElement::draw() {
    const auto MONITOR = g_pHyprRenderer->m_renderData.pMonitor.lock();
    if (!MONITOR || !s_shader || !s_scratch || !s_scratch->getTexture())
        return {};

    const auto PROJ    = projector(MONITOR);
    const auto CORNERS = tiltCorners(MONITOR, m_data.card, m_data.axis, m_data.angle);
    const std::array<Vector2D, 4> FLAT = {
        Vector2D{m_data.card.x, m_data.card.y},
        Vector2D{m_data.card.x + m_data.card.w, m_data.card.y},
        Vector2D{m_data.card.x, m_data.card.y + m_data.card.h},
        Vector2D{m_data.card.x + m_data.card.w, m_data.card.y + m_data.card.h},
    };

    // a_clip (vec4) + a_uv (vec2) per corner. A framebuffer texel maps
    // straight to clip space, so the flat corner's clip position gives its UV
    // whatever orientation Hyprland renders with.
    std::array<float, 24> vertices{};
    for (size_t i = 0; i < 4; ++i) {
        const auto CLIP = PROJ.clip(CORNERS[i].screen);
        const auto UV   = (PROJ.clip(FLAT[i]) + Vector2D{1.0, 1.0}) * 0.5;
        const auto W    = CORNERS[i].w;
        vertices[i * 6 + 0] = sc<float>(CLIP.x) * W;
        vertices[i * 6 + 1] = sc<float>(CLIP.y) * W;
        vertices[i * 6 + 2] = 0.F;
        vertices[i * 6 + 3] = W;
        vertices[i * 6 + 4] = sc<float>(UV.x);
        vertices[i * 6 + 5] = sc<float>(UV.y);
    }

    Render::GL::g_pHyprOpenGL->blend(true);
    Render::GL::g_pHyprOpenGL->useShader(s_shader);
    Render::GL::g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);

    glActiveTexture(GL_TEXTURE0);
    s_scratch->getTexture()->bind();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glUniform1i(s_texLoc, 0);

    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(4 * sizeof(float)));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return {};
}
