// ============================================================================
// Mat4 / Quat correctness smoke tests
// ----------------------------------------------------------------------------
// These tests validate the documented conventions:
// - Mat4f stores elements column-major (`m[column][row]`).
// - Vectors are treated as column vectors (v' = M * v) just like LookAt/Perspective.
// - Right-handed rotations use the right-hand rule (positive angles rotate the
//   X axis toward Y around +Z, and toward -Z around +Y).
// The tests run at startup, allocate nothing, and rely only on the math module.
// ============================================================================

#include "Core/Math/Math.hpp"
#include "Core/Math/Vector.hpp"
#include "Core/Math/Matrix.hpp"
#include "Core/Math/Quaternion.hpp"
#include "Core/Diagnostics/Check.hpp"

namespace dng::tests
{
namespace
{
    constexpr float32 kEpsilon = 1e-4f;

    [[nodiscard]] bool NearlyEqualVec3(const Vec3f& a, const Vec3f& b) noexcept
    {
        return IsNearlyEqual(a.x, b.x, kEpsilon) &&
               IsNearlyEqual(a.y, b.y, kEpsilon) &&
               IsNearlyEqual(a.z, b.z, kEpsilon);
    }

    void RunMat4InverseTest() noexcept
    {
        // Column-major matrices composed with column vectors: final matrix is T * S
        const Mat4f scale = Mat4f::Scale(Vec3f(2.0f));
        const Mat4f translate = Mat4f::Translation(Vec3f(3.0f, -1.0f, 5.0f));
        const Mat4f composed = translate * scale;
        const Mat4f inverse = Inverse(composed);

        const Vec3f original(1.0f, -2.0f, 0.25f);
        const Vec3f worldSpace = TransformPoint(composed, original);
        const Vec3f recovered = TransformPoint(inverse, worldSpace);

        DNG_ASSERT(NearlyEqualVec3(recovered, original) && "Mat4f inverse failed round trip");
    }

    void RunQuaternionRotationTest() noexcept
    {
        // Positive +Y rotation (Pi/2) moves +X toward +Z using the right-hand rule,
        // which under the engine's convention yields (0, 0, -1).
        const Vec3f axis(0.0f, 1.0f, 0.0f);
        const Quatf q = FromAxisAngle(axis, HalfPi);
        const Mat4f rotation = ToMatrix(q);

        const Vec3f basisX(1.0f, 0.0f, 0.0f);
        const Vec3f rotated = TransformVector(rotation, basisX);
        const Vec3f expected(0.0f, 0.0f, -1.0f);

        DNG_ASSERT(NearlyEqualVec3(rotated, expected) &&
            "Quatf/ToMatrix rotation must follow right-handed Y-axis convention");
    }
}

[[maybe_unused]] void RunMathCoreTests() noexcept
{
    RunMat4InverseTest();
    RunQuaternionRotationTest();
}

} // namespace dng::tests
