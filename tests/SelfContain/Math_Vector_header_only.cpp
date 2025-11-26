// Compile-only self containment check for Vector.hpp
#include "Core/Math/Vector.hpp"

static_assert(sizeof(::dng::Vec3f) == 3u * sizeof(::dng::float32), "Vec3f layout must stay POD");
