// Compile-only self containment check for Quaternion.hpp
#include "Core/Math/Quaternion.hpp"

static_assert(sizeof(::dng::Quatf) == 4u * sizeof(::dng::float32), "Quatf layout must stay POD");
