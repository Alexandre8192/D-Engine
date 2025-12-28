// ============================================================================
// D-Engine - tests/smoke/Math_smoke.cpp
// ----------------------------------------------------------------------------
// Purpose : Basic compile and runtime sanity tests for the Math module.
// Notes   : Verifies arithmetic, conventions, and self-containment.
// ============================================================================

#include "Core/Math/Math.hpp"
#include "Core/Math/Vector.hpp"
#include "Core/Math/Matrix.hpp"
#include "Core/Math/Quaternion.hpp"
#include "Core/Diagnostics/Check.hpp"

#include <cstdio>

using namespace dng;

int RunMathSmoke()
{
    std::printf("Running Math_smoke...\n");

    // ------------------------------------------------------------------------
    // 1. Vector Arithmetic
    // ------------------------------------------------------------------------
    {
        Vec3f a(1.0f, 2.0f, 3.0f);
        Vec3f b(4.0f, 5.0f, 6.0f);
        Vec3f c = a + b;
        DNG_ASSERT(c.x == 5.0f && c.y == 7.0f && c.z == 9.0f, "Vec3f addition failed");

        float32 d = Dot(a, b); // 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32
        DNG_ASSERT(d == 32.0f, "Vec3f Dot failed");

        Vec3f cross = Cross(Vec3f(1, 0, 0), Vec3f(0, 1, 0));
        DNG_ASSERT(cross.x == 0.0f && cross.y == 0.0f && cross.z == 1.0f, "Vec3f Cross failed (expecting Z-up RH)");
    }

    // ------------------------------------------------------------------------
    // 2. Matrix Operations
    // ------------------------------------------------------------------------
    {
        Mat4f id = Mat4f::Identity();
        Vec3f v(1.0f, 2.0f, 3.0f);
        Vec3f vTrans = TransformPoint(id, v);
        DNG_ASSERT(vTrans == v, "Mat4f Identity TransformPoint failed");

        Mat4f scale = Mat4f::Scale(Vec3f(2.0f));
        Vec3f vScaled = TransformPoint(scale, v);
        DNG_ASSERT(vScaled.x == 2.0f && vScaled.y == 4.0f && vScaled.z == 6.0f, "Mat4f Scale failed");
    }

    // ------------------------------------------------------------------------
    // 3. Quaternion & Convention Check
    // ------------------------------------------------------------------------
    {
        // Explicit Convention Check:
        // System is Right-Handed (RH).
        // Positive Z is "up" (or "back" depending on view, but here we test axis rotation).
        // Rotation of +90 deg around Z-axis (0,0,1) should map X-axis (1,0,0) to Y-axis (0,1,0).
        // Rule: Thumb along Z, fingers curl from X to Y.
        
        // Rotate +90 deg around Z
        // Expect (1,0,0) -> (0,1,0)
        Quatf q = FromAxisAngle(Vec3f(0.0f, 0.0f, 1.0f), HalfPi);
        Vec3f v(1.0f, 0.0f, 0.0f);
        
        // Test Quat * Vec
        Vec3f vRot = q * v;
        DNG_ASSERT(IsNearlyEqual(vRot.x, 0.0f), "Quat rotation X failed");
        DNG_ASSERT(IsNearlyEqual(vRot.y, 1.0f), "Quat rotation Y failed");
        DNG_ASSERT(IsNearlyEqual(vRot.z, 0.0f), "Quat rotation Z failed");

        // Test Matrix * Vec (via ToMatrix)
        Mat4f m = ToMatrix(q);
        Vec3f vMatRot = TransformPoint(m, v); // M * v
        DNG_ASSERT(IsNearlyEqual(vMatRot.x, 0.0f), "Matrix rotation X failed");
        DNG_ASSERT(IsNearlyEqual(vMatRot.y, 1.0f), "Matrix rotation Y failed");
        DNG_ASSERT(IsNearlyEqual(vMatRot.z, 0.0f), "Matrix rotation Z failed");
    }

    std::printf("Math_smoke passed.\n");
    return 0;
}
