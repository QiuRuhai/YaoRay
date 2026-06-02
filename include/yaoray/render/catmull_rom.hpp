#pragma once

// Catmull-Rom spline utilities — faithful port of pbrt-v4 (util/interpolation.cpp).
// Generic 1-D helpers used to interpolate, integrate, and importance-sample the
// tabulated subsurface diffusion profile. Raw-pointer API (const float* + int n)
// to match the rest of the render layer; no allocation, no throwing.

namespace yr {

// Compute the four Catmull-Rom basis weights for evaluating a spline at `x` over
// the monotonically increasing `nodes` (size n). On success returns true, sets
// `offset` so the weights apply to nodes/values indices [offset, offset+3]
// (clamped at the ends, where the corresponding weight is 0), and writes
// weights[0..3]. Returns false (leaving outputs untouched) if x is outside
// [nodes[0], nodes[n-1]].
bool CatmullRomWeights(int n, const float* nodes, float x, int& offset, float weights[4]);

// Integrate the Catmull-Rom spline defined by (x[i], values[i]) for i in [0,n),
// writing the cumulative integral into cdf[0..n-1] (cdf[0]==0). Returns the total
// integral (== cdf[n-1]).
float IntegrateCatmullRom(int n, const float* x, const float* values, float* cdf);

// Invert the Catmull-Rom spline: return the position x* in [x[0], x[n-1]] at which
// the spline through (x[i], values[i]) equals `u`. `values` must be monotonically
// non-decreasing (e.g. a CDF). Clamps to the endpoints when u is out of range.
float InvertCatmullRom(int n, const float* x, const float* values, float u);

}  // namespace yr
