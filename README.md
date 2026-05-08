# DX12 Renderer

这个仓库是我拿来补实时渲染这条线的一个小项目。

起点其实是一个偏教学性质的 DirectX 12 场景框架，后面我把它往“能比较完整地讲清实时 PBR 主链路”的方向一点点往前推。现在这版重点不是堆很多大效果，而是先把基础管线、材质系统和资源绑定做扎实，让它至少能比较顺地把一套 `metal-roughness` 工作流跑起来。

项目代码主要在 [`DX12/`](./DX12) 目录下面。

## 现在已经做了什么

先说结论：这版已经不是单纯的 `Blinn-Phong + 贴图` 了，而是一个可以拿来讲实时 PBR 基本流程的小渲染器。

- 基础框架是 `DirectX 12 + Win32`，有比较完整的设备初始化、交换链、命令列表、描述符堆和多帧资源管理。
- 场景里保留了原来的一些基础内容：程序化生成的 box / grid / sphere / cylinder、`skull` 模型、billboard 树、模板测试做的镜面反射。
- 光照模型已经换成了 `Cook-Torrance`，具体是：
  - `GGX NDF`
  - `Smith Geometry`
  - `Schlick Fresnel`
- 材质这边走的是 `metal-roughness` 工作流，支持：
  - `baseColor`
  - `normal`
  - `roughness`
  - `metallic`
- 法线贴图现在是标准的切线空间 normal mapping，不是简单拿世界法线硬顶。
- 环境反射已经接进来了，会用 cubemap 做一个实时近似版的 IBL。
- 后处理这边补了 `tone mapping + gamma correction`，不会再是线性空间结果直接怼到屏幕上。
- 资源导入这边接了 `tinyobjloader + stb_image`，现在可以直接读 `OBJ + MTL`，并且把 `MTL` 里的贴图自动接到实时材质上。

当前用于验证 PBR 贴图链路的模型在：

- [`DX12/Models/Obj_PBRTest/Sphere.obj`](./DX12/Models/Obj_PBRTest/Sphere.obj)
- [`DX12/Models/Obj_PBRTest/Sphere.mtl`](./DX12/Models/Obj_PBRTest/Sphere.mtl)

这套测试资产会自动读取：

- `map_Kd`
- `norm / bump`
- `map_Pr`
- `map_Pm`

也就是把离线路径追踪器里常见的那套贴图组织方式，尽量平顺地迁到实时渲染器这边。

## 这个项目现在更像什么

如果一句话概括，我会更愿意把它叫做：

> 一个基于 DirectX 12 的实时 PBR 渲染器雏形。

这里的“雏形”不是谦虚，是因为它现在确实已经把主链路打通了，但还没有把所有现代实时渲染里更重的部分都补完。

比如说：

- 现在的阴影还不是标准 `shadow map`，目前保留的是平面投影阴影（`XMMatrixShadow` 这条路线）。
- 环境光照这边是一个够用的近似版，不是完整的 `prefiltered env map + BRDF LUT` 工业实现。
- 还没有做 `GBuffer / Deferred / SSAO / SSR / SSGI / RTGI / DXR` 这些更往后的东西。

所以如果你是来看“现在仓库里到底已经落地了多少”的，我会建议按“实时 PBR 主链路已经跑通，但阴影/GI 还在后面”这个理解来读。

## 项目里比较关键的点

### 1. 材质系统不是只改了 shader

这次改动里，真正麻烦的部分其实不是把公式写进 HLSL，而是把整条数据链改成 PBR 语义：

- C++ 侧 `Material` 结构要扩容
- `MatConstants / PassConstants` 要改
- root signature 要多挂几张 SRV
- draw call 绑定逻辑要跟着改
- shader 输入输出也要一起对齐

换句话说，实时 PBR 这件事，本质上不只是“写个 GGX 函数”，而是把渲染器的资源组织方式一起改掉。

### 2. OBJ / MTL 自动加载这条链已经接上了

我比较在意这一点，因为这能把“材质模型”和“工程落地”真正连起来。

现在导入模型的时候，不是手写几张贴图绑定上去，而是会：

1. 读 `OBJ`
2. 解析 `MTL`
3. 检查是否有 `baseColor / normal / roughness / metallic`
4. 自动创建纹理资源
5. 自动给材质分配对应的 SRV 槽位

这样后面如果继续补更多测试资产，就不需要每次都手工改一遍材质接线。

### 3. 这版和我之前路径追踪器的关系

这部分我自己是有意这样安排的。

我之前的离线路径追踪器已经能把 microfacet PBR 的原理讲清楚，但那套东西是基于随机采样、pdf、递归反弹和 MIS 的。

这个仓库补的是实时渲染这条线：

- BRDF 还是那套 microfacet 思路
- 但不再做随机积分
- 直射光改成闭式求值
- 间接镜面换成环境贴图近似
- 多次反弹暂时不做

所以这两个项目放在一起，刚好能把“离线 PBR”和“实时 PBR”这两条线对照起来看。

## 目录大概怎么分

- [`DX12/ShapesApp.cpp`](./DX12/ShapesApp.cpp)
  - 主场景组织、材质创建、OBJ/MTL 导入、render item 管理
- [`DX12/FrameResources.h`](./DX12/FrameResources.h)
  - 帧资源、Pass 常量和材质常量
- [`DX12/ToolFunc.cpp`](./DX12/ToolFunc.cpp)
  - shader 编译、默认 buffer 创建、DDS / 常规图片纹理加载
- [`DX12/Shaders/LightingTools.hlsl`](./DX12/Shaders/LightingTools.hlsl)
  - PBR 光照核心：F / D / G 和直射光求值
- [`DX12/Shaders/Color.hlsl`](./DX12/Shaders/Color.hlsl)
  - 主像素着色器：贴图采样、normal mapping、IBL 近似、tone mapping

## 编译方式

环境我这边用的是：

- Visual Studio 2022
- Windows SDK
- x64

直接打开下面任意一个都可以：

- [`DX12/D3D12.slnx`](./DX12/D3D12.slnx)
- [`DX12/WindowsProject1.vcxproj`](./DX12/WindowsProject1.vcxproj)

然后切到 `x64 | Release` 或 `x64 | Debug` 编译运行。

项目里已经带了：

- `DXMath`
- `tiny_obj_loader`
- `stb_image`
- `DDSTextureLoader`

所以正常情况下不需要额外装一堆第三方包。

## 运行时的简单操作

- `鼠标左键拖动`：绕场景旋转相机
- `鼠标右键拖动`：拉远 / 拉近
- `方向键`：调整主光方向
- `W/A/S/D`：移动 skull，顺手看一下反射和阴影的变化

## 现在还没做完的部分

这一段我想写得直白一点，免得把仓库说得太满。

- 还没有真正的 `shadow map`
- 还没有完整的预积分 IBL 管线
- 还没有 `SSAO / SSR / SSGI / DXR`
- 目前更像 forward renderer 的一版扎实基线，而不是一个已经封顶的大项目

如果后面继续往下做，我优先想补的会是：

1. `shadow map`
2. 更完整的 IBL 预计算
3. 再往后才是 GI 或更重的实时效果

## 最后

这个仓库对我来说，意义不只是“做一个 DX12 demo”。

更准确一点说，它是我把自己之前偏离线渲染的经验，往实时渲染这条线上迁的一个中间站。现在这版还没到终点，但已经能比较完整地把实时 PBR 这条主链路讲明白了。
