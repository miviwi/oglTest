#pragma once

#include <gx/gx.h>
#include <math/geometry.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <initializer_list>

namespace gx {

class VertexArray;
class IndexedVertexArray;
class IndexBuffer;

class Shader {
public:
  enum Type {
    Invalid,
    Vertex   = GL_VERTEX_SHADER,
    Geometry = GL_GEOMETRY_SHADER,
    Fragment = GL_FRAGMENT_SHADER,
  };

  using SourcesList = std::vector<const char *>;

  Shader(Type type, SourcesList sources);
  Shader(Type type, const char *const sources[], size_t count);
  Shader(const Shader&) = delete;
  ~Shader();

private:
  friend class Program;

  GLuint m;
};

enum Primitive {
  Points        = GL_POINTS,

  Lines         = GL_LINES,
  LineLoop      = GL_LINE_LOOP,
  LineStrip     = GL_LINE_STRIP,

  Triangles     = GL_TRIANGLES,
  TriangleFan   = GL_TRIANGLE_FAN,
  TriangleStrip = GL_TRIANGLE_STRIP,
};

class Program {
public:
  using OffsetMap = std::unordered_map<std::string, size_t>;

  Program(const Shader& vertex, const Shader& fragment);
  Program(const Shader& vertex, const Shader& geometry, const Shader& fragment);
  Program(const Program&) = delete;
  Program(Program&& other);
  ~Program();

  Program& operator=(Program&& other);

  // Uses sources autogenerated from *.uniform files
  // in the format
  //            <uniform_name>, <uniform_name>, <uniform_name>....
  // c++ style line-comments are also allowed
  template <typename T>
  void getUniformsLocations(T& klass)
  {
    getUniforms(T::offsets.data(), T::offsets.size(), klass.locations);
  }
  GLint getUniformLocation(const char *name);

  unsigned getUniformBlockIndex(const char *name);
  void uniformBlockBinding(unsigned block, unsigned index);
  void uniformBlockBinding(const char *name, unsigned index);
  const OffsetMap& uniformBlockOffsets(unsigned block);

  int getOutputLocation(const char *name);

  Program& use();

  Program& uniformInt(int location, int i);
  Program& uniformSampler(int location, int i);
  Program& uniformFloat(int location, float f);
  Program& uniformVector3(int location, size_t size, const vec3 *v);
  Program& uniformVector3(int location, vec3 v);
  Program& uniformVector4(int location, size_t size, const vec4 *v);
  Program& uniformVector4(int location, vec4 v);
  Program& uniformMatrix4x4(int location, const mat4& mtx, bool transpose = true);
  Program& uniformMatrix3x3(int location, const mat3& mtx, bool transpose = true);
  Program& uniformMatrix3x3(int location, const mat4& mtx, bool transpose = true);
  Program& uniformBool(int location, bool v);

  void draw(Primitive p, const VertexArray& vtx, size_t offset, size_t num);
  void draw(Primitive p, const VertexArray& vtx, size_t num);
  void draw(Primitive p, const IndexedVertexArray& vtx, size_t offset, size_t num);
  void draw(Primitive p, const IndexedVertexArray& vtx, size_t num);

  void drawBaseVertex(Primitive p,
    const IndexedVertexArray& vtx, size_t base, size_t offset, size_t num);

  void label(const char *lbl);

private:
  void link();
  void getUniforms(const std::pair<std::string, unsigned> *offsets, size_t sz, int locations[]);
  void getUniformBlockOffsets();

  GLuint m;
  static const OffsetMap m_null_offsets;
  std::vector<OffsetMap> m_ubo_offsets;
};

template <typename T>
static Program make_program(
  Shader::SourcesList vs_src, Shader::SourcesList fs_src,
  T& uniforms)
{
  Shader vs(gx::Shader::Vertex,   vs_src);
  Shader fs(gx::Shader::Fragment, fs_src);

  Program prog(vs, fs);

  prog.getUniformsLocations(uniforms);

  return prog;
}

template <typename T>
static Program make_program(
  Shader::SourcesList vs_src, Shader::SourcesList gs_src, Shader::SourcesList fs_src,
  T& uniforms)
{
  Shader vs(gx::Shader::Vertex,   vs_src);
  Shader gs(gx::Shader::Geometry, gs_src);
  Shader fs(gx::Shader::Fragment, fs_src);

  Program prog(vs, gs, fs);

  prog.getUniformsLocations(uniforms);

  return prog;
}

}
