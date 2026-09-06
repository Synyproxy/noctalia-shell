#include "render/programs/crt_post_program.h"

#include "render/core/texture_handle.h"

#include <stdexcept>

namespace {

  constexpr char kVertexShader[] = R"(
precision highp float;
attribute vec2 a_position;
varying vec2 v_uv;

void main() {
    v_uv = a_position;
    vec2 ndc = a_position * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

  // v_uv is top-left based (window convention); the offscreen scene texture is
  // GL bottom-up, so every sample flips Y. Colours are premultiplied throughout:
  // only ever scale rgb by <= 1 or clamp it to alpha when brightening.
  constexpr char kFragmentShader[] = R"(
precision highp float;
uniform sampler2D u_texture;
uniform vec2 u_texel_size;
uniform vec2 u_logical_size;
uniform vec4 u_rect;
uniform float u_radius;
uniform float u_time;
uniform float u_intensity;
varying vec2 v_uv;

float hash1(float n) {
    return fract(sin(n * 127.1) * 43758.5453);
}

float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

float roundedBoxSDF(vec2 center, vec2 halfSize, float radius) {
    vec2 q = abs(center) - halfSize + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

vec4 sampleScene(vec2 uv) {
    uv = clamp(uv, 0.0, 1.0);
    return texture2D(u_texture, vec2(uv.x, 1.0 - uv.y));
}

float orderedDither(vec2 p) {
    float x = mod(floor(p.x), 4.0);
    float y = mod(floor(p.y), 4.0);
    float idx = x + y * 4.0;
    float v = 0.0;
    if (idx == 0.0) v = 0.0; else if (idx == 1.0) v = 8.0; else if (idx == 2.0) v = 2.0; else if (idx == 3.0) v = 10.0;
    else if (idx == 4.0) v = 12.0; else if (idx == 5.0) v = 4.0; else if (idx == 6.0) v = 14.0; else if (idx == 7.0) v = 6.0;
    else if (idx == 8.0) v = 3.0; else if (idx == 9.0) v = 11.0; else if (idx == 10.0) v = 1.0; else if (idx == 11.0) v = 9.0;
    else if (idx == 12.0) v = 15.0; else if (idx == 13.0) v = 7.0; else if (idx == 14.0) v = 13.0; else v = 5.0;
    return (v + 1.0) / 17.0;
}

void main() {
    float k = clamp(u_intensity, 0.0, 1.0);
    vec2 uv = v_uv;
    vec2 px = uv * u_logical_size;

    // Everything hashes off a nominal 60 Hz frame counter so the glitches step
    // instead of sliding, like the original's iFrame-driven interference.
    float frame = floor(u_time * 60.0);
    float slowFrame = floor(frame / 10.0);
    float frameHash = hash1(slowFrame + 0.5);

    // --- Static: a few 2px rows jitter sideways at ~24 Hz; every now and then a
    // whole frame fills with it.
    float staticP = 0.012;
    float staticMag = 0.005;
    if (frameHash < 0.05) {
        staticP = 0.3;
        staticMag = 0.012;
    }
    float row = floor(px.y / 2.0);
    float rowFrame = floor(frame / 2.5);
    float shift = 0.0;
    if (hash2(vec2(row, rowFrame)) < staticP) {
        float mag = hash2(vec2(rowFrame, row)) * 2.0 - 1.0;
        shift -= staticMag * sign(mag) * mag * mag;
    }

    // --- Band displacement: either the lower part of the frame slips vertically
    // or a thin horizontal band tears sideways.
    vec2 uvS = uv;
    uvS.x += shift;
    bool displaced = false;
    if (frameHash > 0.965) {
        float dispX = hash1(slowFrame + 8783.0);
        float dispY = hash1(floor(frame / 12.0) + 364719.0);
        if (uv.y < dispX) {
            uvS.y -= (dispY * 2.0 - 1.0) * 0.05;
            displaced = true;
        }
    } else if (frameHash > 0.875) {
        float dispX = hash1(floor(frame / 9.0) + 147251.0);
        float dispY = hash1(floor(frame / 11.0) + 287512.0);
        float dispZ = hash1(floor(frame / 7.0) + 8756123.0);
        if (uv.y > dispX && uv.y < dispX + dispZ * 0.08) {
            uvS.x -= (dispY * 2.0 - 1.0) * 0.1;
            displaced = true;
        }
    }
    uvS = mix(uv, uvS, k);

    vec4 col;
    if (displaced && k > 0.0) {
        // Colour fringe, posterise with an ordered dither, and lift the band.
        float ox = 1.5 / u_logical_size.x;
        vec4 cr = sampleScene(uvS + vec2(ox, 0.0));
        vec4 cg = sampleScene(uvS);
        vec4 cb = sampleScene(uvS - vec2(ox, 0.0));
        col = vec4(cr.r, cg.g, cb.b, cg.a);
        float dither = orderedDither(px);
        vec3 q = floor(col.rgb * 6.0 + dither) / 6.0;
        col.rgb = mix(col.rgb, q, k);
        col.rgb = min(col.rgb * (1.0 + 0.2 * k), vec3(col.a));
    } else {
        col = sampleScene(uvS);
    }

    // --- Card-only shading: scanlines and a soft vignette clipped to the rounded
    // panel rect so the shadow around it stays clean.
    vec2 halfSize = u_rect.zw * 0.5;
    vec2 center = px - (u_rect.xy + halfSize);
    float inCard = 0.0;
    float vignette = 1.0;
    if (halfSize.x > 0.0 && halfSize.y > 0.0) {
        float d = roundedBoxSDF(center, halfSize, u_radius);
        inCard = 1.0 - smoothstep(-1.0, 0.0, d);
        vec2 n = center / halfSize;
        float dist = length(n) / 1.41421356;
        vignette = mix(1.0, max(0.0, 1.0 - pow(dist * 0.6, 3.0)), 0.5 * k);
    }
    float scan = mod(floor(gl_FragCoord.y), 2.0) < 1.0 ? 1.0 : 1.0 - 0.06 * k;
    float shade = mix(1.0, scan * vignette, inCard);
    col.rgb *= shade;

    gl_FragColor = col;
}
)";

} // namespace

void CrtPostProgram::ensureInitialized() {
  if (m_program.isValid()) {
    return;
  }

  m_program.create(kVertexShader, kFragmentShader);

  const auto id = m_program.id();
  m_posLoc = glGetAttribLocation(id, "a_position");
  m_texLoc = glGetUniformLocation(id, "u_texture");
  m_texelSizeLoc = glGetUniformLocation(id, "u_texel_size");
  m_logicalSizeLoc = glGetUniformLocation(id, "u_logical_size");
  m_rectLoc = glGetUniformLocation(id, "u_rect");
  m_radiusLoc = glGetUniformLocation(id, "u_radius");
  m_timeLoc = glGetUniformLocation(id, "u_time");
  m_intensityLoc = glGetUniformLocation(id, "u_intensity");

  if (m_posLoc < 0 || m_texLoc < 0 || m_logicalSizeLoc < 0 || m_rectLoc < 0 || m_timeLoc < 0) {
    throw std::runtime_error("failed to query crt post shader locations");
  }
}

void CrtPostProgram::destroy() { m_program.destroy(); }

void CrtPostProgram::abandon() noexcept { m_program.abandon(); }

void CrtPostProgram::draw(
    TextureId srcTex, std::uint32_t bufferWidth, std::uint32_t bufferHeight, float logicalWidth, float logicalHeight,
    const ScenePostEffect& effect
) const {
  if (!m_program.isValid() || srcTex == 0 || bufferWidth == 0 || bufferHeight == 0) {
    return;
  }

  static constexpr float kQuad[] = {
      0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F, 1.0F,
  };

  glUseProgram(m_program.id());

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(srcTex.value()));
  glUniform1i(m_texLoc, 0);

  if (m_texelSizeLoc >= 0) {
    glUniform2f(m_texelSizeLoc, 1.0F / static_cast<float>(bufferWidth), 1.0F / static_cast<float>(bufferHeight));
  }
  glUniform2f(m_logicalSizeLoc, logicalWidth, logicalHeight);
  // A zero rect means "whole surface": the card geometry is not known yet.
  const bool hasRect = effect.rectWidth > 0.0F && effect.rectHeight > 0.0F;
  glUniform4f(
      m_rectLoc, hasRect ? effect.rectX : 0.0F, hasRect ? effect.rectY : 0.0F, hasRect ? effect.rectWidth : logicalWidth,
      hasRect ? effect.rectHeight : logicalHeight
  );
  if (m_radiusLoc >= 0) {
    glUniform1f(m_radiusLoc, effect.radius);
  }
  glUniform1f(m_timeLoc, effect.time);
  if (m_intensityLoc >= 0) {
    glUniform1f(m_intensityLoc, effect.intensity);
  }

  auto posAttr = static_cast<GLuint>(m_posLoc);
  glVertexAttribPointer(posAttr, 2, GL_FLOAT, GL_FALSE, 0, kQuad);
  glEnableVertexAttribArray(posAttr);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(posAttr);
}
