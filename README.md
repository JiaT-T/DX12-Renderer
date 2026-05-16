# DX12 Renderer

一个基于 DirectX 12 的实时渲染项目，当前实现重点围绕金属度-粗糙度 PBR、HDR 环境光照以及真实阴影映射展开。项目代码位于 [`DX12/`](./DX12) 目录。

## 当前特性

- 基于 `DirectX 12 + Win32` 的基础渲染框架
  - 设备与交换链初始化
  - 命令列表与命令队列管理
  - 描述符堆与多帧资源管理
- 金属度-粗糙度 PBR 材质流程
  - `baseColor`
  - `normal`
  - `roughness`
  - `metallic`
- 基于微表面模型的直接光照
  - `GGX NDF`
  - `Smith Geometry`
  - `Schlick Fresnel`
- 切线空间法线贴图
- 基于 HDR 经纬图的环境光照
  - HDR 浮点纹理加载
  - 环境贴图 mip 链生成
  - diffuse / specular IBL 近似采样
  - HDR 天空背景显示
- 真实阴影映射
  - 独立 shadow map 深度预通道
  - 比较采样
  - `3x3 PCF`
- 后处理
  - ACES tone mapping
  - gamma correction
- 资源导入
  - `tinyobjloader`
  - `stb_image`
  - `DDSTextureLoader`

## 当前场景

当前运行场景用于验证 PBR、IBL 与 shadow map 的组合效果，包含：

- 一个平面
- 一个金属球体
- 一张 HDR 环境贴图背景

当前使用的主要测试资源包括：

- [`DX12/Shaders/Color.hlsl`](./DX12/Shaders/Color.hlsl)
- [`DX12/Shaders/ShadowMap.hlsl`](./DX12/Shaders/ShadowMap.hlsl)
- [`DX12/Shaders/Sky.hlsl`](./DX12/Shaders/Sky.hlsl)
- `D:/Computer Graphics/PathTracer/PathTracer-CPP/images/Metal1`
- `D:/Computer Graphics/PathTracer/PathTracer-CPP/images/HDR/suburban_garden_2k.hdr`

## 渲染流程

当前主渲染流程可以概括为：

1. 渲染 shadow map 深度预通道
2. 渲染 HDR 天空背景
3. 渲染主场景物体
4. 在主着色阶段综合：
   - 直接光照
   - shadow map 阴影
   - diffuse IBL
   - specular IBL
5. 输出前执行 tone mapping 与 gamma correction

## 关键实现文件

- [`DX12/ShapesApp.cpp`](./DX12/ShapesApp.cpp)
  - 场景组织、渲染流程、资源绑定、shadow pass、sky pass
- [`DX12/ShapesApp.h`](./DX12/ShapesApp.h)
  - 场景状态、渲染层级、阴影资源声明
- [`DX12/FrameResources.h`](./DX12/FrameResources.h)
  - 帧资源、Pass 常量与材质常量
- [`DX12/ToolFunc.cpp`](./DX12/ToolFunc.cpp)
  - 纹理加载、HDR 浮点纹理创建、mip 链生成、基础资源辅助函数
- [`DX12/Shaders/LightingTools.hlsl`](./DX12/Shaders/LightingTools.hlsl)
  - PBR 直接光照核心计算
- [`DX12/Shaders/Color.hlsl`](./DX12/Shaders/Color.hlsl)
  - 主着色器、法线贴图、IBL、阴影采样
- [`DX12/Shaders/ShadowMap.hlsl`](./DX12/Shaders/ShadowMap.hlsl)
  - 阴影贴图深度预通道
- [`DX12/Shaders/Sky.hlsl`](./DX12/Shaders/Sky.hlsl)
  - HDR 天空背景渲染

## 构建方式

推荐环境：

- Visual Studio 2022
- Windows SDK
- x64 平台

可直接打开：

- [`DX12/D3D12.slnx`](./DX12/D3D12.slnx)
- [`DX12/WindowsProject1.vcxproj`](./DX12/WindowsProject1.vcxproj)

建议使用：

- `x64 | Debug`
- `x64 | Release`

项目中已包含：

- `DXMath`
- `tiny_obj_loader`
- `stb_image`
- `DDSTextureLoader`

## 操作方式

- 鼠标左键拖动：旋转相机
- 鼠标右键拖动：调整观察距离
- 方向键：调整主光源方向

## 当前限制

当前实现已经具备一条完整的实时 PBR 验证链路，但仍属于 forward renderer 的阶段性版本。后续仍可继续扩展：

- 更标准的 IBL 预计算流程
  - irradiance map
  - prefiltered environment map
  - BRDF LUT
- 更完整的场景资源组织
- `SSAO / SSR / SSGI`
- `DXR`

## 项目定位

该项目用于验证实时渲染中的核心基础模块如何在 DirectX 12 中完成工程化落地，包括资源管理、材质系统、环境光照、阴影映射与着色器协作。当前版本已经能够较完整地展示一条可运行的实时 PBR 主链路，并为后续扩展更复杂的实时渲染特性提供基础。
