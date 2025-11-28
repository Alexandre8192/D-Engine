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

- Modules/Rendering/*:
  - `NullRenderer`: does nothing, for tests/headless.
  - `BasicForwardRenderer`: classic mesh+material drawing.
  - Future: `VirtualGeometryRenderer` (Nanite-like), `RTRenderer`, etc.

All rendering modules must implement RendererAPI and can be swapped at compile-time or runtime.
