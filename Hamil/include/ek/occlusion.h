#pragma once

#include <ek/euklid.h>
#include <ek/visobject.h>

#include <math/geometry.h>

#include <atomic>
#include <vector>
#include <memory>

// Uncomment the line below to disable the
//   use of SSE instructions in the rasterizer
//#define NO_OCCLUSION_SSE

namespace gx {
class MemoryPool;
}

namespace sched {
class WorkerPool;
}

namespace ek {

class MemoryPool;

union XY {
  struct { u16 x, y; };

  u32 xy;
};

struct BinnedTri {
  XY v[3];     // Screen-space coords
  float Z[3];  // Plane equation
};

// TODO: maybe replace the current implementation with Masked Occlusion Culling?
class OcclusionBuffer {
public:
  // Size of the underlying framebuffer
  //   - Can be adjusted
  static constexpr ivec2 Size = { 640, 360 };
  static constexpr vec2 Sizef = Size.cast<float>();
  // Size of a single binning tile
  //   - Must be adjusted to partition the framebuffer
  //     into an even number of tiles
  static constexpr ivec2 TileSize = { 80, 96 };

  static constexpr ivec2 SizeMinusOne = { Size.x - 1, Size.y - 1 };

  // Size of blocks of 'm_fb_coarse'
  //   - Do NOT change this
  static constexpr ivec2 CoarseBlockSize = { 8, 8 };
  static constexpr ivec2 CoarseSize = Size / CoarseBlockSize;

  // Number of tiles in framebuffer (rounded up)
  static constexpr ivec2 SizeInTiles = {
    (Size.x + TileSize.x-1)/TileSize.x,
    (Size.y + TileSize.y-1)/TileSize.y
  };

  enum {
    NumTrisPerBin = 1024*16,
    NumBins = SizeInTiles.area(),

    // When this value is exceeded bad things will happen...
    MaxTriangles = NumTrisPerBin * NumBins,
    // Same with this one - thus it's very conservative
    MempoolSize = 32 * 1024*1024, // 32MB

    NumSIMDLanes = 4,
    SIMDLaneMask = (1<<NumSIMDLanes) - 1,

    // Number of threads used to render tiles
    NumJobs = 4,
  };

  static constexpr ivec2 Offset1 = { 1, SizeInTiles.x };
  static constexpr ivec2 Offset2 = { NumTrisPerBin, SizeInTiles.x * NumTrisPerBin };

  // Transforms a vector from clip space to viewport space
  //   and inverts the depth (from RH coordinate system to LH,
  //   which is more convinient for the rasterizer)
  static constexpr mat4 ViewportMatrix = {
    Sizef.x * 0.5f,            0.0f,  0.0f, Sizef.x * 0.5f,
              0.0f, Sizef.y * -0.5f,  0.0f, Sizef.y * 0.5f,
              0.0f,            0.0f, -1.0f,           1.0f,
              0.0f,            0.0f,  0.0f,           1.0f,
  };

  using ObjectsRef = const std::vector<VisibilityObject *>&;

  // 'mempool' is used to store the framebuffer() and other
  //  internal structures required for rasterization
  //   - 'mempool' should have size() >= MempoolSize
  OcclusionBuffer(MemoryPool& mempool);

  // Sets up internal structures for rasterizeBinnedTriangles()
  OcclusionBuffer& binTriangles(ObjectsRef objects);
  // Rasterize all VisibilityObject::Occluders to the framebuffer()
  //  and the coarseFramebuffer() when NO_OCCLUSION_SSE is NOT defined
  //   - Call after binTriangles() to enable <early,late>Test()
  OcclusionBuffer& rasterizeBinnedTriangles(ObjectsRef objects, sched::WorkerPool& pool);

  // Returns the framebuffer which can potentially be
  //   tile in 2x2 pixel quads
  const float *framebuffer() const;
  // Returns a copy of the framebuffer which has been
  //   detiled and flipped vertically
  std::unique_ptr<float[]> detiledFramebuffer() const;

  // Returns a framebuffer which stores vec2(min, max)
  //   for 8x8 blocks of the main framebuffer
  const vec2 *coarseFramebuffer() const;

  // Test the meshes AABB against the coarseFramebuffer()
  //   - When the return value == Visibility::Unknown
  //     fullTest() must be called to obtain a result
  VisibilityMesh::Visibility earlyTest(VisibilityMesh& mesh, const mat4& viewprojectionviewport,
    void /* __m128 */ *xformed_out);
  // Test the meshes AABB against the framebuffer()
  //   - Returns 'false' when the mesh is occluded
  bool fullTest(VisibilityMesh& mesh, const mat4& viewprojectionviewport,
    void /* __m128 */ *xformed_in);

private:
  void binTriangles(const VisibilityMesh& mesh, uint object_id, uint mesh_id);

  void clearTile(ivec2 start, ivec2 end);
  void rasterizeTile(const std::vector<VisibilityObject *>& objects, uint tile_idx);

  void createCoarseTile(ivec2 tile_start, ivec2 tile_end);

  // The framebuffer of size Size.area()
  float *m_fb;
  // Stores vec2(min, max) for 8x8 blocks
  //   of the framebuffer 'm_fb'
  vec2 *m_fb_coarse;

#if defined(NO_OCCLUSION_SSE)
  // Stores SizeInTiles.area() * NumTrisPerBins entries
  u16 *m_obj_id;  // VisibilityObject index
  u16 *m_mesh_id; // VisibilityMesh index
  uint *m_bin;    // Triangle index
#else
  // See note above VisibilityMesh::XformedPtr for rationale
  BinnedTri *m_bin;
#endif

  // Stores SizeInTiles.area() (number of bins) entries
  u16 *m_bin_counts;

  // Stores the number of rasterized triangles per bin
  u16 *m_drawn_tris;

  // Stores indices which define the prefered tile rendering order
  //   - That is sorted by each tile's number of triangles
  //     in descending order
  u16 *m_tile_seq;

  // Index of next tile in m_tile_seq to be rasterized
  std::atomic<uint> m_next_tile;
};

}