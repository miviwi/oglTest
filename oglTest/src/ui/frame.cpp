#include "ui/frame.h"
#include "ui/painter.h"

namespace ui {

Frame::Frame(Ui &ui, const char *name, Geometry geom) :
  m_ui(&ui), m_name(name), m_gravity(Left), m_geom(geom)
{
  m_ui->registerFrame(this);
}

Frame::Frame(Ui& ui, Geometry geom) :
  Frame(ui, nullptr, geom)
{
}

Frame::Frame(Ui& ui, const char *name) :
  Frame(ui, name, Geometry{ 0, 0, 0, 0 })
{
}

Frame::Frame(Ui& ui) :
  Frame(ui, nullptr)
{
}

Frame::~Frame()
{
}

bool Frame::input(ivec2 mouse_pos, const InputPtr& input)
{
  if(!m_geom.intersect(mouse_pos)) return false;

  if(input->getTag() == win32::Mouse::tag()) {
    using win32::Mouse;

    auto mouse = (Mouse *)input.get();
    if(mouse->buttonDown(Mouse::Left)) {
      m_countrer++;
    } else if(mouse->buttons & Mouse::Left) {
      m_geom.x += mouse->dx; m_geom.y += mouse->dy;
    }

    return true;
  }

  return false;
}

Frame& Frame::geometry(Geometry geom)
{
  m_geom = geom;

  return *this;
}

Geometry Frame::geometry() const
{
  return m_geom;
}

Frame& Frame::gravity(Gravity gravity)
{
  m_gravity = gravity;

  return *this;
}

Frame::Gravity Frame::gravity() const
{
  return m_gravity;
}

bool Frame::mouseWillLeave(ivec2 mouse_pos, const win32::Mouse *mouse)
{
  mouse_pos += ivec2{ (int)mouse->dx, (int)mouse->dy };
  return !m_geom.intersect(mouse_pos);
}

void Frame::paint(VertexPainter& painter, Geometry parent)
{
  Geometry g = m_geom;
  auto style = m_ui->style();

  auto ga = vec2{ g.x, g.y },
    gb = vec2{ g.x+g.w, g.y+g.h };

  vec2 circle = g.center();
  vec2 c = g.center();

  auto time = GetTickCount();
  auto anim_time = 10000;

  auto anim_factor = (float)(time % anim_time)/(float)anim_time;

  float radius = lerp(0.0f, 55.0f/2.0f, anim_factor);

  Color gray = { 100, 100, 100, 255 },
    dark_gray = { 200, 200, 200, 255 };
  Geometry bg_pos = { gb.x - 30.0f, ga.y, 30.0f, 30.0f };
  vec2 close_btn = {
    (bg_pos.w / 2.0f) + bg_pos.x,
    (bg_pos.h / 2.0f) + bg_pos.y,
  };

  Geometry round_rect = {
    g.x+15.0f, g.y+15.0f, 55.0f, 55.0f
  };

  vec2 title_pos = {
    g.x+5.0f, g.y+style.font->ascender()
  };

  const char *title = "ui::Frame title goes here";

  float angle = lerp(0.0f, 2.0f*PIf, anim_factor);

  vec2 line_delta = {
    50.0f, 0.0f
  };

  painter
    .pipeline(gx::Pipeline().alphaBlend().scissor(Ui::scissor_rect(parent.clip(g))))
    .rect(g, style.bg.color)
    .border(g, 1, style.border.color)
    //.text(*style.font.get(), title, title_pos, white())
    //.border({ g.x+5.0f, title_pos.y-style.font->ascender()-style.font->descener(),
    //        style.font->width(style.font->string(title)),
    //        style.font->height(style.font->string(title)) }, white())
    .text(*style.font.get(), std::to_string(m_countrer).c_str(), circle, black())
    //.circleSegment(circle, 180.0f, angle, angle + (PI/2.0f), transparent(), white())
    .line(c+line_delta, c-line_delta, 16, VertexPainter::CapRound, { 0, 20, 80 }, { 0, 20, 80 })
    .line(c+line_delta, c-line_delta, 8, VertexPainter::CapRound, { 20, 20, 100 }, { 20, 20, 100 })
    .lineBorder(c+line_delta, c-line_delta, 16-1, VertexPainter::CapRound, black(), black())
    .circle({ c.x-45.0f, c.y }, 13, style.bg.color[0])
    .circle({ c.x-45.0f, c.y }, 7, style.bg.color[1])
    .arcFull({ c.x-45.0f, c.y }, 12, black())
    //.roundedRect(round_rect, radius, VertexPainter::All & ~VertexPainter::BottomRight, gray)
    //.roundedRect(round_rect.contract(1), radius, VertexPainter::All & ~VertexPainter::BottomRight, dark_gray)
    .arcFull(circle, radius, { 0, 128, 0, 255 })
    ;
}

}