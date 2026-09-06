#pragma once

#include "render/core/render_styles.h"
#include "render/core/shader_program.h"

#include <GLES2/gl2.h>
#include <array>
#include <cstdint>

class TextureId;

// Whole-surface CRT post-process: resamples an offscreen copy of the scene with
// per-row static jitter, occasional band displacement, and a card-limited
// scanline + vignette pass. Loosely follows the "Interfere" stage of
// https://www.shadertoy.com/view/lfscD7, heavily toned down.
class CrtPostProgram {
public:
  void ensureInitialized();
  void destroy();
  void abandon() noexcept;

  // Draw srcTex (the scene rendered into a framebuffer of bufferWidth x
  // bufferHeight) to the currently bound framebuffer as a fullscreen quad.
  void draw(
      TextureId srcTex, std::uint32_t bufferWidth, std::uint32_t bufferHeight, float logicalWidth,
      float logicalHeight, const ScenePostEffect& effect
  ) const;

private:
  ShaderProgram m_program;
  GLint m_posLoc = -1;
  GLint m_texLoc = -1;
  GLint m_logicalSizeLoc = -1;
  GLint m_rectLoc = -1;
  GLint m_radiusLoc = -1;
  GLint m_timeLoc = -1;
  std::array<GLint, 5> m_paramLocs{-1, -1, -1, -1, -1};
};
