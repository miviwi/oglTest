#include <mesh/util.h>

#include <math/util.h>

#include <cmath>

namespace mesh {

std::tuple<std::vector<PNVertex>, std::vector<u16>> sphere(uint rings, uint sectors)
{
  const auto R = 1.0f / (float)(rings-1);
  const auto S = 1.0f / (float)(sectors-1);

  std::vector<PNVertex> verts;
  verts.reserve(rings*sectors);
  for(uint r = 0; r < rings; r++) {
    for(uint s = 0; s < sectors; s++) {
      auto y = sinf(-(PIf/2.0f) + PIf*r*R);
      auto x = cosf(2.0f*PIf * s * S) * sinf(PIf * r * R);
      auto z = sinf(2.0f*PIf * s * S) * sinf(PIf * r * R);

      // Flush very small values to 0.0f
      if(fabs(x) < 1e-6f) x = 0.0f;
      if(fabs(y) < 1e-6f) y = 0.0f;
      if(fabs(z) < 1e-6f) z = 0.0f;

      verts.push_back({
        { x, y, z },
        { x, y, z }, 
      });
    }
  }

  std::vector<u16> inds;
  inds.reserve(rings*sectors * 6);
  for(uint r = 0; r < rings-1; r++) {   // Avoid out-of-range indices
    for(uint s = 0; s < sectors-1; s++) {
      inds.push_back(r*sectors + s);
      inds.push_back((r+1)*sectors + s+1);
      inds.push_back(r*sectors + s+1);

      inds.push_back((r+1)*sectors + s+1);
      inds.push_back(r*sectors + s);
      inds.push_back((r+1)*sectors + s);
    }
  }

  return std::make_tuple(std::move(verts), std::move(inds));
}

std::tuple<std::vector<PVertex>, std::vector<u16>> box(float w, float h, float d)
{
  std::vector<PVertex> verts = {
    { -w, h, d },  { -w, -h, d },  { w, -h, d, },  { w, h, d },
    { -w, h, -d }, { -w, -h, -d }, { w, -h, -d, }, { w, h, -d },
  };

  std::vector<u16> inds = {
    0, 1, 2, 0, 2, 3, // Front
    0, 4, 1, 4, 5, 1, // Left
    0, 7, 4, 7, 0, 3, // Top
    3, 2, 6, 6, 7, 3, // Right
    4, 6, 5, 6, 4, 7, // Back
    1, 5, 6, 6, 2, 1, // Bottom
  };

  return std::make_tuple(std::move(verts), std::move(inds));
}

}
