#include "pipeline.h"

#include <cassert>
#include <algorithm>

namespace gx {

Pipeline p_current;

Pipeline::Pipeline()
{
  std::fill(m_enabled, m_enabled+NumConfigTypes, false);
}

void Pipeline::use() const
{
  p_current = *this;

  for(unsigned i = 0; i < NumConfigTypes; i++) {
    if(!m_enabled[i]) disable((ConfigType)i);
    else              enable((ConfigType)i);
  }
}

Pipeline& Pipeline::viewport(int x, int y, int w, int h)
{
  auto& v = m_viewport;

  m_enabled[Viewport] = true;
  v.x = x; v.y = y; v.width = w; v.height = h;
  
  return *this;
}

Pipeline& Pipeline::scissor(int x, int y, int w, int h)
{
  auto& s = m_scissor;

  m_enabled[Scissor] = true;
  s.current = false;
  s.x = x; s.y = y; s.width = w; s.height = h;

  return *this;
}

Pipeline& Pipeline::noScissor()
{
  m_enabled[Scissor] = false;

  return *this;
}

Pipeline& Pipeline::noBlend()
{
  m_enabled[Blend] = false;

  return *this;
}

Pipeline& Pipeline::noDepthTest()
{
  m_enabled[Depth] = false;

  return *this;
}

Pipeline& Pipeline::noCull()
{
  m_enabled[Cull] = false;

  return *this;
}

Pipeline& Pipeline::filledPolys()
{
  m_enabled[Wireframe] = false;

  return *this;
}

Pipeline& Pipeline::currentScissor()
{
  m_enabled[Scissor] = true;
  m_scissor.current = true;

  return *this;
}

Pipeline Pipeline::current()
{
  return p_current;
}

bool Pipeline::isEnabled(ConfigType what)
{
  return m_enabled[what];
}

Pipeline& Pipeline::alphaBlend()
{
  m_enabled[Blend] = true;
  m_blend.sfactor = GL_SRC_ALPHA; m_blend.dfactor = GL_ONE_MINUS_SRC_ALPHA;

  return *this;
}

Pipeline& Pipeline::additiveBlend()
{
  m_enabled[Blend] = true;
  m_blend.sfactor = GL_ONE; m_blend.dfactor = GL_ONE;

  return *this;
}

Pipeline& Pipeline::depthTest(DepthFunc func)
{
  m_enabled[Depth] = true;
  m_depth.func = (GLenum)func;


  return *this;
}

Pipeline& Pipeline::cull(FrontFace front, CullMode mode)
{
  m_enabled[Cull] = true;
  m_cull.front = (GLenum)front; m_cull.mode = (GLenum)mode;

  return *this;
}

Pipeline& Pipeline::cull(CullMode mode)
{
  return cull(CounterClockwise, mode);
}

Pipeline& Pipeline::clearColor(vec4 color)
{
  m_enabled[Clear] = true;
  m_clear.color = color;

  return *this;
}

Pipeline& Pipeline::clearDepth(float depth)
{
  m_enabled[Clear] = true;
  m_clear.depth = depth;

  return *this;
}

Pipeline& Pipeline::clearStencil(int stencil)
{ 
  m_enabled[Clear] = true;
  m_clear.stencil = stencil;

  return *this;
}

Pipeline& Pipeline::clear(vec4 color, float depth)
{
  m_enabled[Clear] = true;
  m_clear.color = color; m_clear.depth = depth;

  return *this;
}

Pipeline& Pipeline::wireframe()
{
  m_enabled[Wireframe] = true;

  return *this;
}

void Pipeline::disable(ConfigType config) const
{
  switch(config) {
  case Viewport:  break;
  case Scissor:   glDisable(GL_SCISSOR_TEST); break;
  case Blend:     glDisable(GL_BLEND); break;
  case Depth:     glDisable(GL_DEPTH_TEST); break;
  case Stencil:   glDisable(GL_STENCIL_TEST); break;
  case Cull:      glDisable(GL_CULL_FACE); break;
  case Clear:     break;
  case Wireframe: glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); break;

  default: break;
  }
}

void Pipeline::enable(ConfigType config) const
{
  const auto& v = m_viewport;
  const auto& sc = m_scissor;
  const auto& c = m_clear;

  switch(config) {
  case Viewport: glViewport(v.x, v.y, v.width, v.height); break;
  case Scissor:
    if(!sc.current) {
      glEnable(GL_SCISSOR_TEST);
      glScissor(sc.x, sc.y, sc.width, sc.height);
    }
    break;
  case Blend:
    glEnable(GL_BLEND);
    glBlendFunc(m_blend.sfactor, m_blend.dfactor);
    break;
  case Depth:
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(m_depth.func);
    break;
  case Stencil:
    assert(0 && "StencilConfig unimpleneted!");
    // TODO
    break;
  case Cull:
    glEnable(GL_CULL_FACE);
    glFrontFace(m_cull.front);
    glCullFace(m_cull.mode);
    break;
  case Clear:
    glClearColor(c.color.r, c.color.g, c.color.b, c.color.a);
    glClearDepth(c.depth);
    glClearStencil(c.stencil);
    break;
  case Wireframe: glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); break;

  default: break;
  }
}

ScopedPipeline::ScopedPipeline(const Pipeline& p)
{
  m = Pipeline::current();

  p.use();
}

ScopedPipeline::~ScopedPipeline()
{
  m.use();
}

}
