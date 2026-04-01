# 🥝 KiwiEngine

**[English README](README.md)**

一个从零开始构建的轻量级 3D 渲染引擎与场景编辑器，基于 C++17。采用**延迟渲染管线**和 G-Buffer 可视化，以及**深度 RHI（渲染硬件接口）抽象层**，支持 **DX11、DX12、OpenGL 和 Vulkan** 运行时切换——应用层代码 100% 后端无关，零 `static_cast`、零 `isDX12` 分支、零后端头文件引用。

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![DirectX 11/12](https://img.shields.io/badge/DirectX-11%20%7C%2012-green)
![OpenGL](https://img.shields.io/badge/OpenGL-4.5-blue)
![Vulkan](https://img.shields.io/badge/Vulkan-1.2-red)
![Platform](https://img.shields.io/badge/平台-Windows-lightgrey)
![Build](https://img.shields.io/badge/构建-CMake-orange)
![ImGui](https://img.shields.io/badge/ImGui-v1.91.8-purple)
![RenderDoc](https://img.shields.io/badge/RenderDoc-已集成-red)

---

## ✨ 功能特性

### 🖥️ 渲染硬件接口（RHI）

- **深度 RHI 抽象** — 应用层和场景代码不包含任何 DX11/DX12/OpenGL/Vulkan 头文件引用。所有后端特定逻辑（着色器编译、PSO 创建、ImGui 集成、命令列表生命周期、资源屏障）均隐藏在虚接口之后。
- **DX11 后端** — 完整的 DirectX 11 实现：设备、上下文、交换链、着色器、缓冲区、管线状态、ImGui 后端。
- **DX12 后端** — 完整的 DirectX 12 实现：根签名（CBV + SRV 描述符表 + 静态采样器）、PSO、描述符堆（RTV/DSV/SRV/离屏 RTV）、Fence 同步、资源屏障、上传堆缓冲区、ImGui 后端。
- **OpenGL 后端** — 完整的 OpenGL 4.5 核心配置实现：WGL 上下文创建、glad2 加载器、基于 FBO 的渲染目标、GLSL 着色器编译与程序链接、前向渲染路径、ImGui OpenGL3 后端。
- **Vulkan 后端** — Vulkan 1.2 实现：VkInstance（含 Validation Layer）、物理/逻辑设备、交换链（Win32 Surface）、Render Pass、Framebuffer、命令缓冲（Fence 同步）、描述符池/集、VkPipeline 创建、SPIR-V Shader Module、ImGui Vulkan 后端。前向渲染路径。
- **运行时 RHI 切换** — 通过菜单栏在 DX11、DX12、OpenGL 和 Vulkan 之间热切换（延迟到帧边界执行，无需重启）。
- **双轨着色器编译** — DX11 使用 FXC（SM 5.0 → DXBC）；DX12 使用 DXC（SM 6.0 → DXIL）并保留 FXC 回退；OpenGL 使用驱动原生 GLSL 编译。
- **统一着色器编译** — `RHIDevice::CompileShader()` 和 `RHIDevice::CreateGraphicsPipelineState()`，各后端内部处理编译和 PSO 创建细节。
- **帧生命周期抽象** — `RHICommandContext::BeginFrame()` / `EndFrame()` 封装 DX12 的 Reset/RootSig/DescriptorHeaps/ResourceBarrier 流程（DX11/GL 为空操作）。
- **SRV 绑定抽象** — `RHICommandContext::SetShaderResourceView()` 实现后端无关的纹理绑定。
- **资源状态管理** — `EResourceState` 枚举和 `ResourceBarrier()` 用于 DX12 资源状态转换（DX11/GL 为空操作）。
- **GPU 调试标注** — `RHICommandContext::BeginEvent()` / `EndEvent()` / `SetMarker()` 用于 GPU 调试器的 Pass 标记。每个渲染阶段（G-Buffer、Deferred Lighting、Skybox、Buffer Visualization、Gizmo、Post-Process、ImGui）均已标注，在 RenderDoc、PIX 等 GPU 分析工具中清晰可辨。DX12 使用 `ID3D12GraphicsCommandList` 事件 API；DX11 使用 `ID3DUserDefinedAnnotation`。
- **GPU 资源调试命名** — 所有 GPU 资源（纹理、缓冲区）都携带描述性调试名称，在 RenderDoc 和 PIX 中可见。`TextureDesc::DebugName` 和 `BufferDesc::DebugName` 通过 `SetPrivateData`（DX11）和 `SetName`（DX12）自动应用。

### 🔧 着色器编译管线

| 后端 | 编译器 | Shader Model | 输出格式 | 备注 |
|------|--------|-------------|---------|------|
| **DX11** | FXC (`d3dcompiler.dll`) | SM 5.0 | DXBC | 传统编译器，稳定可靠 |
| **DX12** | DXC (`dxcompiler.dll`) | SM 6.0 | DXIL | 现代编译器，自动通过 `UpgradeProfileForDXC()` 将 `vs_5_0` 升级为 `vs_6_0`，保留 FXC 回退 |
| **OpenGL** | 驱动 GLSL | GLSL 450 | 原生 | `.glsl` 文件使用 `//!VERTEX` / `//!FRAGMENT` 分割标记 |
| **Vulkan** | glslang / shaderc | GLSL 450 → SPIR-V | SPIR-V | 预编译或运行时 GLSL→SPIR-V；每阶段一个 `VkShaderModule` |

- **DXC 集成** — `DXCCompiler` 单例封装 `IDxcCompiler3`，运行时通过 `LoadLibrary("dxcompiler.dll")` 加载。DXC 运行时 DLL（`dxcompiler.dll` + `dxil.dll`）由 CMake 自动复制到输出目录。
- **ShaderLibrary** 根据当前活跃的 RHI 后端自动选择 HLSL（`Shaders/`）或 GLSL（`GLShaders/`）源文件。

### 🔦 延迟渲染管线（UE5 风格）

- **G-Buffer** — 3 个 MRT（全部 `R8G8B8A8_UNORM`，UE5 标准布局）：
  | RT | 内容 |
  |---|---|
  | **GBufferA** (t0) | 法线 Octahedron (RG) + Normal.z (B) + PerObjectData (A) |
  | **GBufferB** (t1) | 金属度 (R) + Specular (G) + 粗糙度 (B) + ShadingModelID (A) |
  | **GBufferC** (t2) | BaseColor (RGB) + AO (A) |
  | **深度** (t7) | 硬件深度 (`R32_TYPELESS`) → 通过 InvViewProj 重建世界坐标 |
- **Octahedron 法线编码** — 单位法线使用 Octahedron 映射存储在 2 个通道中，比线性 [0,1] 打包在 R8G8 格式下精度更高。
- **基于深度的位置重建** — 世界空间位置从硬件深度缓冲 + 逆 ViewProjection 矩阵重建，无需专用位置渲染目标（比 R16F 位置 RT 节省约 40% 带宽）。
- **G-Buffer 几何 Pass** — 所有场景网格使用 `GBufferPass` 着色器渲染到 3 个 MRT + 深度缓冲。顶点着色器通过 interpolants 将 per-object 材质数据（粗糙度、金属度、纹理标志、选中状态）传递到像素着色器。
- **Multi-Pass 延迟光照（UE5）** — 光照拆分为多个 Pass，而非单次全屏绘制：
  - **Ambient pass** — 不透明全屏 Pass，计算环境光/IBL 贡献。
  - **Per-light additive passes** — 每个光源一次全屏绘制，使用 additive blending (`SrcAlpha ONE`)。无硬性光源数量限制；每个 Pass 绑定独立的 `LightUniformBuffer` (b3)。
  - **PipelineStateDesc.AdditiveBlend** — 新增 blend state 用于多光源累积。
- **延迟光照 BRDF** — 对齐 UE5 `DefaultLitBxDF`：**D_GGX**（Trowbridge-Reitz NDF）、**Vis_SmithJointApprox**（联合 Smith 可见性，分母已内置）、**F_Schlick**（含 2% 反射率阴影阈值）、**Diffuse_Burley**（Disney 漫反射，粗糙度相关）、**EnvBRDFApprox**（Lazarov 2013 解析近似，无需 LUT）用于间接高光。支持级联阴影贴图。
- **阴影 Pass（CSM）** — 级联阴影贴图，支持最多 4 级级联，所有级联渲染到**单张阴影 Atlas**（2x2 布局）。每个级联占 Atlas 纹理的一个象限（`R32_TYPELESS`，`2*cascadeSize × 2*cascadeSize`）。PSSM 混合对数和均匀分割方案。着色器根据视空间距离选择级联并计算 UV 偏移到 Atlas 对应区域。5 次 PCF 采样配合比较采样器实现柔和阴影边缘。
- **HDR 管线** — 离屏渲染目标使用 `R16G16B16A16_FLOAT` 格式。所有后处理在 HDR 空间操作，最终的 **Tonemap pass**（ACES Filmic）将 HDR 转换为 LDR 后呈现到交换链。内置 `Tonemap.hlsl`。
- **前向 Gizmo Pass** — 变换 Gizmo（平移/旋转/缩放）在延迟结果之上使用前向渲染（带深度以确保正确遮挡）。
- **材质属性** — 材质资产定义粗糙度 [0,1]、金属度 [0,1]、基础颜色和贴图。法线贴图通过 TBN 矩阵应用（每顶点切线 Vec4 含 handedness）。属性存储在 G-Buffer 中，可通过材质编辑器/Inspector UI 编辑。

> **注意**：延迟渲染管线（G-Buffer、CSM 阴影、Multi-Pass 延迟光照）在 DX11 和 DX12 后端下激活。OpenGL 和 Vulkan 后端使用前向渲染路径。

### 👁️ 视图模式系统

- **视图模式切换** — 菜单栏 → Rendering → View Mode。非 "Lit" 模式时，菜单栏显示当前模式指示器。
- **可用模式**：
  | 模式 | 类型 | 描述 |
  |---|---|---|
  | **Lit** | 默认 | 完整延迟渲染 + UE5 风格 PBR 光照 |
  | **Unlit** | 调试 | 前向渲染，无光照（纯反照率） |
  | **BaseColor** | 缓冲区可视化 | G-Buffer BaseColor (GBufferC RGB) |
  | **Roughness** | 缓冲区可视化 | G-Buffer 粗糙度 (GBufferB Blue, 灰度) |
  | **Metallic** | 缓冲区可视化 | G-Buffer 金属度 (GBufferB Red, 灰度) |
  | **Normal** | 缓冲区可视化 | 解码世界空间法线 (GBufferA, 重映射到 [0,1]) |
  | **Specular** | 缓冲区可视化 | G-Buffer Specular (GBufferB Green, 灰度) |
  | **AO** | 缓冲区可视化 | G-Buffer 环境遮蔽 (GBufferC Alpha, 灰度) |
- **缓冲区可视化** — G-Buffer Pass 执行后，专用的 `BufferVisualization` 着色器采样指定的 G-Buffer 通道并以全屏 Pass 显示。

### 🧱 材质系统

- **材质资产** — `.mat` 文件（JSON 格式），独立于网格数据定义材质属性。每个材质引用一个着色器并定义属性值（浮点数、颜色、贴图）。
- **MaterialLibrary** — 单例材质管理器。首次运行自动创建 `Default-Material.mat`，启动时扫描 `Materials/` 文件夹。材质通过名称从 `MeshComponent` 引用。
- **着色器属性元数据** — 着色器可声明 `// @Properties { }` 块定义 UI 可见属性（Float、Range、Color、Texture2D）。材质编辑器解析这些元数据生成动态属性 UI。
- **材质编辑器** — 在 Content Browser 中双击 `.mat` 文件打开浮动编辑器窗口。功能：着色器选择下拉框、基于 @Properties 的动态 UI（含颜色预览色块）、贴图槽位行（Pick 按钮 + 模态弹窗 + Content Browser 拖放）、Save/Close 按钮。
- **属性迁移** — 所有外观属性（颜色、粗糙度、金属度、贴图路径）定义在 Material 中，不在 MeshComponent 中。UploadObjectUB 和贴图绑定在渲染时从材质读取。

### 🖼️ 纹理系统

- **TextureManager** — 通过 stb_image 运行时加载纹理。支持 PNG、JPG、BMP、TGA 格式。基于路径的缓存确保每张纹理只加载一次。
- **默认纹理** — 始终可用的回退纹理：白色（1×1）、黑色（1×1）、平坦法线（128,128,255 — 中性法线贴图）。
- **GPU 资源生命周期** — RHI 切换时所有纹理正确释放。DX11：修复 SysMemPitch；DX12：上传堆初始化数据。
- **纹理选择器** — 模态弹窗列出 `Textures/` 目录中所有文件供选择。同时支持从 Content Browser 拖放（`KIWI_TEXTURE` 载荷）。

### 📁 内容浏览器

- **资产浏览器窗口** — 从 Window 菜单打开的浮动窗口。左侧：文件夹树（Scenes/Shaders/GLShaders/PostProcessShaders/Textures/Materials）。右侧：文件表格。
- **右键菜单** — Show In Explorer（在系统文件管理器中高亮）+ Open File（使用默认应用打开）。
- **双击行为** — `.json` → 加载场景；`.mat` → 打开材质编辑器；`.png/.jpg` → 预加载纹理到 GPU 缓存。
- **拖放** — 纹理文件可从 Content Browser 拖放到材质编辑器的贴图槽位。

### 🖼️ 多视口（ImGui Docking）

- **ImGui Docking 分支** — 升级到 ImGui docking 分支，支持窗口停靠和多视口。
- **可拖出窗口** — 任何 ImGui 窗口都可拖出主窗口成为独立 OS 窗口，各自拥有独立的交换链。
- **ViewportsEnabled** — 完整启用视口拖拽和多窗口支持。

### 🎬 场景与组件系统

- **实体-组件架构** — `SceneObject` 通过 `unique_ptr` 持有 `Component` 列表。提供模板方法 `AddComponent<T>`、`GetComponent<T>`、`RemoveComponent<T>`。
- **MeshComponent（网格组件）** — 网格数据、材质名称引用、排序顺序、图元类型。外观属性（颜色、粗糙度、金属度、贴图）定义在 Material 资产中。
- **CameraComponent（相机组件）** — 透视/正交投影、可配置 FOV、近/远裁剪面、Main Camera 开关（互斥）。激活的相机驱动引擎渲染视角。
- **DirectionalLightComponent（方向光组件）** — 方向由旋转决定，可配置颜色和强度。**级联阴影贴图（CSM）**参数：CastShadow、NumCascades (1-4)、ShadowMapResolution、ShadowDistance、CascadeSplitLambda、ShadowBias、NormalBias、ShadowStrength。
- **PointLightComponent（点光源组件）** — 基于位置的光照，可配置半径，二次衰减。
- **PostProcessComponent（后处理组件）** — 持有多个 `PostProcessMaterial`（着色器名称、强度、启用开关）。后处理效果按顺序应用。
- **场景管理** — `Scene` 存储 `vector<unique_ptr<SceneObject>>`。工厂方法：`AddMeshObject`、`AddCameraObject`、`AddDirectionalLightObject`、`AddPointLightObject`、`AddPostProcessObject`、`AddEmptyObject`。
- **JSON 序列化** — 完整的场景保存/加载，支持组件数组格式，向后兼容旧格式。File 菜单 → Save Scene（命名对话框）、Open Scene（自动扫描 `Scenes/` 目录）、Create Scene。默认场景（`Scenes/Default.json`）首次运行自动创建。
- **窗口标题** — 显示当前场景名称："Kiwi Engine - \<场景名\>"。

### 🎨 着色器系统

- **文件化着色器库** — 将 `.hlsl` 文件放入 `Shaders/` 文件夹（或 `.glsl` 放入 `GLShaders/`），`ShaderLibrary` 在启动时自动扫描编译，根据当前 RHI 后端自动选择正确的文件夹。通过 UI 下拉框为每个物体指定着色器。
- **内置着色器**：
  | 着色器 | 描述 |
  |---|---|
  | **Default** | Phong 光照 — Lambert 漫反射 + Blinn-Phong 高光，支持方向光和点光源（最多 8 盏），二次衰减 |
  | **DefaultLit** | 标准 PBR 风格材质 — 响应粗糙度和金属度属性，新物体默认着色器 |
  | **Unlit** | 纯色输出，无光照计算 |
  | **Wireframe** | 法线可视化 — 将世界空间法线映射为 RGB 颜色 |
  | **GBufferPass** | G-Buffer 几何 Pass — Octahedron 法线编码，**TBN 法线贴图**（切线空间法线→世界空间，Gram-Schmidt 正交化 + handedness），输出法线+PerObjectData、金属度+Specular+粗糙度+ShadingModelID、BaseColor+AO 到 3 个 MRT |
  | **DeferredAmbient** | 全屏环境光 Pass — 计算环境光/IBL 贡献 |
  | **DeferredLighting** | Per-light 全屏 additive PBR Pass（UE5 DefaultLitBxDF）— D_GGX + Vis_SmithJointApprox + F_Schlick + Diffuse_Burley + EnvBRDFApprox、CSM 阴影 Atlas 采样。每个光源一次全屏绘制，additive blending |
  | **ShadowPass** | 仅深度顶点着色器，用于阴影贴图生成（无像素着色器）。支持 CB offset（默认）和 SV_InstanceID（USE_GPU_SCENE_INSTANCING）两种顶点着色器路径 |
  | **BufferVisualization** | 调试全屏 Pass — 可视化单独的 G-Buffer 通道（BaseColor、Roughness、Metallic、Normal、Specular、AO） |
  | **Skybox** | 全屏 Pass — 在 depth==1 像素上采样 HDR Equirectangular 环境贴图，逆 ViewProj 方向重建 |
  | **Tonemap** | ACES Filmic 色调映射（HDR → LDR），作为最终后处理 Pass |
- **GLSL 着色器** — OpenGL 版本的 DefaultLit、Unlit 和 Wireframe 存放在 `GLShaders/` 目录，使用 `//!VERTEX` / `//!FRAGMENT` 标记分割着色器阶段。
- **共享 Shader Include** — `Common.hlsli` 定义分层 CB 布局，所有 HLSL 着色器通过 `#include "Common.hlsli"` 引用。编译时自动展开 `#include`。
- **UE5 风格常量缓冲区布局** — 按更新频率拆分为 5 个缓冲区：
  | 寄存器 | 缓冲区 | 更新频率 | 内容 |
  |---|---|---|---|
  | **b0** | `ViewUniformBuffer` | 每帧 1 次 | View/Proj/ViewProj/InvViewProj 矩阵、CameraPos、屏幕尺寸、Near/Far、灯光数量 + 灯光数组[8] |
  | **b1** | `ObjectUniformBuffer` | 每次绘制 | World 矩阵、物体颜色、选中状态、粗糙度、金属度、纹理标志、可视化模式 |
  | **b2** | `ShadowUniformBuffer` | 每帧 1 次 | LightViewProj[4]、级联分割距离、阴影偏移/强度、级联数量 |
  | **b3** | `LightUniformBuffer` | 每光源 Pass | LightColor、LightDirection、LightPosition、LightType、LightRadius、ShadowAtlasUV |
  | **b4** | `BatchUniformBuffer` | 每 Batch（预留） | BatchStartIndex，用于 GPU Scene instanced drawing |
- **自定义着色器** — 创建 `.hlsl` 文件，`#include "Common.hlsli"` 获取 CB 布局，定义 `VSMain`/`PSMain` 入口点，放入 `Shaders/` 即可运行。

### 🧠 GPU Scene 系统

- **GPUScene 类** (`include/Scene/GPUScene.h`, `src/Scene/GPUScene.cpp`) — 统一 GPU Scene 管理器（参考 UE5 `FPrimitiveSceneData`）：
  - 收集所有可见图元的 `ObjectUniformBuffer` 数据到单个大 CB。
  - `Update()` — 每帧从场景采集数据 + 构建 batch 分类。
  - `UploadToGPU()` — 上传到 CB 用于 single-draw offset binding。
  - `BindPrimitive()` — 通过 CB offset (b1) 绑定指定图元的 256B 窗口。
- **Batch 分类** — 视锥体剔除后，图元按 MeshID → Material → front-to-back 排序，分类为两条渲染路径：
  | 路径 | 条件 | 描述 |
  |---|---|---|
  | **InstanceBatch** | count ≥ 2，相同 mesh + 材质 | VB/IB/SRV 只绑一次，per-instance 循环 CB offset |
  | **SingleDrawItem** | 唯一 mesh 或材质组合 | 普通 CB offset + DrawIndexed |
- **共享 Mesh Pool** — 相同 `EPrimitiveType`（如 50 个 Cube）共享同一套 VB/IB。减少 GPU 内存和绑定开销。
- **冗余绑定消除** — VB/IB 和纹理 SRV 绑定缓存，连续相同资源跳过绑定。
- **帧级上传** — GPU Scene 数据在帧开始时上传一次，所有渲染 Pass（Shadow、G-Buffer、Lighting）共享同一份数据。
- **DX11.1 CB Offset** — 使用 `VSSetConstantBuffers1`（DX11.1）+ 缓存 `ID3D11DeviceContext1`。
- **DX12 CB Offset** — 使用 GPU 虚拟地址 + 偏移计算。
- **未来: StructuredBuffer Instancing** — RHI 已提供 `CreateBufferSRV()` 和 `DrawIndexedInstanced()`（DX11+DX12）。Shader 已预留 `USE_GPU_SCENE_INSTANCING` 路径（SV_InstanceID + StructuredBuffer(t8)），待激活。

### 🌈 后处理系统

- **PostProcessShaderLibrary** — 扫描 `PostProcessShaders/` 文件夹，编译像素着色器 + 共享全屏顶点着色器，创建无 Input Layout 的 PSO。
- **全屏三角形** — 使用 `SV_VertexID` (0,1,2) 生成覆盖屏幕的三角形，无需顶点缓冲区。
- **Ping-Pong 渲染** — Scene → RT[0] → 后处理链 → backbuffer。中间 Pass 在 RT[0] 和 RT[1] 之间交替。
- **HDR 管线** — 离屏 RT 使用 `R16G16B16A16_FLOAT`。所有后处理在 HDR 空间操作，最终 Tonemap 转换为 LDR。
- **内置效果**：
  | 效果 | 描述 |
  |---|---|
  | **Grayscale** | 亮度去饱和，可控制强度 |
  | **Vignette** | 可配置强度的暗角效果 |
  | **Tonemap** | ACES Filmic 色调映射（HDR → LDR），作为最终后处理 Pass |

- **Per-Object Post-Process** — 每个 `PostProcessComponent` 管理独立的材质列表，支持排序、启用/禁用和强度调节。

### ⚡ 性能与 Draw Call 优化

- **共享 Mesh Pool** — 相同 `EPrimitiveType`（如 50 个 Cube）共享同一套 VB/IB。减少 GPU 内存和绑定开销。
- **GPU Scene Batch 分类** — 图元按 MeshID → Material → front-to-back 排序。连续相同 mesh+material 的图元分组为 `InstanceBatch`（共享 VB/IB/SRV 绘制）。
- **冗余绑定消除** — VB/IB 和纹理 SRV 绑定缓存，连续相同资源跳过绑定。
- **双渲染路径** — Instance Batch（count ≥ 2）只绑一次 VB/IB/SRV 然后 per-instance 循环；Single Draw 使用普通 CB offset binding。两条路径均应用于 Deferred、Forward 和 Shadow 三个渲染路径。

### 💡 光照系统

- **Multi-Pass 延迟光照** — 每个光源作为独立的 fullscreen additive pass 渲染：
  - **Ambient pass**（不透明）— 计算环境光/IBL 贡献。
  - **Per-light pass**（additive blend）— 每个光源一次全屏绘制，绑定独立的 `LightUniformBuffer` (b3)。无硬性光源数量限制。
- **无限光源** — 光源数量不受固定数组大小限制。每个光源 Pass 绑定独立的 CB。
- **GPU 灯光数据** — Per-light `LightUniformBuffer`：颜色+强度、类型（0=方向光、1=点光源）、方向/位置、半径、阴影 Atlas UV 参数。
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
- **相机导航** — 按住鼠标右键 + WASD/方向键飞行移动主相机（水平面移动）；按住右键 + 鼠标拖动旋转相机方向（Yaw/Pitch，灵敏度 0.15°/px，Pitch 夹紧 ±89°）。
- **相机设置** — 右上角 📷 按钮打开弹窗，可调节移动速度（0.5–50 units/s）和 FOV（10–120°）滑块。
- **平移 Gizmo** — 三轴 Gizmo（X=红、Y=绿、Z=蓝），拖拽平移物体，活动轴变为黄色。**恒定屏幕大小** — Gizmo 根据相机距离自动缩放，无论缩放级别如何，始终保持相同的像素大小。
- **旋转/缩放 Gizmo** — 三模式变换 Gizmo（平移/旋转/缩放），通过 W/E/R 快捷键或工具栏按钮切换。旋转使用环形手柄 + 屏幕空间角度拾取；缩放使用轴+立方体手柄 + 射线投影。右上角显示当前模式图标。
- **屏幕空间 Gizmo 拾取** — Gizmo 轴选择使用 2D 屏幕空间距离（像素阈值）和平移/缩放轴的环点云采样（40 个采样点）来选择旋转环，无论相机角度或距离如何，都能提供可靠直觉的拾取体验。
- **模型导入** — 加载外部 `.obj`（Wavefront OBJ，通过 tinyobjloader）和 `.fbx`（Autodesk FBX，通过 ufbx）模型文件。每个子网格成为独立的网格物体，附带材质漫反射颜色。自动顶点去重、多边形三角化、平面/平滑法线计算和 UV 坐标翻转。
- **射线拾取** — 点击视口选择物体，Gizmo 轴优先于场景物体。
- **程序化网格** — 内置图元：立方体、球体、圆柱体、地面。
- **内置数学库** — Vec2/3/4、Mat4（含 `Inverse()` Cramer 法则实现，用于 InvViewProj）、透视/正交投影、LookAt（左手坐标系）。
- **RenderDoc 集成** — 一键截帧（🔵 按钮），启动时自动 attach，自动打开 RenderDoc。默认 RHI 为 Vulkan 时自动跳过 RenderDoc（与 NVIDIA OpenGL/Vulkan 驱动钩子不兼容）。
- **引擎配置** — 基于 INI 的单例配置系统（`Config/DefaultEngine.ini`），支持自动发现。

---

## 📋 环境要求

| 需求 | 版本 | 说明 |
|---|---|---|
| **操作系统** | Windows 10/11 | 需要 DirectX 11/12 和 OpenGL 4.5 支持 |
| **Visual Studio** | 2022 (v17.x) | Community / Professional / Enterprise |
| **MSVC 工具链** | v143+ | 通过 VS2022 "使用 C++ 的桌面开发" 工作负载安装 |
| **CMake** | 3.20+ | VS2022 自带，或单独安装 |
| **Windows SDK** | 10.0.19041+ | 包含 d3d11、d3d12、d3dcompiler、dxgi 头文件和库 |
| **GPU** | DX11 FL 11.0+ / OpenGL 4.5 | 大多数现代 GPU 都支持 |
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

- **切换 RHI**：菜单栏 → Rendering → RHI → Direct3D 11 / Direct3D 12 / OpenGL / Vulkan
- **切换视图模式**：菜单栏 → Rendering → View Mode → Lit / Unlit / BaseColor / Roughness / Metallic
- **相机导航**：按住右键 + WASD 飞行移动；右键 + 鼠标拖动旋转视角
- **相机设置**：点击右上角 📷 按钮调节移动速度和 FOV
- **添加物体**：Placer 面板 → Cube / Sphere / Cylinder / Floor / Post Process
- **添加灯光**：Placer 面板 → Directional Light / Point Light
- **添加相机**：Placer 面板 → Camera
- **选择物体**：点击视口中的物体或从物体列表选择
- **变换物体**：W（平移）/ E（旋转）/ R（缩放）— 拖拽 Gizmo 轴变换物体；右上角模式按钮
- **编辑属性**：Detail 面板 → Transform / Material / Shader / FOV / 灯光参数
- **主相机**：Detail 面板 → 勾选 Main Camera（互斥）
- **后处理**：Detail 面板 → 添加材质、调整顺序、调节强度、启用/禁用
- **材质编辑器**：Content Browser → 双击 `.mat` 文件 → 编辑着色器、属性、贴图；可从浏览器拖入贴图
- **内容浏览器**：Window 菜单 → 浏览 Scenes/Shaders/Textures/Materials；拖放贴图；右键打开 Explorer
- **帧捕获**：点击右上角 🔵 按钮 → 自动打开 RenderDoc
- **场景管理**：File 菜单 → Create Scene / Open Scene（扫描 `Scenes/` 文件夹） / Save Scene（命名对话框，JSON 格式）
- **多视口**：拖拽面板边框停靠；拖拽窗口标题栏出主窗口创建独立 OS 窗口

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

### 场景着色器（HLSL — DX11/DX12）

在 `Shaders/` 文件夹中创建 `.hlsl` 文件，入口点为 `VSMain` 和 `PSMain`。包含 `Common.hlsli` 以使用 UE5 风格的分层常量缓冲区布局：

```hlsl
#include "Common.hlsli"

// b0 = ViewUniformBuffer（每帧自动上传）
// b1 = ObjectUniformBuffer（每绘制调用自动上传）

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD;
};

struct VSOutput
{
    float4 PositionCS : SV_POSITION;
    float3 PositionWS : POSITION;
    float3 NormalWS   : NORMAL;
    float4 Color      : COLOR;
    float2 TexCoord   : TEXCOORD;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4 worldPos = mul(float4(input.Position, 1.0), g_World);
    float4 viewPos = mul(worldPos, g_View);
    output.PositionCS = mul(viewPos, g_Projection);
    output.PositionWS = worldPos.xyz;
    output.NormalWS = mul(input.Normal, (float3x3)g_World);
    output.Color = input.Color * g_ObjectColor;
    output.TexCoord = input.TexCoord;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    // 通过 ViewUB(b0) 访问 g_CameraPos、g_Lights、g_Selected
    // 通过 ObjectUB(b1) 访问 g_World、g_ObjectColor、g_Roughness、g_Metallic
    return float4(input.Color.rgb, input.Color.a);
}
```

### 场景着色器（GLSL — OpenGL）

在 `GLShaders/` 文件夹中创建 `.glsl` 文件，使用 `//!VERTEX` 和 `//!FRAGMENT` 标记：

```glsl
//!VERTEX
#version 450 core
layout(std140, binding = 0) uniform Constants { ... };
layout(location = 0) in vec3 aPos;
void main() { gl_Position = ...; }

//!FRAGMENT
#version 450 core
out vec4 FragColor;
void main() { FragColor = vec4(1.0); }
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

- **自动 Attach**：在任何图形设备创建之前自动加载（默认 RHI 为 Vulkan 时跳过）
- **一键截帧**：右上角 🔵 按钮
- **视觉反馈**：捕获时按钮变橙色，悬停显示捕获次数
- **零配置**：直接运行即可，自动检测 RenderDoc
- **GPU Pass 标签**：所有渲染阶段（Shadow Pass、G-Buffer、Deferred Lighting、Skybox、Buffer Visualization、Gizmo、Post-Process、ImGui）均使用 `BeginEvent`/`EndEvent` 标注，在 RenderDoc 事件浏览器中以层级分组形式显示
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
├── docs/
│   └── ShaderCompilationPipeline.drawio  # 着色器编译流程图
├── include/
│   ├── RHI/
│   │   ├── RHI.h                     # 抽象接口（Device、Context、SwapChain、Buffer）
│   │   ├── RHITypes.h                # 类型定义（Format、BindFlags、ResourceState、BufferDesc）
│   │   ├── README.md                 # RHI 架构文档
│   │   ├── DX11/                     # DX11 后端头文件
│   │   │   ├── DX11Headers.h         # DX11 系统头文件
│   │   │   ├── DX11Utils.h           # DX11 工具函数
│   │   │   ├── DX11Resources.h       # DX11 资源包装类
│   │   │   └── DX11Device.h          # DX11 Device / SwapChain / CommandContext
│   │   ├── DX12/                     # DX12 后端头文件
│   │   │   ├── DX12Headers.h         # DX12 系统头文件
│   │   │   ├── DX12Resources.h       # DX12 资源包装类
│   │   │   └── DX12Device.h          # DX12 Device / SwapChain / CommandContext
│   │   ├── GL/                       # OpenGL 后端头文件
│   │   │   ├── GLHeaders.h           # OpenGL / WGL 系统头文件
│   │   │   ├── GLResources.h         # GL 资源包装类
│   │   │   └── GLDevice.h            # GL Device / SwapChain / CommandContext
│   │   ├── Vulkan/                   # Vulkan 后端头文件
│   │   │   ├── VulkanHeaders.h       # Vulkan / Win32 系统头文件
│   │   │   └── VulkanDevice.h        # Vulkan Device / SwapChain / CommandContext
│   │   └── DXC/                      # DXC 着色器编译器
│   │       └── DXCCompiler.h         # DXC 单例封装
│   ├── Core/
│   │   ├── Window.h                  # Win32 窗口封装 + KeyState 键盘跟踪
│   │   ├── Application.h             # 应用框架（初始化、循环、RHI 切换）
│   │   ├── EditorInput.h             # 统一编辑器输入管理（相机移动/旋转）
│   │   └── EngineConfig.h            # 单例 INI 配置系统
│   ├── Debug/
│   │   └── RenderDocIntegration.h    # RenderDoc In-App API 封装
│   ├── Math/
│   │   └── Math.h                    # Vec2/3/4、Mat4、投影、LookAt
│   └── Scene/
│       ├── Component.h               # 组件基类（Transform、EComponentType）
│       ├── MeshComponent.h           # 网格 + 材质引用 + 排序顺序
│       ├── Material.h                # 材质资产 + MaterialLibrary + ShaderProperties 解析器
│       ├── TextureManager.h          # 纹理加载 (stb_image) + GPU 纹理缓存 + 默认纹理
│       ├── CameraComponent.h         # 相机（透视/正交、FOV、MainCamera）
│       ├── LightComponent.h          # 方向光与点光源组件
│       ├── PostProcessComponent.h    # 后处理材质容器
│       ├── SceneObject.h             # 实体与组件列表
│       ├── PrimitiveType.h           # EPrimitiveType 枚举（打破循环依赖）
│       ├── Scene.h                   # 场景管理与序列化
│       ├── Mesh.h                    # 网格数据结构（Vertex: Position/Normal/Tangent/Color/UV）
│       ├── Shaders.h                 # 内嵌 HLSL 与 CB 布局
│       ├── ShaderLibrary.h           # 文件着色器扫描与编译
│       ├── GLShaders.h               # OpenGL 后端 GLSL 着色器定义
│       ├── PostProcessShaders.h      # 后处理着色器定义（全屏 VS）
│       ├── PostProcessShaderLibrary.h # 后处理着色器扫描与编译
│       ├── ModelImporter.h           # OBJ/FBX 模型导入（tinyobjloader + ufbx）
│       ├── ViewMode.h               # 视图模式枚举（Lit、Unlit、BaseColor、Roughness、Metallic）
│       └── GPUScene.h               # GPU Scene 管理器（batch 分类、CB/StructuredBuffer 上传）
├── src/
│   ├── main.cpp                      # 入口点与场景编辑器（ImGui UI、渲染）
│   ├── Core/                         # Window、Application、EditorInput、EngineConfig
│   ├── RHI/                          # DX11、DX12、GL、Vulkan 后端实现 + DXC 编译器
│   ├── Scene/                        # 网格生成、场景序列化、模型导入
│   └── Debug/                        # RenderDoc 运行时加载
├── Shaders/                          # HLSL 着色器（Default、Unlit、Wireframe、DefaultLit、GBufferPass、DeferredAmbient、DeferredLighting、ShadowPass、BufferVisualization、Skybox）
├── GLShaders/                        # GLSL 着色器（DefaultLit、Unlit、Wireframe）
├── PostProcessShaders/               # 后处理 HLSL 着色器（Grayscale、Vignette、Tonemap）
├── Textures/                         # 用户纹理文件（PNG、JPG、BMP、TGA）
├── Materials/                        # 材质资产文件（.mat JSON）
├── Scenes/                           # 场景 JSON 文件（Default.json、用户场景）
├── third_party/
│   ├── imgui/                        # Dear ImGui v1.91.8
│   ├── renderdoc/                    # RenderDoc In-App API 头文件
│   ├── tinyobjloader/                # Wavefront OBJ 加载器
│   ├── ufbx/                         # Autodesk FBX 加载器
│   ├── glad/                         # glad2 OpenGL 4.5 core 加载器
│   └── vulkan-headers/               # Vulkan SDK 头文件 + vulkan-1.lib
└── tools/
    └── compile_shaders.mjs           # GLSL → SPIR-V 编译器（Vulkan 用）
```

---

## 🏗️ 架构

```
┌──────────────────────────────────────────────────────┐
│          应用层 / 场景编辑器                          │  ← 100% 后端无关
│   （零 DX11/DX12/GL/VK 头文件引用或类型转换）        │
├──────────────────────────────────────────────────────┤
│   组件系统                                           │  ← Mesh、Camera、Light、PostProcess
│   （SceneObject → vector<Component>）                │
├──────────────────────────────────────────────────────┤
│   着色器库                                           │  ← HLSL (Shaders/) + GLSL (GLShaders/)
│   （文件化，运行时编译）                              │
├──────────────────────────────────────────────────────┤
│   着色器编译器                                       │  ← FXC/DXC/GLSL/SPIR-V
├──────────────────────────────────────────────────────┤
│   调试 / RenderDoc 集成                              │  ← 一键截帧
├──────────────────────────────────────────────────────┤
│          RHI 抽象层                                  │  ← RHIDevice、RHICommandContext、
│                                                      │     RHISwapChain、RHIBuffer...
├─────────────┬─────────────┬────────────┬─────────────┤
│    DX11     │    DX12     │  OpenGL    │   Vulkan    │  ← 后端实现
│  （延迟渲染）│ （延迟渲染）│ （前向渲染）│ （前向渲染）│
└─────────────┴─────────────┴────────────┴─────────────┘
```

### 核心 RHI 虚方法

| 接口 | 方法 | 用途 |
|---|---|---|
| `RHIDevice` | `CompileShader()` | 编译 HLSL/GLSL → 着色器对象（后端内部处理细节） |
| `RHIDevice` | `CreateGraphicsPipelineState()` | DX12: 完整 PSO；DX11: 轻量包装；GL: 链接程序 |
| `RHIDevice` | `CreateTexture()` / `CreateTextureView()` | 创建纹理（支持 SRV、RTV、DSV 绑定标志） |
| `RHIDevice` | `InitImGui()` / `ImGuiNewFrame()` / `ImGuiRenderDrawData()` | 后端特定的 ImGui 生命周期 |
| `RHICommandContext` | `BeginFrame()` / `EndFrame()` | DX12: 完整帧设置/收尾；VK: acquire/present + 信号量 + Image Layout 转换；DX11/GL: 空操作 |
| `RHICommandContext` | `SetShaderResourceView()` | 绑定 SRV 到像素着色器槽位 |
| `RHICommandContext` | `ResourceBarrier()` | DX12: 资源状态转换；DX11/GL: 空操作 |
| `RHICommandContext` | `BeginEvent()` / `EndEvent()` / `SetMarker()` | GPU 调试标注，用于 RenderDoc/PIX Pass 分组 |
| `RHICommandContext` | `SetConstantBufferOffset()` | DX11.1/DX12 CB 子范围绑定，用于 GPU Scene（偏移量单位为 16 字节常量） |
| `RHICommandContext` | `DrawIndexedInstanced()` | Instanced 绘制，用于 GPU Scene batch（回退：循环 DrawIndexed） |
| `RHIDevice` | `CreateBufferSRV()` | 创建 StructuredBuffer SRV，用于 GPU Scene instanced drawing |

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
| OpenGL 上下文失败 | 确保 GPU 驱动支持 OpenGL 4.5 核心配置 |
| C4819 编译警告 | 源文件含中文注释导致，不影响编译和运行 |
| RenderDoc 未检测到 | 在 `Config/DefaultEngine.ini` 中设置 `DllPath`，或从 RenderDoc UI 启动 |
| 后处理不渲染 | 确保场景中有 PostProcess 对象且至少有一个启用的材质 |
| DXC 编译失败 | `dxcompiler.dll` + `dxil.dll` 由 CMake 自动复制；检查 `build/bin/` 中是否存在 |

---

## 📝 许可证

本项目仅供学习和个人使用。
