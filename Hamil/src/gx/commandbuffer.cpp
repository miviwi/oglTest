#include <gx/commandbuffer.h>
#include <gx/renderpass.h>
#include <gx/program.h>
#include <gx/vertex.h>

#include <cassert>

namespace gx {

CommandBuffer::CommandBuffer(size_t initial_alloc) :
  m_pool(nullptr),
  m_program(nullptr),
  m_last_draw(NonIndexedDraw)
{
  m_commands.reserve(initial_alloc);
}

CommandBuffer CommandBuffer::begin(size_t initial_alloc)
{
  return CommandBuffer(initial_alloc);
}

CommandBuffer& CommandBuffer::renderPass(ResourceId pass)
{
  return appendCommand(OpBeginRenderPass, pass);
}

CommandBuffer& CommandBuffer::program(ResourceId prog)
{
  checkResourceId(prog);

  return appendCommand(OpUseProgram, prog);
}

CommandBuffer& CommandBuffer::draw(Primitive p, ResourceId vertex_array, size_t num_verts)
{
  checkResourceId(vertex_array);

  return appendCommand(make_draw(p, vertex_array, num_verts));
}

CommandBuffer& CommandBuffer::drawIndexed(Primitive p, ResourceId indexed_vertex_array, size_t num_inds)
{
  checkResourceId(indexed_vertex_array);

  return appendCommand(make_draw_indexed(p, indexed_vertex_array, num_inds));
}

CommandBuffer& CommandBuffer::end()
{
  return appendCommand(OpEnd);
}

CommandBuffer& CommandBuffer::bindResourcePool(ResourcePool *pool)
{
  m_pool = pool;

  return *this;
}

CommandBuffer& CommandBuffer::execute()
{
  assert(m_commands.back() >> OpShift == OpEnd
    && "Attempted to execute() a CommandBuffer without a previous call to end() on it!\n"
    "(Or commands were added after the end() call)");
  assert(m_pool
    && "Attemped to execute() a CommandBuffer without a bound ResourcePool!");

  u32 *pc = m_commands.data();
  while(auto next_pc = dispatch(pc)) {
    pc =  next_pc;
  }

  return *this;
}

CommandBuffer& CommandBuffer::appendCommand(Command opcode, u32 data)
{
  assert((data & ~OpDataMask) == 0 && "OpData has overflown into the opcode!");
  assert(opcode < NumCommands && "The opcode is invalid!");

  u32 command = (opcode << OpShift) | data;
  m_commands.push_back(command);

  return *this;
}

CommandBuffer& CommandBuffer::appendExtraData(u32 data)
{
  m_commands.push_back(data);

  return *this;
}

CommandBuffer& CommandBuffer::appendCommand(CommandWithExtra c)
{
  return appendExtraData(c.command)
    .appendExtraData(c.extra);
}

CommandBuffer::u32 *CommandBuffer::dispatch(u32 *op)
{
  auto fetch_extra = [&]() -> CommandWithExtra
  {
    CommandWithExtra op_extra;

    op_extra.command = *op;
    op++;
    op_extra.extra = *op;

    return op_extra;
  };

  u32 command = *op;
  u32 opcode = op_opcode(command);
  u32 data   = op_data(command);

  switch(opcode) {
  case Nop: break;

  case OpBeginRenderPass:
    m_pool->get<RenderPass>(data)
      .begin(*m_pool);
    break;

  case OpUseProgram:
    m_program = &m_pool->get<Program>(data)
      .use();
    break;

  case OpDraw:
  case OpDrawIndexed:
    assertProgram();

    drawCommand(fetch_extra());
    break;

  case OpEnd:
    endIndexedArray();
    return nullptr;
  }

  return op + 1;
}

void CommandBuffer::drawCommand(CommandWithExtra op)
{
  auto primitive = draw_primitive(op);
  auto array     = draw_array(op);
  auto num       = draw_num(op);

  if(m_last_draw != array) endIndexedArray();

  switch(op_opcode(op.command)) {
  case OpDraw:
    m_program->draw(primitive, m_pool->get<VertexArray>(array), num);
    return;

  case OpDrawIndexed:
    m_program->draw(primitive, m_pool->get<IndexedVertexArray>(array), num);
    m_last_draw = array;
    break;
  }
}

void CommandBuffer::endIndexedArray()
{
  if(m_last_draw == NonIndexedDraw) return;

  m_pool->get<IndexedVertexArray>(m_last_draw).end();
  m_last_draw = NonIndexedDraw;
}

void CommandBuffer::checkResourceId(ResourceId id)
{
  if(id & ~OpDataMask) throw ResourceIdTooLargeError();
}

void CommandBuffer::checkNumVerts(size_t num)
{
  if(num & ~OpExtraNumVertsMask) throw NumVertsTooLargeError();
}

void CommandBuffer::assertProgram()
{
  assert(m_program && "Command requires a bound Program!");
}

CommandBuffer::Command CommandBuffer::op_opcode(u32 op)
{
  auto opcode = op >> OpShift;
  return (Command)(opcode & OpMask);
}

CommandBuffer::u32 CommandBuffer::op_data(u32 op)
{
  return op & OpDataMask;
}

static constexpr Primitive p_primitive_lut[] ={
  Points,
  Lines, LineLoop, LineStrip,
  Triangles, TriangleFan, TriangleStrip,

  (Primitive)~0u, // Sentinel
};

Primitive CommandBuffer::draw_primitive(CommandWithExtra op)
{
  auto primitive = (op.extra >> OpExtraPrimitiveShift) & OpExtraPrimitiveMask;
  return p_primitive_lut[primitive];
}

CommandBuffer::ResourceId CommandBuffer::draw_array(CommandWithExtra op)
{
  return op_data(op.command);
}

size_t CommandBuffer::draw_num(CommandWithExtra op)
{
  return (size_t)(op.extra & OpExtraNumVertsMask);
}

CommandBuffer::CommandWithExtra CommandBuffer::make_draw(Primitive p,
  ResourceId array, size_t num_verts)
{
  u32 primitive = 0;
  switch(p) {
  case Points: primitive = 0; break;

  case Lines:     primitive = 1; break;
  case LineLoop:  primitive = 2; break;
  case LineStrip: primitive = 3; break;

  case Triangles:     primitive = 4; break;
  case TriangleFan:   primitive = 5; break;
  case TriangleStrip: primitive = 6; break;
  }

  checkNumVerts(num_verts);
  auto num = (u32)(num_verts & OpExtraNumVertsMask);

  checkResourceId(array);

  CommandWithExtra c;
  c.command = (OpDraw << OpShift) | array;
  c.extra = (primitive << OpExtraPrimitiveShift) | num;

  return c;
}

CommandBuffer::CommandWithExtra CommandBuffer::make_draw_indexed(Primitive p,
  ResourceId array, size_t num_inds)
{
  auto c = make_draw(p, array, num_inds);

  c.command &= ~(OpMask << OpShift);
  c.command |= OpDrawIndexed << OpShift;

  return c;
}

}