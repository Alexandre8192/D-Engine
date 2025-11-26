// Compile-only self containment check for Math.hpp
#include "Core/Math/Math.hpp"

static_assert(::dng::Pi > 0.0f, "Math.hpp must expose Pi constant");
