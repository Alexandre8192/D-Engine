# D-Engine Math Module

## Overview
The `Source/Core/Math` module provides a high-performance, functional-style math library tailored for computer graphics and game development. It emphasizes correctness, determinism, and debug-safety.

## Coordinate System & Conventions

*   **Handedness**: **Right-Handed**
*   **Up Vector**: **+Y**
*   **Forward Vector**: **-Z** (Standard OpenGL/Vulkan convention)
*   **Clip Space**: **Z in [0, 1]** (Vulkan / DX12 style, not OpenGL -1..1)
*   **Matrices**: **Column-Major** storage and multiplication order (Pre-multiplication: `M * v`).
*   **Angles**: **Radians** by default.

## Core Types

### Vectors (`Vector.hpp`)
*   `Vec2f`, `Vec3f`, `Vec4f`: Standard float vectors.
*   **Operations**: `Dot`, `Cross` (Vec3), `Length`, `Normalize`.
*   **Safety**: `Normalize` handles zero-length vectors safely (returns zero vector).

### Matrices (`Matrix.hpp`)
*   `Mat3f`, `Mat4f`: Column-major matrices.
*   **Storage**: `m[col][row]`.
*   **Transforms**: `Translate`, `Rotate`, `Scale` helpers.
*   **Projections**:
    *   `Perspective(fovY, aspect, zNear, zFar)`: Infinite far plane supported if zFar is huge.
    *   `Orthographic(...)`.
    *   **Note**: All projections map Z to [0, 1].

### Quaternions (`Quaternion.hpp`)
*   `Quatf`: `(x, y, z, w)` storage.
*   **Interpolation**: `Slerp` (Spherical Linear Interpolation) is the default.
*   **Shortest Path**: `Slerp` automatically flips the sign to ensure the shortest rotation path.

## Utilities (`Math.hpp`)

*   **Constants**: `Pi`, `TwoPi`, `HalfPi`, `Epsilon`.
*   **Functions**: `Clamp`, `Saturate`, `Lerp`, `Min`, `Max`, `Abs`.
*   **Diagnostics**: `IsFinite`, `IsNaN`, `IsUnitLength`.
*   **Policy**: `LerpPolicy<T>` allows customizing interpolation for user types.

## Usage Example

```cpp
#include "Core/Math/Math.hpp"
#include "Core/Math/Vector.hpp"
#include "Core/Math/Matrix.hpp"

using namespace dng;

void Example()
{
    Vec3f eye = {0.0f, 1.0f, 5.0f};
    Vec3f target = {0.0f, 0.0f, 0.0f};
    Vec3f up = {0.0f, 1.0f, 0.0f};

    // Create a view matrix
    Mat4f view = Math::LookAt(eye, target, up);

    // Create a perspective projection (60 deg FOV, 16:9 aspect, 0.1 near, 1000 far)
    Mat4f proj = Math::Perspective(Math::Radians(60.0f), 16.0f/9.0f, 0.1f, 1000.0f);

    // Combine
    Mat4f viewProj = proj * view;
}
```
