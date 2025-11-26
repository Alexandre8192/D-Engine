// ============================================================================
// Mat4 Convention Test TU
// ----------------------------------------------------------------------------
// Storage  : Column-major; first index selects column, second selects row.
// Multiplication : Column vectors on the right (v' = M * v). Translation lives
//                  in column 3 so LookAt/Perspective/TransformPoint all agree.
// Purpose : Lock the convention in a compile-time test so future edits cannot
//           silently switch to row-major semantics.
// ============================================================================

#include "Core/Math/Matrix.hpp"

namespace dng
{
namespace mat4_convention_test
{
    constexpr Mat4f kScaleX2 = Mat4f::Scale(Vec3f(2.0f, 1.0f, 1.0f));
    constexpr Vec3f kInput(1.5f, -3.25f, 0.5f);
    constexpr Vec3f kTransformed = TransformVector(kScaleX2, kInput);

    static_assert(kTransformed.x == kInput.x * 2.0f,
        "Mat4f must double X when scale is written into column 0 under column-major storage");
    static_assert(kTransformed.y == kInput.y && kTransformed.z == kInput.z,
        "Mat4f must leave Y/Z untouched when scaling only the X column");
} // namespace mat4_convention_test
} // namespace dng
