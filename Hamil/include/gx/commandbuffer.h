#pragma once

#include <gx/gx.h>
#include <gx/resourcepool.h>
#include <gx/memorypool.h>

#include <vector>
#include <utility>

namespace gx {

class RenderPass;
class Program;

// A CommandBuffer abstraction
//   - Aggregates draw calls, buffer uploads, state changes etc.
//     for deffered/repeated execution
//   - Can be recorded on one thread and executed on a
//     different one (for OpenGL the executing thread
//     must be the same one which spawned the context)
//   - Needless for OpenGL (as it has no explicit concept
//     of a command buffer), but having it should ease the
//     transition to for example Vulkan
class CommandBuffer {
public:
  using u32 = ::u32;
  using u64 = ::u64;
  using ResourceId = ResourcePool::Id;

  using Command = u32;

  enum : u32 {
    CommandBits = sizeof(Command)*CHAR_BIT,

    OpBits  = 4,
    OpShift = CommandBits - OpBits,
    OpMask  = (1<<OpBits) - 1,

    OpDataBits = CommandBits - OpBits,
    OpDataMask = (1<<OpShift) - 1,
  };

  enum : u64 {
    // RawOpExtraShift = 32,

    OpExtraPrimitiveBits  = 3,
    OpExtraPrimitiveShift = CommandBits - OpExtraPrimitiveBits,
    OpExtraPrimitiveMask  = (1<<OpExtraPrimitiveBits) - 1,

    OpExtraNumVertsBits = CommandBits - OpExtraPrimitiveBits,
    OpExtraNumVertsMask = (1<<OpExtraNumVertsBits) - 1,

    OpExtraXferSizeBits  = 16,
    OpExtraXferSizeShift = 16,
    OpExtraXferSizeMask  = (1<<OpExtraXferSizeBits) - 1,

    OpExtraHandleBits = 16,
    OpExtraHandleMask = (1<<OpExtraHandleBits) - 1,
  };

  enum : u32 {
    OpDataUniformTypeBits  = 4,
    OpDataUniformTypeShift = 24,
    OpDataUniformTypeMask  = (1<<OpDataUniformTypeBits) - 1,

    OpDataUniformLocationBits = 24,
    OpDataUniformLocationMask = (1<<OpDataUniformLocationBits) - 1,

    OpDataUniformInt     = 0,
    OpDataUniformFloat   = 1,
    OpDataUniformSampler = 2,
    OpDataUniformVector4 = 3,
    OpDataUniformMatrix4x4 = 4,

    OpDataNumUniformTypes,

    OpDataFenceOpBits  = 1,
    OpDataFenceOpShift = 27,
    OpDataFenceOpMask  = (1<<OpDataFenceOpBits) - 1,

    // Mask for 'fence' ResourceId in FenceOps
    OpDataFenceOpDataMask = OpDataMask & ~(OpDataFenceOpMask<<OpDataFenceOpShift),

    OpDataFenceSync = 0,
    OpDataFenceWait = 1,
  };
  static_assert(OpDataNumUniformTypes < (1<<OpDataUniformTypeBits), "Too many Uniform types defined!");

  enum : Command {
    Nop,

    // The RenderPasses ResourcePool::Id is encoded in the OpData
    OpBeginRenderPass,
    // The subpass id is encoded in the OpData
    OpBeginSubpass,

    // The Programs ResourcePool::Id is encoded in the OpData
    OpUseProgram,

    // CommandWithExtra where:
    //   - OpData encodes the VertexArray ResourceId
    //   - Bits [31;29] of OpExtra encode the Primitive
    //   - Bits [28;0] of OpExtra encode the num
    OpDraw, OpDrawIndexed,

    // Same as Draw<Indexed> except the CommandWithExtra is
    //   followed by two ExtraData which encode
    //   the base and offset respectively
    OpDrawBaseVertex,

    // CommandWithExtra where:
    //   - OpData encodes the Buffer ResourceId
    //   - Bits [31;16] of OpExtra encode the upload size (in bytes)
    //   - Bits [15;0] of OpExtra encode the
    //        MemoryPool::Handle >> MemoryPool::AllocAlignShift
    OpBufferUpload,

    // CommandWithExtra where:
    //   - The upper 4 bits of OpData encode the Uniform's type
    //   - The lowest 24 bits of OpData encode the Uniform's location
    //   - OpExtra encodes:
    //       * Literal for 32-bit values
    //       * MemoryPool handle for larger values
    OpPushUniform,

    // The MSB of OpData encodes the FenceOp - sync()/wait()
    OpFence,

    OpGenerateMipmaps,

    // OpData is ignored
    OpEnd,

    NumCommands
  };
  static_assert(NumCommands < (1<<OpBits), "Too many opcodes defined!");

  struct Error { };

  struct OutOfRangeError : public Error { };
  struct ResourceIdTooLargeError : public OutOfRangeError { };
  struct SubpassIdTooLargeError : public OutOfRangeError { };
  struct NumVertsTooLargeError : public OutOfRangeError { };
  struct XferSizeTooLargeError : public OutOfRangeError { };
  struct UniformLocationTooLargeError : public OutOfRangeError { };

  struct HandleError : public Error { };
  struct HandleOutOfRangeError : public HandleError { };
  struct HandleUnalignedError : public HandleError { };

  struct UniformTypeInvalidError : public Error { };

  struct FenceOpInvalidError : public Error { };

  // Creates a new CommandBuffer with 'initial_alloc'
  //   preallocated commands (i.e. the number of recorded
  //   commands can be bigger than this number)
  static CommandBuffer begin(size_t initial_alloc = 64);

  CommandBuffer& renderpass(ResourceId pass);
  CommandBuffer& subpass(uint id);
  CommandBuffer& program(ResourceId prog);
  CommandBuffer& draw(Primitive p, ResourceId vertex_array, size_t num_verts);
  CommandBuffer& drawIndexed(Primitive p, ResourceId indexed_vertex_array, size_t num_inds);
  CommandBuffer& drawBaseVertex(Primitive p, ResourceId iarray, size_t num, u32 base, u32 offset = 0);
  CommandBuffer& bufferUpload(ResourceId buf, MemoryPool::Handle h, size_t sz);

  CommandBuffer& uniformInt(uint location, int value);
  CommandBuffer& uniformFloat(uint location, float value);
  CommandBuffer& uniformSampler(uint location, uint sampler);
  CommandBuffer& uniformVector4(uint location, MemoryPool::Handle h);
  CommandBuffer& uniformMatrix4x4(uint location, MemoryPool::Handle h);

  CommandBuffer& fenceSync(ResourceId fence);
  CommandBuffer& fenceWait(ResourceId fence);

  CommandBuffer& generateMipmaps(ResourceId texture);

  // Must be called after the last recorded command!
  CommandBuffer& end();

  // The CommandBuffer must have a ResourcePool bound
  //   before execute() is called
  CommandBuffer& bindResourcePool(ResourcePool *pool);

  // The CommandBuffer must have a MemoryPool bound
  //   if upload commands will be used
  CommandBuffer& bindMemoryPool(MemoryPool *pool);

  // Sets an internal pointer to the currently active RenderPass
  //   - Useful when transitioning between CommandBuffers
  //     which are executed during the same RenderPass
  //   - Must be called AFTER binding a ResourcePool
  //     as this is NOT a command
  CommandBuffer& activeRenderPass(ResourceId renderpass);

  CommandBuffer& execute();

  // Clears all the commands stored in the buffer
  //   allowing it to be reused
  // The call does NOT invalidate the ResourcePool
  //   and MemoryPool bindings previously created
  CommandBuffer& reset();

protected:
  CommandBuffer(size_t initial_alloc);

private:
  enum {
    NonIndexedDraw = ~0u,
  };

  union CommandWithExtra {
    struct {
      u32 command;
      u32 extra;
    };

    u64 raw;
  };

  CommandBuffer& appendCommand(Command opcode, u32 data = 0);
  CommandBuffer& appendExtraData(u32 data);
  CommandBuffer& appendCommand(CommandWithExtra c);

  // Returns a pointer to the next Command or
  //   nullptr when the OpEnd Command is reached
  u32 *dispatch(u32 *op);

  void drawCommand(CommandWithExtra op, u32 base = ~0u, u32 offset = 0);
  void uploadCommand(CommandWithExtra op);
  void pushUniformCommand(CommandWithExtra op);
  void fenceCommand(u32 data);

  // Calls IndexedVertexArray::end() if
  //   the last draw command was drawIndexed()
  void endIndexedArray();

  static void checkResourceId(ResourceId id);
  static void checkNumVerts(size_t num);
  static void checkHandle(MemoryPool::Handle h);
  static void checkXferSize(size_t sz);
  static void checkUniformLocation(uint location);

  void assertResourcePool();
  void assertProgram();
  void assertMemoryPool();
  void assertRenderPass();

  static Command op_opcode(u32 op);
  static u32 op_data(u32 op);

  static Primitive draw_primitive(CommandWithExtra op);
  static ResourceId draw_array(CommandWithExtra op);
  static size_t draw_num(CommandWithExtra op);

  static ResourceId xfer_buffer(CommandWithExtra op);
  static MemoryPool::Handle xfer_handle(CommandWithExtra op);
  static size_t xfer_size(CommandWithExtra op);

  static CommandWithExtra make_draw(Primitive p, ResourceId array, size_t num_verts);
  static CommandWithExtra make_draw_indexed(Primitive p, ResourceId array, size_t num_inds);
  static CommandWithExtra make_draw_base_vertex(Primitive p, ResourceId array, size_t num_inds);

  static CommandWithExtra make_buffer_upload(ResourceId buf, MemoryPool::Handle h, size_t sz);

  // Doesn't check if 'type' is valid!
  static u32 make_push_uniform(u32 type, uint location);

  template <typename T>
  const T& memoryPoolRef(MemoryPool::Handle h)
  {
    return *m_memory->ptr<T>(h);
  }

  std::vector<u32> m_commands;
  ResourcePool *m_pool;
  MemoryPool *m_memory;
  Program *m_program;
  const RenderPass *m_renderpass;

  // Stores the last-used IndexedVertexArray or
  //   NonIndexedDraw otherwise
  u32 m_last_draw;

};

}