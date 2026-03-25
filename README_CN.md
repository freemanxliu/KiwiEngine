# 🥝 KiwiEngine

**[English README](README.md)**

一个从零开始构建的轻量级 3D 渲染引擎与场景编辑器，基于 C++17 和 DirectX。采用**延迟渲染管线**和 G-Buffer 可视化，以及**深度 RHI（渲染硬件接口）抽象层**——应用层代码 100% 后端无关，零 `static_cast`、零 `isDX12` 分支、零 DX11/DX12 头文件引用。

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![DirectX 11/12](https://img.shields.io/badge/DirectX-11%20%7C%2012-green)
![Vulkan](https://img.shields.io/badge/Vulkan-开发中-yellow)
![Platform](https://img.shields.io/badge/平台-Windows-lightgrey)
![Build](https://img.shields.io/badge/构建-CMake-orange)
![ImGui](https://img.shields.io/badge/ImGui-v1.91.8-purple)
![RenderDoc](https://img.shields.io/badge/RenderDoc-已集成-red)

---

## ✨ 功能特性

### 🖥️ 渲染硬件接口（RHI）

- **深度 RHI 抽象** — 应用层和场景代码不包含任何 DX11/DX12 头文件引用。所有后端特定逻辑（着色器编译、PSO 创建、ImGui 集成、命令列表生命周期、资源屏障）均隐藏在虚接口之后。
- **DX11 后端** — 完整的 DirectX 11 实现：设备、上下文、交换链、着色器、缓冲区、管线状态、ImGui 后端。
- **DX12 后端** — 完整的 DirectX 12 实现：根签名（CBV + SRV 描述符表 + 静态采样器）、PSO、描述符堆（RTV/DSV/SRV/离屏 RTV）、Fence 同步、资源屏障、上传堆缓冲区、ImGui 后端。
- **Vulkan 后端** *（开发中）* — Vulkan 实现进行中：设备、交换链、渲染通道、管线、SPIR-V 着色器。
- **运行时 RHI 切换** — 通过菜单栏在 DX11 和 DX12 之间热切换（延迟到帧边界执行，无需重启）。
- **统一着色器编译** — `RHIDevice::CompileShader()` 和 `RHIDevice::CreateGraphicsPipelineState()`，各后端内部处理编译和 PSO 创建细节。
- **帧生命周期抽象** — `RHICommandContext::BeginFrame()` / `EndFrame()` 封装 DX12 的 Reset/RootSig/DescriptorHeaps/ResourceBarrier 流程（DX11 为空操作）。
- **SRV 绑定抽象** — `RHICommandContext::SetShaderResourceView()` 实现后端无关的纹理绑定。
- **资源状态管理** — `EResourceState` 枚举和 `ResourceBarrier()` 用于 DX12 资源状态转换（DX11 为空操作）。
- **GPU 调试标注** — `RHICommandContext::BeginEvent()` / `EndEvent()` / `SetMarker()` 用于 GPU 调试器的 Pass 标记。每个渲染阶段（G-Buffer、Deferred Lighting、Buffer Visualization、Gizmo、Post-Process、ImGui）均已标注，在 RenderDoc、PIX 等 GPU 分析工具中清晰可辨。DX12 使用 `ID3D12GraphicsCommandList` 事件 API；DX11 使用 `ID3DUserDefinedAnnotation`。
- **GPU 资源调试命名** — 所有 GPU 资源（纹理、缓冲区）都携带描述性调试名称，在 RenderDoc 和 PIX 中可见。`TextureDesc::DebugName` 和 `BufferDesc::DebugName` 通过 `SetPrivateData`（DX11）和 `SetName`（DX12）自动应用。已命名的资源包括：G-Buffer RT、阴影贴图、深度缓冲、常量缓冲、网格顶点/索引缓冲（含物体名称）、Gizmo 缓冲和离屏渲染目标。

### 🔦 延迟渲染管线（UE5 风格）

- **G-Buffer** — 3 个 MRT（全部 `R8G8B8A8_UNORM`，参考 UE5 布局）：
  | RT | 内容 |
  |---|---|
  | **GBufferA** (t0) | 法线 Octahedron (RG) + 金属度 (B) + ShadingModelID (A) |
  | **GBufferB** (t1) | BaseColor (RGB) + 粗糙度 (A) |
  | **GBufferC** (t2) | 自发光 (RGB) + Specular (A) |
  | **深度** (t7) | 硬件深度 (`R32_TYPELESS`) → 通过 InvViewProj 重建世界坐标 |
- **Octahedron 法线编码** — 单位法线使用 Octahedron 映射存储在 2 个通道中，比线性 [0,1] 打包在 R8G8 格式下精度更高。
- **基于深度的位置重建** — 世界空间位置从硬件深度缓冲 + 逆 ViewProjection 矩阵重建，无需专用位置渲染目标（比 R16F 位置 RT 节省约 40% 带宽）。
- **G-Buffer 几何 Pass** — 所有场景网格使用 `GBufferPass` 着色器渲染到 3 个 MRT + 深度缓冲。通过 `CreateGraphicsPipelineState()` 和 `PipelineStateDesc` 创建 MRT PSO。
- **延迟光照 Pass** — 全屏三角形 (SV_VertexID) 从深度重建世界坐标，解码 Octahedron 法线，读取 G-Buffer 材质属性，计算 **Cook-Torrance PBR** 光照（GGX NDF + Schlick Fresnel + Smith Geometry），支持多光源，应用级联阴影贴图和 Reinhard 色调映射。
- **阴影 Pass（CSM）** — 级联阴影贴图，支持最多 4 级级联，所有级联渲染到**单张阴影 Atlas**（2x2 布局）。每个级联占 Atlas 纹理的一个象限（`R32_TYPELESS`，`2*cascadeSize × 2*cascadeSize`）。PSSM 混合对数和均匀分割方案。着色器根据视空间距离选择级联并计算 UV 偏移到 Atlas 对应区域。5 次 PCF 采样配合比较采样器实现柔和阴影边缘。
- **前向 Gizmo Pass** — 平移 Gizmo 在延迟结果之上使用前向渲染（带深度以确保正确遮挡）。
- **材质属性** — 每个 `MeshComponent` 包含 `Roughness` [0,1] 和 `Metallic` [0,1] 属性，存储在 G-Buffer 中，可通过 Inspector UI 编辑。

### 👁️ 视图模式系统

- **视图模式切换** — 菜单栏 → Rendering → View Mode。非 "Lit" 模式时，菜单栏显示当前模式指示器。
- **可用模式**：
  | 模式 | 类型 | 描述 |
  |---|---|---|
  | **Lit** | 默认 | 完整延迟渲染 + PBR 光照 |
  | **Unlit** | 调试 | 前向渲染，无光照（纯反照率） |
  | **BaseColor** | 缓冲区可视化 | G-Buffer BaseColor (GBufferB RGB) |
  | **Roughness** | 缓冲区可视化 | G-Buffer 粗糙度 (GBufferB Alpha, 灰度) |
  | **Metallic** | 缓冲区可视化 | G-Buffer 金属度 (GBufferA Blue, 灰度) |
- **缓冲区可视化** — G-Buffer Pass 执行后，专用的 `BufferVisualization` 着色器采样指定的 G-Buffer 通道并以全屏 Pass 显示。

### 🎬 场景与组件系统

- **实体-组件架构** — `SceneObject` 通过 `unique_ptr` 持有 `Component` 列表。提供模板方法 `AddComponent<T>`、`GetComponent<T>`、`RemoveComponent<T>`。
- **MeshComponent（网格组件）** — 网格数据、颜色、着色器名称、排序顺序、材质属性（粗糙度、金属度）。
- **CameraComponent（相机组件）** — 透视/正交投影、可配置 FOV、近/远裁剪面、Main Camera 开关（互斥）。激活的相机驱动引擎渲染视角。
- **DirectionalLightComponent（方向光组件）** — 方向由旋转决定，可配置颜色和强度。**级联阴影贴图（CSM）**参数：CastShadow、NumCascades (1-4)、ShadowMapResolution、ShadowDistance、CascadeSplitLambda、ShadowBias、NormalBias、ShadowStrength。
- **PointLightComponent（点光源组件）** — 基于位置的光照，可配置半径，二次衰减。
- **PostProcessComponent（后处理组件）** — 持有多个 `PostProcessMaterial`（着色器名称、强度、启用开关）。后处理效果按顺序应用。
- **场景管理** — `Scene` 存储 `vector<unique_ptr<SceneObject>>`。工厂方法：`AddMeshObject`、`AddCameraObject`、`AddDirectionalLightObject`、`AddPointLightObject`、`AddPostProcessObject`、`AddEmptyObject`。
- **JSON 序列化** — 完整的场景保存/加载，支持组件数组格式，向后兼容旧格式。

### 🎨 着色器系统

- **文件化着色器库** — 将 `.hlsl` 文件放入 `Shaders/` 文件夹，`ShaderLibrary` 在启动时自动扫描编译。通过 UI 下拉框为每个物体指定着色器。
- **内置着色器**：
  | 着色器 | 描述 |
  |---|---|
  | **Default** | Phong 光照 — Lambert 漫反射 + Blinn-Phong 高光，支持方向光和点光源（最多 8 盏），二次衰减 |
  | **DefaultLit** | 标准 PBR 风格材质 — 响应粗糙度和金属度属性，新物体默认着色器 |
  | **Unlit** | 纯色输出，无光照计算 |
  | **Wireframe** | 法线可视化 — 将世界空间法线映射为 RGB 颜色 |
  | **GBufferPass** | G-Buffer 几何 Pass — Octahedron 法线编码，输出法线+金属度、BaseColor+粗糙度、自发光+Specular 到 3 个 MRT |
  | **DeferredLighting** | 全屏 PBR 延迟光照 — 深度位置重建、Cook-Torrance BRDF (GGX + Schlick + Smith)、CSM 阴影 Atlas 采样（UV 偏移）、Reinhard 色调映射 |
  | **ShadowPass** | 仅深度顶点着色器，用于阴影贴图生成（无像素着色器） |
  | **BufferVisualization** | 调试全屏 Pass — 可视化单独的 G-Buffer 通道 |
- **统一常量缓冲区** — World/View/Projection 矩阵、物体颜色、选中状态、灯光数量、相机位置、材质属性（粗糙度、金属度）、GPU 灯光数据（最多 8 盏）。
- **自定义着色器** — 创建包含 `VSMain`/`PSMain` 入口点的 `.hlsl` 文件，使用共享的 CB 布局，放入 `Shaders/` 即可在运行时使用。

### 🌈 后处理系统

- **PostProcessShaderLibrary** — 扫描 `PostProcessShaders/` 文件夹，编译像素着色器 + 共享全屏顶点着色器，创建无 InputLayout 的 PSO。
- **全屏三角形** — 使用 `SV_VertexID`（0,1,2）生成覆盖全屏的三角形，无需顶点缓冲区。
- **Ping-Pong 渲染** — 场景 → RT[0] → 后处理链 → 后缓冲区。中间 Pass 在 RT[0] 和 RT[1] 之间交替。
- **内置效果**：
  | 效果 | 描述 |
  |---|---|
  | **Grayscale（灰度）** | 基于亮度的去色，可控制强度 |
  | **Vignette（暗角）** | 边缘变暗效果，可配置强度 |
- **逐对象后处理** — 每个 `PostProcessComponent` 管理独立的材质列表，支持排序、启用/禁用和强度调节。

### 💡 光照系统

- **多光源支持** — 单次绘制调用中支持最多 8 盏灯光（方向光 + 点光源）。
- **GPU 灯光数据** — 打包结构体：颜色+强度、类型（0=方向光、1=点光源）、方向/位置、半径。
- **级联阴影贴图（CSM）** — 方向光支持实时级联阴影贴图：
  - 最多 **4 级级联**，渲染到**单张阴影 Atlas**（2x2 布局，`2*cascadeSize × 2*cascadeSize`）
  - **Shadow Atlas** — 所有级联共享一张 `R32_TYPELESS` 深度纹理；每个级联通过 viewport/scissor 渲染到各自象限
  - **PSSM 分割方案** — 通过 lambda 参数混合对数和均匀级联分割
  - **Atlas UV 映射** — 着色器根据视空间 Z 距离选择级联，然后应用 UV 缩放 (0.5×) 和偏移采样正确的 Atlas 象限
  - **5 次 PCF 采样**，配合比较采样器实现柔和阴影边缘
  - 逐灯光可配置：阴影距离、深度偏移、法线偏移、阴影强度
  - 阴影 Pass 仅渲染深度（无像素着色器），性能最优
  - Detail 面板提供完整的阴影参数 UI 控件
- **Fallback 光照** — 场景中无灯光时，使用默认方向光 (0.5, 0.7, 0.3)。
- **AffectWorld 开关** — 每个灯光组件可独立启用/禁用。

### 🛠️ 编辑器与工具

- **ImGui 界面** — 完整的编辑器界面：菜单栏（File、Rendering）、场景面板（物体列表）、属性面板、物体放置面板。
- **平移 Gizmo** — 三轴 Gizmo（X=红、Y=绿、Z=蓝），拖拽平移物体，活动轴变为黄色。
- **射线拾取** — 点击视口选择物体，Gizmo 轴优先于场景物体。
- **程序化网格** — 内置图元：立方体、球体、圆柱体、地面。
- **内置数学库** — Vec2/3/4、Mat4（含 `Inverse()` Cramer 法则实现，用于 InvViewProj）、透视/正交投影、LookAt（左手坐标系）。
- **RenderDoc 集成** — 一键截帧（🔵 按钮），启动时自动 attach，自动打开 RenderDoc。
- **引擎配置** — 基于 INI 的单例配置系统（`Config/DefaultEngine.ini`），支持自动发现。

---

## 📋 环境要求

| 需求 | 版本 | 说明 |
|---|---|---|
| **操作系统** | Windows 10/11 | 需要 DirectX 11/12 支持 |
| **Visual Studio** | 2022 (v17.x) | Community / Professional / Enterprise |
| **MSVC 工具链** | v143+ | 通过 VS2022 "使用 C++ 的桌面开发" 工作负载安装 |
| **CMake** | 3.20+ | VS2022 自带，或单独安装 |
| **Windows SDK** | 10.0.19041+ | 包含 d3d11、d3d12、d3dcompiler、dxgi 头文件和库 |
| **RenderDoc** | 1.6+ *（可选）* | 用于帧捕获和 GPU 调试 |

---

## 🔨 构建说明

### 方式一：命令行（推荐）

```powershell
# 克隆仓库
git clone https://github.com/freemanxliu/KiwiEngine.git
cd KiwiEngine

# 创建构建目录并生成 VS 解决方案
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64

# 构建 Release 版本
cmake --build . --config Release

# 构建 Debug 版本（含调试符号）
cmake --build . --config Debug
```

可执行文件位于：`build/bin/KiwiEngine.exe`

### 方式二：Visual Studio IDE

1. 打开 Visual Studio 2022
2. **文件 → 打开 → CMake...** → 选择 `KiwiEngine/CMakeLists.txt`
3. 等待 CMake 配置完成
4. 从配置下拉框选择 **Release** 或 **Debug**
5. **生成 → 全部生成**（`Ctrl+Shift+B`）
6. **调试 → 开始执行（不调试）**（`Ctrl+F5`）

### 方式三：打开 .sln

```powershell
cd KiwiEngine/build
cmake .. -G "Visual Studio 17 2022" -A x64
# 在 Visual Studio 中打开 build/KiwiEngine.sln
```

---

## 🚀 运行

```powershell
./build/bin/KiwiEngine.exe
```

打开一个 1280×720 的 3D 场景编辑器窗口，你可以：

- **切换 RHI**：菜单栏 → Rendering → RHI → Direct3D 11 / Direct3D 12
- **切换视图模式**：菜单栏 → Rendering → View Mode → Lit / Unlit / BaseColor / Roughness / Metallic
- **添加物体**：Placer 面板 → Cube / Sphere / Cylinder / Floor / Post Process
- **添加灯光**：Placer 面板 → Directional Light / Point Light
- **添加相机**：Placer 面板 → Camera
- **选择物体**：点击视口中的物体或从物体列表选择
- **移动物体**：拖拽 Gizmo 轴（红=X、绿=Y、蓝=Z）
- **编辑属性**：Detail 面板 → 位置 / 旋转 / 缩放 / 颜色 / 着色器 / FOV / 灯光参数
- **主相机**：Detail 面板 → 勾选 Main Camera（互斥）
- **后处理**：Detail 面板 → 添加材质、调整顺序、调节强度、启用/禁用
- **帧捕获**：点击右上角 🔵 按钮 → 自动打开 RenderDoc
- **场景管理**：File 菜单 → Create Scene / Open Scene / Save Scene（JSON 格式）

---

## ⚙️ 引擎配置

KiwiEngine 使用基于 INI 的单例配置系统 `EngineConfig`。

```ini
[RenderDoc]
DllPath=C:\Program Files\RenderDoc\renderdoc.dll
Enabled=true
CapturePathTemplate=captures/kiwi_frame

[Rendering]
DefaultRHI=DX11
EnableValidation=true
VSync=true

[Window]
Width=1280
Height=720
Title=Kiwi Engine - Scene Editor
```

配置文件自动发现路径：`exe/Config/` → `../../Config/` → `exe/`

### 代码中使用

```cpp
#include "Core/EngineConfig.h"

auto& config = Kiwi::EngineConfig::Get();
config.LoadDefaultConfig();

std::string path = config.GetString("RenderDoc", "DllPath", "");
int width        = config.GetInt("Window", "Width", 1280);
bool vsync       = config.GetBool("Rendering", "VSync", true);
```

---

## 🎨 自定义着色器

### 场景着色器

在 `Shaders/` 文件夹中创建 `.hlsl` 文件，入口点为 `VSMain` 和 `PSMain`：

```hlsl
cbuffer Constants : register(b0)
{
    row_major float4x4 g_World;
    row_major float4x4 g_View;
    row_major float4x4 g_Projection;
    float4 g_ObjectColor;
    float  g_Selected;
    int    g_NumLights;
    float2 g_Padding;
    float3 g_CameraPos;
    float  g_Roughness;
    float  g_Metallic;
    float3 g_MaterialPadding;
    // GPULightData g_Lights[8];
};
```

### 后处理着色器

在 `PostProcessShaders/` 文件夹中创建 `.hlsl` 文件，入口点为 `PSMain`：

```hlsl
cbuffer PostProcessCB : register(b0)
{
    float ScreenWidth;
    float ScreenHeight;
    float Intensity;
    float Time;
};

Texture2D g_InputTexture : register(t0);
SamplerState g_Sampler : register(s0);

float4 PSMain(float2 uv : TEXCOORD, float4 pos : SV_Position) : SV_Target
{
    float4 color = g_InputTexture.Sample(g_Sampler, uv);
    // 在这里编写你的效果
    return color;
}
```

---

## 🔍 RenderDoc 集成

- **自动 Attach**：在任何图形设备创建之前自动加载
- **一键截帧**：右上角 🔵 按钮
- **视觉反馈**：捕获时按钮变橙色，悬停显示捕获次数
- **零配置**：直接运行即可，自动检测 RenderDoc
- **GPU Pass 标签**：所有渲染阶段（Shadow Pass、G-Buffer、Deferred Lighting、Buffer Visualization、Gizmo、Post-Process、ImGui）均使用 `BeginEvent`/`EndEvent` 标注，在 RenderDoc 事件浏览器中以层级分组形式显示
- **GPU 资源名称**：所有纹理和缓冲区都有描述性名称（如 `GBufferA_NormalMetallic`、`ShadowAtlas_CSM`、`MeshVB_Cube`），在 RenderDoc 的 Resource Inspector 和 Texture Viewer 中可见

**自定义 DLL 路径**（`Config/DefaultEngine.ini`）：
```ini
[RenderDoc]
DllPath=D:\Tools\RenderDoc\renderdoc.dll
```

DLL 查找优先级：已加载 → 配置文件 `[RenderDoc].DllPath`（未加载时必须配置）

---

## 📁 项目结构

```
KiwiEngine/
├── CMakeLists.txt                    # 构建配置（C++17, CMake 3.20+）
├── Config/
│   └── DefaultEngine.ini             # 引擎设置（RenderDoc、渲染、窗口）
├── include/
│   ├── RHI/
│   │   ├── RHI.h                     # 抽象接口（Device、Context、SwapChain、Buffer）
│   │   ├── RHITypes.h                # 类型定义（Format、BindFlags、ResourceState、BufferDesc）
│   │   ├── DX11/                     # DX11 头文件与实现声明
│   │   ├── DX12/                     # DX12 头文件与实现声明
│   │   └── Vulkan/                   # Vulkan 头文件与实现声明
│   ├── Core/
│   │   ├── Window.h                  # Win32 窗口封装
│   │   ├── Application.h             # 应用框架（初始化、循环、RHI 切换）
│   │   └── EngineConfig.h            # 单例 INI 配置系统
│   ├── Debug/
│   │   └── RenderDocIntegration.h    # RenderDoc In-App API 封装
│   ├── Math/
│   │   └── Math.h                    # Vec2/3/4、Mat4、投影、LookAt
│   └── Scene/
│       ├── Component.h               # 组件基类（Transform、EComponentType）
│       ├── MeshComponent.h           # 网格 + 颜色 + 着色器引用
│       ├── CameraComponent.h         # 相机（透视/正交、FOV、MainCamera）
│       ├── LightComponent.h          # 方向光与点光源组件
│       ├── PostProcessComponent.h    # 后处理材质容器
│       ├── SceneObject.h             # 实体与组件列表
│       ├── Scene.h                   # 场景管理与序列化
│       ├── Mesh.h                    # 网格数据结构
│       ├── Shaders.h                 # 内嵌 HLSL 与 CB 布局
│       ├── ShaderLibrary.h           # 文件着色器扫描与编译
│       ├── PostProcessShaders.h      # 后处理着色器定义（全屏 VS）
│       ├── PostProcessShaderLibrary.h # 后处理着色器扫描与编译
│       └── ViewMode.h                # 视图模式枚举（Lit、Unlit、BaseColor、Roughness、Metallic）
├── src/
│   ├── main.cpp                      # 入口点与场景编辑器（ImGui UI、渲染）
│   ├── Core/                         # Window、Application、EngineConfig 实现
│   ├── RHI/                          # DX11、DX12、Vulkan 后端实现
│   ├── Scene/                        # 网格生成、场景序列化
│   └── Debug/                        # RenderDoc 运行时加载
├── Shaders/                          # 场景 HLSL 着色器（Default、Unlit、Wireframe、GBufferPass、DeferredLighting、ShadowPass、BufferVisualization）
├── PostProcessShaders/               # 后处理 HLSL 着色器（Grayscale、Vignette）
├── third_party/
│   ├── imgui/                        # Dear ImGui v1.91.8
│   ├── renderdoc/                    # RenderDoc In-App API 头文件
│   └── vulkan-headers/               # Vulkan SDK 头文件
└── tools/
    └── compile_shaders.mjs           # GLSL → SPIR-V 编译器（Vulkan 用）
```

---

## 🏗️ 架构

```
┌──────────────────────────────────────────┐
│          应用层 / 场景编辑器              │  ← 100% 后端无关
│   （零 DX11/DX12 头文件引用或类型转换）   │
├──────────────────────────────────────────┤
│   组件系统                               │  ← Mesh、Camera、Light、PostProcess
│   （SceneObject → vector<Component>）    │
├──────────────────────────────────────────┤
│   着色器库                               │  ← 场景着色器 + 后处理着色器
│   （文件化，运行时编译）                  │
├──────────────────────────────────────────┤
│   调试 / RenderDoc 集成                  │  ← 一键截帧
├──────────────────────────────────────────┤
│          RHI 抽象层                      │  ← RHIDevice、RHICommandContext、
│                                          │     RHISwapChain、RHIBuffer...
├──────────────┬──────────────┬────────────┤
│    DX11      │    DX12      │  Vulkan    │  ← 后端实现
│  （已完成）   │  （已完成）   │ （开发中） │
└──────────────┴──────────────┴────────────┘
```

### 核心 RHI 虚方法

| 接口 | 方法 | 用途 |
|---|---|---|
| `RHIDevice` | `CompileShader()` | 编译 HLSL → 着色器二进制（后端内部处理细节） |
| `RHIDevice` | `CreateGraphicsPipelineState()` | DX12: 完整 PSO；DX11: 轻量包装 |
| `RHIDevice` | `CreateTexture()` / `CreateTextureView()` | 创建纹理（支持 SRV、RTV、DSV 绑定标志） |
| `RHIDevice` | `InitImGui()` / `ImGuiNewFrame()` / `ImGuiRenderDrawData()` | 后端特定的 ImGui 生命周期 |
| `RHICommandContext` | `BeginFrame()` / `EndFrame()` | DX12: 完整帧设置/收尾；DX11: 空操作 |
| `RHICommandContext` | `SetShaderResourceView()` | 绑定 SRV 到像素着色器槽位 |
| `RHICommandContext` | `ResourceBarrier()` | DX12: 资源状态转换；DX11: 空操作 |
| `RHICommandContext` | `BeginEvent()` / `EndEvent()` / `SetMarker()` | GPU 调试标注，用于 RenderDoc/PIX Pass 分组 |

### 添加新后端

1. 创建 `include/RHI/<API>/` 和 `src/RHI/<API>Device.cpp`
2. 实现 `RHI.h` 中的所有抽象接口
3. 在 `CreateRHI()` 工厂函数中添加新分支
4. **无需修改** `main.cpp`、`Application.cpp` 或任何场景代码

---

## 🔧 常见问题

| 问题 | 解决方案 |
|---|---|
| 找不到 `cmake` | 将 CMake 加入 PATH，或使用 VS2022 自带的 CMake |
| 缺少 Windows SDK | 通过 VS 安装程序 → 单个组件 → Windows 10/11 SDK 安装 |
| 链接错误（d3d11.lib） | 确保 Windows SDK 已安装并被正确检测 |
| 黑屏 | 检查控制台错误输出；确保 GPU 支持 DX11 Feature Level 11.0 |
| C4819 编译警告 | 源文件含中文注释导致，不影响编译和运行 |
| RenderDoc 未检测到 | 在 `Config/DefaultEngine.ini` 中设置 `DllPath`，或从 RenderDoc UI 启动 |
| 后处理不渲染 | 确保场景中有 PostProcess 对象且至少有一个启用的材质 |

---

## 📝 许可证

本项目仅供学习和个人使用。
