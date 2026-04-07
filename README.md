# DX12-Renderer

This repository contains two small DirectX learning projects:

- `DX12`: a rasterization renderer built while studying *Introduction to 3D Game Programming with DirectX 12* by Frank D. Luna
- `DXR/HelloTriangle`: a DirectX Raytracing sample based on NVIDIA's DXR tutorial

The codebase is intended as a study project rather than a polished engine. It focuses on understanding the DirectX 12 rendering pipeline, frame resources, materials, shadows, stencil-based effects, and basic DXR acceleration structures and shader binding.

## What is included

### DX12 raster renderer

The `DX12` project currently launches `ShapesApp` from [`DX12/DX12.cpp`](./DX12/DX12.cpp). From the code structure, this renderer already includes:

- root signature and PSO setup
- frame resources and constant buffer management
- procedural geometry generation
- DDS texture loading
- multiple render layers
- stencil-based mirror / reflection style passes
- planar shadow rendering
- billboard tree rendering
- imported model rendering using `Models/skull.txt`

There is also a simpler `D3D12InitApp` bootstrap sample in the same folder if you want to switch back to a more minimal starting point.

### DXR renderer

The `DXR/HelloTriangle` project is a compact DXR experiment built around:

- bottom-level and top-level acceleration structures
- ray generation, miss, hit, and shadow shaders
- shader binding table construction
- a raster path and a ray tracing path in the same sample

In the current implementation, pressing `Space` toggles between rasterization and ray tracing.

## Repository layout

```text
.
|- DX12/
|  |- DXMath/                  # math helpers used by the raster renderer
|  |- Models/                  # sample mesh data
|  |- Shaders/                 # HLSL shaders for the DX12 renderer
|  `- Textures/                # DDS/BMP textures used by the sample scenes
|- DXR/
|  `- HelloTriangle/           # DXR sample project
`- DirectX-Headers-main/       # vendored DirectX headers used by the projects
```

## Build environment

These projects are Windows-only and expect a native DirectX 12 development environment.

Recommended setup:

- Windows 10/11
- Visual Studio with C++ desktop development tools
- Windows SDK with DirectX 12 support
- a GPU and driver with DirectX 12 support
- for the DXR sample: hardware and drivers that support DirectX Raytracing

The checked-in project files currently target MSVC toolset `v145`. If your local Visual Studio installation uses a different toolset, retarget the project in Visual Studio before building.

## How to open the projects

You can open either project directly in Visual Studio:

- raster sample: [`DX12/WindowsProject1.vcxproj`](./DX12/WindowsProject1.vcxproj)
- DXR sample: [`DXR/HelloTriangle/D3D12HelloTriangle.vcxproj`](./DXR/HelloTriangle/D3D12HelloTriangle.vcxproj) or [`DXR/HelloTriangle/D3D12HelloTriangle.sln`](./DXR/HelloTriangle/D3D12HelloTriangle.sln)

## Important local path note

Some Visual Studio project settings still contain absolute include paths from the original development machine. If the project fails to compile after cloning, update the include directories to match your local checkout.

In particular, check paths that should point to:

- `DirectX-Headers-main/include/directx`
- `DX12/DXMath`
- the local project directory itself

## Third-party / reference material

- Frank D. Luna, *Introduction to 3D Game Programming with DirectX 12*
- NVIDIA DXR tutorial: [DirectX Raytracing Tutorial](https://developer.nvidia.com/rtx/raytracing/dxr/dx12-raytracing-tutorial-part-1)
- Microsoft / DirectX headers bundled in `DirectX-Headers-main`

## Current status

This repository is still evolving as a personal graphics learning project. Expect rough edges, hard-coded project settings, and experiments that prioritize understanding the API over framework cleanliness.

## License

This repository is distributed under the [MIT License](./LICENSE).
