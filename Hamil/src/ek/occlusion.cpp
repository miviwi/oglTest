#include <ek/occlusion.h>

#include <utility>

#include <intrin.h>

namespace ek {

static std::pair<ivec2, ivec2> tri_bbox(ivec2 fx[3], ivec2 min_extent, ivec2 max_extent)
{
  ivec2 start = ivec2::max(ivec2::min(ivec2::min(fx[0], fx[1]), fx[2]), min_extent);
  ivec2 end   = ivec2::min(ivec2::max(ivec2::max(fx[0], fx[1]), fx[2]), max_extent);

  return std::make_pair(start, end);
}

static int tri_area(ivec2 fx[3])
{
  return (fx[1].x - fx[0].x)*(fx[2].y - fx[0].y) - (fx[0].x - fx[2].x)*(fx[0].y - fx[1].y);
}

INTRIN_INLINE static void tri_bbox(__m128i fxx[3], __m128i fxy[3], ivec2 min_extent, ivec2 max_extent,
  __m128i& startx, __m128i& starty, __m128i& endx, __m128i& endy)
{
  startx = _mm_max_epi32(
    _mm_min_epi32(_mm_min_epi32(fxx[0], fxx[1]), fxx[2]),
    _mm_set1_epi32(min_extent.x)
  );
  endx = _mm_min_epi32(
    _mm_max_epi32(_mm_max_epi32(fxx[0], fxx[1]), fxx[2]),
    _mm_set1_epi32(max_extent.x)
  );

  starty = _mm_max_epi32(
    _mm_min_epi32(_mm_min_epi32(fxy[0], fxy[1]), fxy[2]),
    _mm_set1_epi32(min_extent.y)
  );
  endy = _mm_min_epi32(
    _mm_max_epi32(_mm_max_epi32(fxy[0], fxy[1]), fxy[2]),
    _mm_set1_epi32(max_extent.y)
  );
}

INTRIN_INLINE static __m128i tri_area(__m128i fxx[3], __m128i fxy[3])
{
  __m128i tri_area1 = _mm_sub_epi32(fxx[1], fxx[0]);
  tri_area1 = _mm_mullo_epi32(tri_area1, _mm_sub_epi32(fxy[2], fxy[0]));

  __m128i tri_area2 = _mm_sub_epi32(fxx[0], fxx[2]);
  tri_area2 = _mm_mullo_epi32(tri_area2, _mm_sub_epi32(fxy[0], fxy[1]));

  __m128i area = _mm_sub_epi32(tri_area1, tri_area2);

  return area;
}

static int find_and_clear_lsb(uint *mask)
{
  ulong idx;
  _BitScanForward(&idx, *mask);
  *mask &= *mask - 1;

  return idx;
}

OcclusionBuffer::OcclusionBuffer()
{
  m_fb = std::make_unique<float[]>(Size.area());

#if defined(NO_OCCLUSION_SSE)
  m_obj_id  = std::make_unique<u16[]>(MaxTriangles);
  m_mesh_id = std::make_unique<u16[]>(MaxTriangles);
  m_bin     = std::make_unique<uint[]>(MaxTriangles);
#else
  m_bin = std::make_unique<BinnedTri[]>(MaxTriangles);
#endif

  m_bin_counts = std::make_unique<u16[]>(NumBins);
  m_drawn_tris = std::make_unique<u16[]>(NumBins);
}

OcclusionBuffer& OcclusionBuffer::binTriangles(const std::vector<VisibilityObject::Ptr>& objects)
{
  // Clear the bin triangle counts
  memset(m_bin_counts.get(), 0, NumBins * sizeof(decltype(m_bin_counts)::element_type));

  for(uint o = 0; o < objects.size(); o++) {
    const auto& obj = objects[o];
    uint num_meshes = obj->numMeshes();
    for(uint m = 0; m < num_meshes; m++) binTriangles(obj->mesh(m), o, m);
  }

  return *this;
}

OcclusionBuffer& OcclusionBuffer::rasterizeBinnedTriangles(const std::vector<VisibilityObject::Ptr>& objects)
{
  static constexpr uint NumTiles = (uint)SizeInTiles.area();
  for(uint i = 0; i < NumTiles; i++) {
    rasterizeTile(objects, i);
  }

  return *this;
}

const float *OcclusionBuffer::framebuffer() const
{
  return m_fb.get();
}

std::unique_ptr<float[]> OcclusionBuffer::detiledFramebuffer() const
{
  auto fb = m_fb.get();
  auto ptr = std::make_unique<float[]>(Size.area());
#if defined(NO_OCCLUSION_SSE)
  for(int y = 0; y < Size.y; y++) {
    auto src = fb + y*Size.x;
    auto dst = ptr.get() + (Size.y - y - 1)*Size.x;
    memcpy(dst, src, Size.x * sizeof(float));
  }
#else
  // Perform detiling and flipping:
  //      y:
  // x:   A B C D  ->  C D . .
  //      . . . .      A B . .
  for(int y = 0; y < Size.y; y += 2) {
    auto src = fb + y*Size.x;
    auto dst = ptr.get() + (Size.y - y - 2)*Size.x;
    for(int x = 0; x < Size.x; x += 2) {
      dst[0] = src[2];
      dst[1] = src[3];
      dst[Size.x + 0 /* Next row */] = src[0];
      dst[Size.x + 1 /* Next row */] = src[1];

      src += 4;
      dst += 2;
    }
  }
#endif

  return std::move(ptr);
}

void OcclusionBuffer::binTriangles(const VisibilityMesh& mesh, uint object_id, uint mesh_id)
{
  static constexpr ivec2 SizeMinusOne = { Size.x - 1, Size.y - 1 };

  auto num_triangles = mesh.numTriangles();
#if defined(NO_OCCLUSION_SSE)
  static constexpr uint tri_increment = 1;
#else
  int num_lanes = NumSIMDLanes;
  int lane_mask = (1<<num_lanes) - 1;
  static constexpr uint tri_increment = NumSIMDLanes;
#endif
  for(uint tri = 0; tri < num_triangles; tri += tri_increment) {
#if defined(NO_OCCLUSION_SSE)
    auto xformed = mesh.gatherTri(tri);

    ivec2 fx[3];
    for(uint i = 0; i < 3; i++) {
      auto pt = xformed[i];
      fx[i] = { (int)(pt.x + 0.5f), (int)(pt.y + 0.5f) };
    }

    int area = tri_area(fx);

    // Compute triangles screen space bounding box in pixels
    auto [start, end] = tri_bbox(fx, ivec2::zero(), SizeMinusOne);

    // Skip triangle if area is 0
    if(area <= 0) continue;
    // Don't bin clipped traingles
    if(end.x < start.x || end.y < start.y) continue;

    // Reject triangles clipped by the near plane
    if(xformed[0].w <= 0.0f || xformed[1].w <= 0.0f || xformed[2].w <= 0.0f) continue;

    ivec2 startt = ivec2::max(start / TileSize, ivec2::zero());
    ivec2 endt   = ivec2::min(end / TileSize, SizeMinusOne);

    int row, col;
    for(row = startt.y; row <= endt.y; row++) {
      int off1 = Offset1.y * row;
      int off2 = Offset2.y * row;
      for(col = startt.x; col <= endt.x; col++) {
        int idx1 = off1 + (Offset1.x*col);
        int idx2 = off2 + (Offset2.x*col) + m_bin_counts[idx1];

        m_bin[idx2] = tri;
        m_obj_id[idx2]  = object_id;
        m_mesh_id[idx2] = mesh_id;

        m_bin_counts[idx1]++;
      }
    }
#else
    if(tri + NumSIMDLanes > num_triangles) {
      num_lanes = num_triangles - tri + 1;
      lane_mask = (1<<num_lanes) - 1;
    }

    VisMesh4Tris xformed[3];
    mesh.gatherTri4(xformed, tri, num_lanes);

    __m128i fxx[3], fxy[3];
    __m128i XY[3];
    __m128 Z[3];
    for(int i = 0; i < 3; i++) {
      fxx[i] = _mm_cvtps_epi32(xformed[i].X);
      fxy[i] = _mm_cvtps_epi32(xformed[i].Y);

      __m128i inter0 = _mm_unpacklo_epi32(fxx[i], fxy[i]);
      __m128i inter1 = _mm_unpackhi_epi32(fxx[i], fxy[i]);

      XY[i] = _mm_packs_epi32(inter0, inter1);
      Z[i]  = xformed[i].Z;
    }

    __m128i area = tri_area(fxx, fxy);
    __m128 inv_area = _mm_rcp_ps(_mm_cvtepi32_ps(area));

    Z[1] = _mm_mul_ps(_mm_sub_ps(Z[1], Z[0]), inv_area);
    Z[2] = _mm_mul_ps(_mm_sub_ps(Z[2], Z[0]), inv_area);

    __m128i startx, endx, starty, endy;
    tri_bbox(fxx, fxy, ivec2::zero(), SizeMinusOne, startx, starty, endx, endy);

    __m128i front = _mm_cmpgt_epi32(area, _mm_setzero_si128());
    __m128i non_emptyx = _mm_cmpgt_epi32(endx, startx);
    __m128i non_emptyy = _mm_cmpgt_epi32(endy, starty);
    __m128 accept1 = _mm_castsi128_ps(_mm_and_si128(_mm_and_si128(front, non_emptyx), non_emptyy));

    __m128 W0 = _mm_cmpgt_ps(xformed[0].W, _mm_setzero_ps());
    __m128 W1 = _mm_cmpgt_ps(xformed[1].W, _mm_setzero_ps());
    __m128 W2 = _mm_cmpgt_ps(xformed[2].W, _mm_setzero_ps());

    __m128 accept = _mm_and_ps(_mm_and_ps(accept1, W0), _mm_and_ps(W1, W2));
    uint tri_mask = _mm_movemask_ps(accept) & lane_mask;

    while(tri_mask) {
      int i = find_and_clear_lsb(&tri_mask);

      int startxx = std::max(startx.m128i_i32[i]/TileSize.x, 0);
      int endxx   = std::min(endx.m128i_i32[i]/TileSize.x, SizeInTiles.x-1);

      int startxy = std::max(starty.m128i_i32[i]/TileSize.y, 0);
      int endxy   = std::min(endy.m128i_i32[i]/TileSize.y, SizeInTiles.y-1);

      int row, col;
      for(row = startxy; row <= endxy; row++) {
        int off1 = Offset1.y * row;
        int off2 = Offset2.y * row;
        for(col = startxx; col <= endxx; col++) {
          int idx1 = off1 + (Offset1.x*col);
          int idx2 = off2 + (Offset2.x*col) + m_bin_counts[idx1];
          BinnedTri *btri = m_bin.get() + idx2;

          btri->v[0].xy = XY[0].m128i_i32[i];
          btri->v[1].xy = XY[1].m128i_i32[i];
          btri->v[2].xy = XY[2].m128i_i32[i];

          btri->Z[0] = Z[0].m128_f32[i];
          btri->Z[1] = Z[1].m128_f32[i];
          btri->Z[2] = Z[2].m128_f32[i];

          m_bin_counts[idx1]++;
        }
      }
    }
#endif
  }

  printf("binned:\n");
  for(auto y = 0; y < SizeInTiles.y; y++) {
    for(auto x = 0; x < SizeInTiles.x; x++) {
      printf("%d ", m_bin_counts[y*SizeInTiles.x + x]);
    }

    printf("\n");
  }
  printf("\n");
}

void OcclusionBuffer::clearTile(ivec2 start, ivec2 end)
{
  float *fb = m_fb.get();
  int w = end.x - start.x;
#if defined(NO_SSE_OCCLUSION)
  for(int r = start.y; r < end.y; r++) {
    int row_idx = r*Size.x + start.x;
    memset(fb + row_idx, 0, w*sizeof(float));
  }
#else
  for(int r = start.y; r < end.y; r += 2) {
    int row_idx = r*Size.x + 2*start.x;
    memset(fb + row_idx, 0, 2*w*sizeof(float));
  }
#endif
}
void OcclusionBuffer::rasterizeTile(const std::vector<VisibilityObject::Ptr>& objects, uint tile_idx)
{
  auto fb = m_fb.get();

  uvec2 tile = { tile_idx % SizeInTiles.x, tile_idx / SizeInTiles.x };

  ivec2 tile_start = tile.cast<int>() * TileSize;
  ivec2 tile_end   = ivec2::min(tile_start + TileSize - ivec2(1, 1), Size - ivec2(1, 1));

  uint bin = 0,
    bin_idx = 0,
    off1 = Offset1.y*tile.y + Offset1.x*tile.x,
    off2 = Offset2.y*tile.y + Offset2.x*tile.x;

  uint num_tris = m_bin_counts[off1 + bin];

  clearTile(tile_start, tile_end);

#if defined(NO_OCCLUSION_SSE)
  VisibilityMesh::Triangle xformed;
#else
  VisMesh4Tris gather_buf[2];

  static const __m128i ColumnOffsets = _mm_setr_epi32(0, 1, 0, 1);
  static const __m128i RowOffsets    = _mm_setr_epi32(0, 0, 1, 1);
#endif
  bool done = false;
  bool all_empty = false;

  m_drawn_tris[tile_idx] = num_tris;
  while(!done) {
#if !defined (NO_OCCLUSION_SSE)
    int num_simd_tris = 0;
    for(uint lane = 0; lane < NumSIMDLanes; lane++) {
#endif
      while(num_tris <= 0) {
        bin++;
        if(bin >= 1) break;

        num_tris = m_bin_counts[off1 + bin];
        m_drawn_tris[tile_idx] += num_tris;
        bin_idx = 0;
      }

      if(!num_tris) break;
#if !defined (NO_OCCLUSION_SSE)
      const auto *btri = m_bin.get() + (off2 + bin*NumTrisPerBin + bin_idx);
      gather_buf[0].v[lane] = _mm_castsi128_ps(_mm_loadu_si128((const __m128i *)&btri->v[0].xy));
      gather_buf[1].v[lane] = _mm_castsi128_ps(_mm_loadl_epi64((const __m128i *)&btri->Z[1]));

      num_simd_tris++;
#else
      u16 object = m_obj_id[off2 + bin*NumTrisPerBin + bin_idx];
      u16 mesh   = m_mesh_id[off2 + bin*NumTrisPerBin + bin_idx];
      uint tri   = m_bin[off2 + bin*NumTrisPerBin + bin_idx];

      xformed = objects[object]->mesh(mesh).gatherTri(tri);
#endif
      all_empty = false;

      bin_idx++;
      num_tris--;
#if !defined(NO_OCCLUSION_SSE)
    }
#endif

    done = bin >= NumBins;
    if(all_empty) return;

#if defined(NO_OCCLUSION_SSE)
    ivec2 fx[3];
    float Z[3];
    for(uint i = 0; i < 3; i++) {
      fx[i] = vec2(xformed[i].x + 0.5f, xformed[i].y + 0.5f).cast<int>();
      Z[i] = xformed[i].z;
    }

    // Fab(x, y) =     Ax       +       By     +      C              = 0
    // Fab(x, y) = (ya - yb)x   +   (xb - xa)y + (xa * yb - xb * ya) = 0
    // Compute A = (ya - yb) for the 3 line segments that make up each triangle
    int A0 = fx[1].y - fx[2].y;
    int A1 = fx[2].y - fx[0].y;
    int A2 = fx[0].y - fx[1].y;

    // Compute B = (xb - xa) for the 3 line segments that make up each triangle
    int B0 = fx[2].x - fx[1].x;
    int B1 = fx[0].x - fx[2].x;
    int B2 = fx[1].x - fx[0].x;

    // Compute C = (xa * yb - xb * ya) for the 3 line segments that make up each triangle
    int C0 = fx[1].x * fx[2].y - fx[2].x * fx[1].y;
    int C1 = fx[2].x * fx[0].y - fx[0].x * fx[2].y;
    int C2 = fx[0].x * fx[1].y - fx[1].x * fx[0].y;

    int area = tri_area(fx);
    float inv_area = 1.0f / (float)area;

    Z[1] = (Z[1] - Z[0]) * inv_area;
    Z[2] = (Z[2] - Z[0]) * inv_area;

    auto [start, end] = tri_bbox(fx, tile_start, tile_end+1);

    start.x &= 0xFFFFFFFE; start.y &= 0xFFFFFFFE;

    int row_idx = start.y*Size.x + start.x;
    int col = start.x,
      row = start.y;

    int alpha0 = (A0 * col) + (B0 * row) + C0,
      beta0 = (A1 * col) + (B1 * row) + C1,
      gama0 = (A2 * col) + (B2 * row) + C2;

    float zx = A1*Z[1] + A2*Z[2];

    for(int r = start.y; r < end.y; r++) {
      int index = row_idx;
      int alpha = alpha0,
        beta = beta0,
        gama = gama0;

      float depth = Z[0] + Z[1]*beta + Z[2]*gama;

      for(int c = start.x; c < end.x; c++) {
        int mask = alpha | beta | gama;

        float prev_depth   = fb[index];
        float merged_depth = std::max(prev_depth, depth);
        float final_depth  = mask < 0 ? prev_depth : merged_depth;

        fb[index] = final_depth;

        index++;
        alpha += A0; beta += A1; gama += A2;
        depth += zx;
      }

      row++; row_idx += Size.x;
      alpha0 += B0; beta0 += B1; gama0 += B2;
    }
#else
    __m128i fxx[3], fxy[3];

    {
      __m128 v0 = gather_buf[0].X;
      __m128 v1 = gather_buf[0].Y;
      __m128 v2 = gather_buf[0].Z;
      __m128 v3 = gather_buf[0].W;
      _MM_TRANSPOSE4_PS(v0, v1, v2, v3);

      fxx[0] = _mm_srai_epi32(_mm_slli_epi32(_mm_castps_si128(v0), 16), 16);
      fxy[0] = _mm_srai_epi32(_mm_castps_si128(v0), 16);
      fxx[1] = _mm_srai_epi32(_mm_slli_epi32(_mm_castps_si128(v1), 16), 16);
      fxy[1] = _mm_srai_epi32(_mm_castps_si128(v1), 16);
      fxx[2] = _mm_srai_epi32(_mm_slli_epi32(_mm_castps_si128(v2), 16), 16);
      fxy[2] = _mm_srai_epi32(_mm_castps_si128(v2), 16);
    }

    __m128i A0 = _mm_sub_epi32(fxy[1], fxy[2]);
    __m128i A1 = _mm_sub_epi32(fxy[2], fxy[0]);
    __m128i A2 = _mm_sub_epi32(fxy[0], fxy[1]);

    __m128i B0 = _mm_sub_epi32(fxx[2], fxx[1]);
    __m128i B1 = _mm_sub_epi32(fxx[0], fxx[2]);
    __m128i B2 = _mm_sub_epi32(fxx[1], fxx[0]);

    __m128i C0 = _mm_sub_epi32(_mm_mullo_epi32(fxx[1], fxy[2]), _mm_mullo_epi32(fxx[2], fxy[1]));
    __m128i C1 = _mm_sub_epi32(_mm_mullo_epi32(fxx[2], fxy[0]), _mm_mullo_epi32(fxx[0], fxy[2]));
    __m128i C2 = _mm_sub_epi32(_mm_mullo_epi32(fxx[0], fxy[1]), _mm_mullo_epi32(fxx[1], fxy[0]));

    __m128i startx, endx, starty, endy;
    tri_bbox(fxx, fxy, tile_start, tile_end+1, startx, starty, endx, endy);

    startx = _mm_and_si128(startx, _mm_set1_epi32(0xFFFFFFFE));
    starty = _mm_and_si128(starty, _mm_set1_epi32(0xFFFFFFFE));

    for(int lane = 0; lane < num_simd_tris; lane++) {
      // Z-plane equation coefficients
      __m128 zz[] = {
        _mm_set1_ps(gather_buf[0].v[lane].m128_f32[3]),
        _mm_set1_ps(gather_buf[1].v[lane].m128_f32[0]),
        _mm_set1_ps(gather_buf[1].v[lane].m128_f32[1]),
      };

      int startxx = startx.m128i_i32[lane];
      int endxx   = endx.m128i_i32[lane];
      int startxy = starty.m128i_i32[lane];
      int endxy   = endy.m128i_i32[lane];

      __m128i aa0 = _mm_set1_epi32(A0.m128i_i32[lane]);
      __m128i aa1 = _mm_set1_epi32(A1.m128i_i32[lane]);
      __m128i aa2 = _mm_set1_epi32(A2.m128i_i32[lane]);

      __m128i bb0 = _mm_set1_epi32(B0.m128i_i32[lane]);
      __m128i bb1 = _mm_set1_epi32(B1.m128i_i32[lane]);
      __m128i bb2 = _mm_set1_epi32(B2.m128i_i32[lane]);

      __m128i aa0_inc = _mm_slli_epi32(aa0, 1);
      __m128i aa1_inc = _mm_slli_epi32(aa1, 1);
      __m128i aa2_inc = _mm_slli_epi32(aa2, 1);

      __m128i row, col;

      int row_idx = startxy*Size.x + 2*startxx;

      col = _mm_add_epi32(ColumnOffsets, _mm_set1_epi32(startxx));
      __m128i aa0_col = _mm_mullo_epi32(aa0, col);
      __m128i aa1_col = _mm_mullo_epi32(aa1, col);
      __m128i aa2_col = _mm_mullo_epi32(aa2, col);

      row = _mm_add_epi32(RowOffsets, _mm_set1_epi32(startxy));
      __m128i bb0_row = _mm_add_epi32(_mm_mullo_epi32(bb0, row), _mm_set1_epi32(C0.m128i_i32[lane]));
      __m128i bb1_row = _mm_add_epi32(_mm_mullo_epi32(bb1, row), _mm_set1_epi32(C1.m128i_i32[lane]));
      __m128i bb2_row = _mm_add_epi32(_mm_mullo_epi32(bb2, row), _mm_set1_epi32(C2.m128i_i32[lane]));

      __m128i sum0_row = _mm_add_epi32(aa0_col, bb0_row);
      __m128i sum1_row = _mm_add_epi32(aa1_col, bb1_row);
      __m128i sum2_row = _mm_add_epi32(aa2_col, bb2_row);

      __m128i bb0_inc = _mm_slli_epi32(bb0, 1);
      __m128i bb1_inc = _mm_slli_epi32(bb1, 1);
      __m128i bb2_inc = _mm_slli_epi32(bb2, 1);

      __m128 zx = _mm_mul_ps(_mm_cvtepi32_ps(aa1_inc), zz[1]);
      zx = _mm_add_ps(zx, _mm_mul_ps(_mm_cvtepi32_ps(aa2_inc), zz[2]));

      for(int r = startxy; r < endxy; r += 2) {
        int idx = row_idx;
        __m128i alpha = sum0_row;
        __m128i beta  = sum1_row;
        __m128i gama  = sum2_row;

        __m128 betaf = _mm_cvtepi32_ps(beta);
        __m128 gamaf = _mm_cvtepi32_ps(gama);

        __m128 depth = zz[0];
        depth = _mm_add_ps(depth, _mm_mul_ps(betaf, zz[1]));
        depth = _mm_add_ps(depth, _mm_mul_ps(gamaf, zz[2]));

        for(int c = startxx; c < endxx; c += 2) {
          __m128i mask = _mm_or_si128(_mm_or_si128(alpha, beta), gama);

          __m128 prev_depth   = _mm_load_ps(fb + idx);
          __m128 merged_depth = _mm_max_ps(depth, prev_depth);
          __m128 final_depth  = _mm_blendv_ps(merged_depth, prev_depth, _mm_castsi128_ps(mask));
          _mm_store_ps(fb + idx, final_depth);

          idx += 4;
          alpha = _mm_add_epi32(alpha, aa0_inc);
          beta  = _mm_add_epi32(beta, aa1_inc);
          gama  = _mm_add_epi32(gama, aa2_inc);
          depth = _mm_add_ps(depth, zx);
        }

        row_idx += 2*Size.x;
        sum0_row = _mm_add_epi32(sum0_row, bb0_inc);
        sum1_row = _mm_add_epi32(sum1_row, bb1_inc);
        sum2_row = _mm_add_epi32(sum2_row, bb2_inc);
      }
    }
#endif
  }

  printf("rasterized:\n");
  for(auto y = 0; y < SizeInTiles.y; y++) {
    for(auto x = 0; x < SizeInTiles.x; x++) {
      printf("%d ", m_drawn_tris[y*SizeInTiles.x + x]);
    }

    printf("\n");
  }
  printf("\n");
}

}