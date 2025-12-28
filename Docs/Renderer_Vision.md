# D-Engine Renderer Vision

## Goals

- Provide a modular, backend-agnostic rendering contract.
- Allow projects to choose which rendering module(s) to enable:
  - Basic forward renderer.
  - Experimental GPU-driven / virtualized geometry renderer.
  - Null renderer for headless / tests.
- Keep the Core/Contracts layer free of API-specific concepts (no Vulkan/D3D names).

## Contract Philosophy

- Inputs:
  - Cameras (view/projection matrices).
  - Instances: (mesh handle, material handle, transform).
  - Lights and environment parameters.
- Outputs:
  - Rendered frames (backbuffer or render targets).
  - Optional statistics (draw calls, GPU time).

- The contract only defines:
  - POD handles (BufferHandle, TextureHandle, PipelineHandle, etc.).
  - Simple descriptors (BufferDesc, TextureDesc).
  - A small RendererAPI v-table (Init, Shutdown, BeginFrame, EndFrame, Create/Destroy resources, Submit draws).
- No hidden allocations, no exceptions, no RTTI in the contract itself.

## Modularity

- Core/Contracts/Renderer.hpp:
  - Defines the abstract RendererAPI.
  - Does not know about any specific backend.

- Modules/Renderer/Rendering/*:
  - `NullRenderer`: does nothing, for tests/headless.
  - `BasicForwardRenderer`: classic mesh+material drawing.
  - Future: `VirtualGeometryRenderer` (Nanite-like), `RTRenderer`, etc.

All rendering modules must implement RendererAPI and can be swapped at compile-time or runtime.

## Long-term Rendering Strategy (beyond basic forward)

D-Engine does not aim to clone Unreal Engine 5's Nanite or Lumen. Instead, the long-term goal is to combine several well-understood building blocks to reach "modern" performance and scalability without a hard dependency on a single technique:

- GPU-driven rendering (indirect draw calls, data-driven draw lists).
- Meshlet or cluster-based geometry layouts when needed.
- Visibility buffer or similar decoupled shading approaches to reduce bandwidth.
- Robust occlusion culling (software HiZ / Masked Occlusion Culling, or GPU-based).
- Automatic LOD and simplification tools (e.g., meshoptimizer) for classic meshes.

The renderer contract in `Core/Contracts/Renderer.hpp` is designed to support this path:

- The engine only talks to:
  - opaque handles (BufferHandle, TextureHandle, PipelineHandle, MeshHandle, MaterialHandle),
  - and a simple `FrameRenderData` struct (cameras + render instances).
- Backends are free to map `MeshHandle` to:
  - traditional vertex/index buffers,
  - meshlet/cluster hierarchies,
  - or virtualized geometry representations.

Capabilities are exposed via `RendererCaps` (mesh shading, ray tracing, virtual geometry, visibility buffer, GPU-driven culling, software occlusion, etc.). A basic forward renderer can start with all advanced flags set to `false`, while more advanced modules (e.g., "VirtualGeometryRenderer") can opt-in to additional features as they are implemented.

This keeps the contract stable and "API-agnostic" while allowing the rendering stack to evolve from a simple CPU-driven forward path to a modern hybrid (GPU-driven, meshlet-based, virtual geometry) pipeline over time.
