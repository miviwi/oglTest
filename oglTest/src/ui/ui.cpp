#include <ui/ui.h>
#include <ui/frame.h>
#include <ui/painter.h>

#include <gx/pipeline.h>
#include <gx/buffer.h>
#include <gx/vertex.h>
#include <gx/program.h>
#include <uniforms.h>

#include <algorithm>
#include <memory>

#include <Windows.h>

namespace ui {

static const char *shader_uType_defs = R"DEFS(

const int TypeShape = 0;
const int TypeText  = 1;

)DEFS";

// Must be kept in sync with GLSL!!
enum Shader_uType {
  Shader_TypeShape = 0,
  Shader_TypeText  = 1,
};

static const char *vs_src = R"VTX(

uniform mat4 uModelViewProjection;

uniform int uType;

const float fixed_factor = 1.0 / float(1<<4);

layout(location = 0) in vec2 iPos;
layout(location = 1) in vec4 iColor;
layout(location = 2) in vec2 iUV;

out VertexData {
  vec4 color;
  vec2 uv;
} vertex;

void main() {
  vec2 pos = iPos;
  if(uType != TypeText) pos *= fixed_factor; // TypeText vertices are already normalized

  vertex.uv = iUV;
  vertex.color = iColor;

  gl_Position = uModelViewProjection * vec4(pos, 0, 1);
}

)VTX";

static const char *fs_src = R"FRAG(

uniform sampler2D uFontAtlas;

uniform int uType;
uniform vec4 uTextColor;

const float UiGamma = 1.2f;

in VertexData {
  vec4 color;
  vec2 uv;
} fragment;

layout(location = 0) out vec4 color;

void main() {
  vec4 font_sample = sampleFontAtlas(uFontAtlas, fragment.uv);

  switch(uType) {
    case TypeText:  color = uTextColor * font_sample; break;
    case TypeShape: color = fragment.color; break;

    default: color = vec4(0); break;
  }

  vec3 srgb_color = pow(color.rgb, vec3(1.0f/UiGamma));
  color = vec4(srgb_color, color.a);
}

)FRAG";

std::unique_ptr<gx::Program> ui_program;

void init()
{
  CursorDriver::init();

  ui_program = std::make_unique<gx::Program>(gx::make_program(
    { shader_uType_defs, vs_src }, { ft::Font::frag_shader, shader_uType_defs, fs_src }, U::ui
  ));
  ui_program->label("UI_program");
}

void finalize()
{
}

Ui::Ui(Geometry geom, const Style& style) :
  m_geom(geom), m_style(style), m_repaint(true),
  m_capture(nullptr),
  m_keyboard(nullptr),
  m_buf(gx::Buffer::Dynamic),
  m_ind(gx::Buffer::Dynamic, gx::u16),
  m_vtx(VertexPainter::Fmt, m_buf, m_ind)
{
  m_buf.label("UI_vertex");
  m_ind.label("UI_index");

  m_vtx.label("UI_vertex_array");

  m_buf.init(sizeof(Vertex), VertexPainter::NumBufferElements);
  m_ind.init(sizeof(u16), VertexPainter::NumBufferElements);
}

Ui::~Ui()
{
  for(const auto& frame : m_frames) delete frame;
}

ivec4 Ui::scissor_rect(Geometry g)
{
  auto ga = ivec2{ (int)g.x, (int)g.y },
    gb = ivec2{ ga.x+(int)g.w, ga.y+(int)g.h };

  return ivec4{ (int)g.x, (int)FramebufferSize.y - gb.y, (int)g.w, (int)g.h };
}

Ui& Ui::frame(Frame *f, vec2 pos)
{
  if(!pos.x && !pos.y) pos = m_geom.center();
  
  f->m_geom.x = pos.x; f->m_geom.y = pos.y;
  f->attached();

  m_frames.push_back(f);

  return *this;
}

Ui& Ui::frame(Frame *f)
{
  f->attached();
  m_frames.push_back(f);

  return *this;
}

Ui& Ui::frame(Frame& f, vec2 pos)
{
  return frame(&f, pos);
}

Ui& Ui::frame(Frame &f)
{
  return frame(&f);
}

void Ui::registerFrame(Frame *frame)
{
  if(frame->m_name) m_names.insert({ frame->m_name, frame });
}

Frame *Ui::getFrameByName(const std::string& name)
{
  auto f = m_names.find(name);
  return f != m_names.end() ? f->second : nullptr;
}

const Style& Ui::style() const
{
  return m_style;
}

bool Ui::input(CursorDriver& cursor, const InputPtr& input)
{
  if(auto mouse = input->get<win32::Mouse>()) {
    if(mouse->event == win32::Mouse::Move && !cursor.visible()) cursor.visible(true);
  }

  if(!m_geom.intersect(cursor.pos())) return false;

  if(m_keyboard) {
    if(input->get<win32::Keyboard>()) return m_keyboard->input(cursor, input);
  } else if(m_capture) {
    return m_capture->input(cursor, input);
  }

  for(auto iter = m_frames.crbegin(); iter != m_frames.crend(); iter++) {
    const auto& frame = *iter;
    if(frame->input(cursor, input)) return true;
  }

  return false;
}

void Ui::paint()
{
  if(m_frames.empty()) return;

  auto pipeline = gx::Pipeline::current();

  if(m_repaint) {
    m_painter.end();
    for(const auto& frame : m_frames) frame->paint(m_painter, m_geom);

    m_buf.upload(m_painter.vertices(), 0, m_painter.numVertices());
    m_ind.upload(m_painter.indices(), 0, m_painter.numIndices());
  }

  auto projection = xform::ortho(0, 0, FramebufferSize.y, FramebufferSize.x, 0.0f, 1.0f);

  ui_program->use()
    .uniformSampler(U::ui.uFontAtlas, ft::TexImageUnit);

  m_painter.doCommands([&,this](VertexPainter::Command cmd)
  {
    switch(cmd.type) {
    case VertexPainter::Primitive:
      ui_program->use()
        .uniformMatrix4x4(U::ui.uModelViewProjection, projection)
        .uniformInt(U::ui.uType, Shader_TypeShape)
        .drawBaseVertex(cmd.p, m_vtx, cmd.base, cmd.offset, cmd.num);
      break;

    case VertexPainter::Text:
      cmd.font->bindFontAltas();
      ui_program->use()
        .uniformMatrix4x4(U::ui.uModelViewProjection, projection*xform::translate(cmd.pos.x, cmd.pos.y, 0))
        .uniformInt(U::ui.uType, Shader_TypeText)
        .uniformVector4(U::ui.uTextColor, cmd.color.normalize())
        .drawBaseVertex(cmd.p, m_vtx, cmd.base, cmd.offset, cmd.num);
      break;

    case VertexPainter::Pipeline:
      cmd.pipeline.use();

      break;

    default: break;
    }
  });
  m_vtx.end();

  pipeline.use();
}

void Ui::capture(Frame *frame)
{
  if(m_capture && frame != m_capture) m_capture->losingCapture();

  m_capture = frame;
}

void Ui::keyboard(Frame *frame)
{
  if(m_keyboard && frame != m_keyboard) m_keyboard->losingCapture();

  m_keyboard = frame;
}

}