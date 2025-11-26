// Compile-only self containment check for Matrix.hpp
#include "Core/Math/Matrix.hpp"

static_assert(sizeof(::dng::Mat4f) == 16u * sizeof(::dng::float32), "Mat4f layout must stay 4x4 floats");
