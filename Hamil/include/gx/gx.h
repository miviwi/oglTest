#pragma once

#include <common.h>

#include <GL/gl3w.h>

#include <array>

namespace gx {

enum Component {
  Zero = GL_ZERO, One = GL_ONE,
  Red = GL_RED, Green = GL_GREEN, Blue  = GL_BLUE, Alpha = GL_ALPHA,
};

enum Format {
  r = GL_RED, rg = GL_RG, rgb = GL_RGB, rgba = GL_RGBA,
  depth = GL_DEPTH_COMPONENT, depth_stencil = GL_DEPTH_STENCIL,

  r8 = GL_R8, r16 = GL_R16,
  rgb8 = GL_RGB8, rgb565 = GL_RGB565,
  rgb5a1 = GL_RGB5_A1, rgba8 = GL_RGBA8,

  depth16 = GL_DEPTH_COMPONENT16,
  depth24 = GL_DEPTH_COMPONENT24,
  depth32 = GL_DEPTH_COMPONENT32, depthf = GL_DEPTH_COMPONENT32F,
  depth24_stencil8 = GL_DEPTH24_STENCIL8,
};

enum Type {
  i8  = GL_BYTE,  u8  = GL_UNSIGNED_BYTE,
  i16 = GL_SHORT, u16 = GL_UNSIGNED_SHORT,
  i32 = GL_INT,   u32 = GL_UNSIGNED_INT,

  f16 = GL_HALF_FLOAT, f32 = GL_FLOAT, f64 = GL_DOUBLE,
  fixed = GL_FIXED,

  u16_565 = GL_UNSIGNED_SHORT_5_6_5, u16_5551 = GL_UNSIGNED_SHORT_5_5_5_1,
  u32_8888 = GL_UNSIGNED_INT_8_8_8_8,
};

enum Face {
  PosX = GL_TEXTURE_CUBE_MAP_POSITIVE_X, NegX = GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
  PosY = GL_TEXTURE_CUBE_MAP_POSITIVE_Y, NegY = GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
  PosZ = GL_TEXTURE_CUBE_MAP_POSITIVE_Z, NegZ = GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
};

// Ordered according to CubeMap FBO layer indices
static constexpr std::array<Face, 6> Faces = {
  PosX, NegX,
  PosY, NegY,
  PosZ, NegZ,
};

bool is_color_format(Format fmt);

// must be called AFTER creating a win32::Window!
void init();
void finalize();

void p_bind_VertexArray(unsigned array);
unsigned p_unbind_VertexArray();

}