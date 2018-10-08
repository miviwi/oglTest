#include <gx/buffer.h>

#include <cassert>
#include <cstring>

namespace gx {

Buffer::Buffer(Usage usage,GLenum target) :
  m_usage(usage), m_target(target)
{
  glGenBuffers(1, &m);
}

Buffer::~Buffer()
{
  glDeleteBuffers(1, &m);
}

void *Buffer::map(Access access)
{
  use();

  return glMapBuffer(m_target, access);
}

void *Buffer::map(Access access, GLintptr off, GLint sz, uint flags)
{
  use();

  return glMapBufferRange(m_target, off, sz, access | (GLbitfield)flags);
}

void Buffer::unmap()
{
  use();

  auto result = glUnmapBuffer(m_target);
  assert(result && "failed to unmap buffer!");
}

void Buffer::flush(ssize_t off, ssize_t sz)
{
  use();

  assert(sz > 0 && "Attempting to flush a mapping with unknown size without specifying one!");
  glFlushMappedBufferRange(m_target, off, sz);
}

void Buffer::label(const char *lbl)
{
#if !defined(NDEBUG)
  use();

  glObjectLabel(GL_BUFFER, m, -1, lbl);
#endif
}

void Buffer::use() const
{
  glBindBuffer(m_target, m);
}

void Buffer::init(size_t elem_sz, size_t elem_count)
{
  size_t sz = elem_sz*elem_count;

  use();
  glBufferData(m_target, sz, nullptr, m_usage);
}

void Buffer::init(const void *data, size_t elem_sz, size_t elem_count)
{
  size_t sz = elem_sz*elem_count;

  use();
  glBufferData(m_target, sz, data, m_usage);
}

void Buffer::upload(const void *data, size_t offset, size_t elem_sz, size_t elem_count)
{
  size_t sz = elem_sz*elem_count;

  use();
  glBufferSubData(m_target, offset*elem_sz, sz, data);
}

VertexBuffer::VertexBuffer(Usage usage) :
  Buffer(usage, GL_ARRAY_BUFFER)
{
}

IndexBuffer::IndexBuffer(Usage usage, Type type) :
  Buffer(usage, GL_ELEMENT_ARRAY_BUFFER), m_type(type)
{
}

void IndexBuffer::use() const
{
  Buffer::use();
}

Type IndexBuffer::elemType() const
{
  return m_type;
}

unsigned IndexBuffer::elemSize() const
{
  switch(m_type) {
  case u8:  return 1;
  case u16: return 2;
  case u32: return 4;
  }

  return 0;
}

UniformBuffer::UniformBuffer(Usage usage) :
  Buffer(usage, GL_UNIFORM_BUFFER)
{
}

void UniformBuffer::bindToIndex(unsigned idx)
{
  glBindBufferBase(m_target, idx, m);
}

void UniformBuffer::bindToIndex(unsigned idx, size_t offset, size_t size)
{
  glBindBufferRange(m_target, idx, m, offset, size);
}

void UniformBuffer::bindToIndex(unsigned idx, size_t size)
{
  bindToIndex(idx, 0, size);
}

}
