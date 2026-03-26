# 🥝 KiwiEngine

**[中文版 README](README_CN.md)**

A lightweight 3D rendering engine and scene editor built from scratch with C++17. Features a **deferred rendering pipeline** with G-Buffer visualization, a **fully abstract RHI (Render Hardware Interface) layer** supporting **DX11, DX12, OpenGL, and Vulkan** with runtime switching — application code is 100% backend-agnostic with zero `static_cast` to specific backends, zero `isDX12` branching, and zero backend header includes in application code.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![DirectX 11/12](https://img.shields.io/badge/DirectX-11%20%7C%2012-green)
![OpenGL](https://img.shields.io/badge/OpenGL-4.5-blue)
![Vulkan](https://img.shields.io/badge/Vulkan-1.2-red)
![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey)
![Build](https://img.shields.io/badge/Build-CMake-orange)
![ImGui](https://img.shields.io/badge/ImGui-v1.91.8-purple)
![RenderDoc](https://img.shields.io/badge/RenderDoc-Integrated-red)

---

## ✨ Features

### 🖥️ Rendering Hardware Interface (RHI)

- **Deep RHI Abstraction** — Application and scene code contain zero references to DX11/DX12/OpenGL/Vulkan headers. All backend-specific logic (shader compilation, PSO creation, ImGui integration, command list lifecycle, resource barriers) lives behind virtual interfaces.
- **DX11 Backend** — Full DirectX 11 implementation: device, context, swap chain, shaders, buffers, pipeline state, ImGui backend.
- **DX12 Backend** — Full DirectX 12 implementation: root signature (CBV + SRV descriptor table + static sampler), PSO, descriptor heaps (RTV/DSV/SRV/offscreen RTV), fence sync, resource barriers, upload heap buffers, ImGui backend.
- **OpenGL Backend** — Full OpenGL 4.5 core profile implementation: WGL context creation, glad2 loader, FBO-based render targets, GLSL shader compilation and program linking, forward rendering path, ImGui OpenGL3 backend.
- **Vulkan Backend** — Vulkan 1.2 implementation: VkInstance with validation layers, physical/logical device, swap chain (Win32 surface), render pass, framebuffers, command buffers with fence sync, descriptor pool/sets, VkPipeline creation, SPIR-V shader modules, ImGui Vulkan backend. Forward rendering path.
- **Runtime RHI Switching** — Hot-switch between DX11, DX12, OpenGL, and Vulkan at runtime via the menu bar (deferred to frame boundary, no restart needed).
- **Dual Shader Compiler** — DX11 uses FXC (SM 5.0 → DXBC); DX12 uses DXC (SM 6.0 → DXIL) with FXC fallback; OpenGL uses driver-native GLSL compilation.
- **Unified Shader Compilation** — `RHIDevice::CompileShader()` and `RHIDevice::CreateGraphicsPipelineState()` — each backend handles its own compilation and PSO creation internally.
- **Frame Lifecycle Abstraction** — `RHICommandContext::BeginFrame()` / `EndFrame()` encapsulate DX12's Reset/RootSignature/DescriptorHeaps/ResourceBarrier cycle (DX11/GL: no-op).
- **SRV Binding Abstraction** — `RHICommandContext::SetShaderResourceView()` for backend-agnostic texture binding.
- **Resource State Management** — `EResourceState` enum and `ResourceBarrier()` for DX12 transitions (DX11/GL: no-op).
- **GPU Debug Annotations** — `RHICommandContext::BeginEvent()` / `EndEvent()` / `SetMarker()` for GPU debugger pass labeling. Each rendering pass (G-Buffer, Deferred Lighting, Gizmo, Post-Process, ImGui) is annotated, making them clearly identifiable in RenderDoc, PIX, and other GPU profilers. DX12 uses `ID3D12GraphicsCommandList` event API; DX11 uses `ID3DUserDefinedAnnotation`.
- **GPU Resource Debug Names** — All GPU resources (textures, buffers) carry descriptive debug names visible in RenderDoc and PIX. `TextureDesc::DebugName` and `BufferDesc::DebugName` are automatically applied via `SetPrivateData` (DX11) and `SetName` (DX12). Named resources include: G-Buffer RTs, shadow maps, depth buffer, constant buffers, mesh vertex/index buffers (with object names), gizmo buffers, and offscreen render targets.

### 🔧 Shader Compilation Pipeline

| Backend | Compiler | Shader Model | Output Format | Notes |
|---------|----------|-------------|--------------|-------|
| **DX11** | FXC (`d3dcompiler.dll`) | SM 5.0 | DXBC | Legacy compiler, stable |
| **DX12** | DXC (`dxcompiler.dll`) | SM 6.0 | DXIL | Modern compiler with FXC fallback; auto-upgrades `vs_5_0` → `vs_6_0` via `UpgradeProfileForDXC()` |
| **OpenGL** | Driver GLSL | GLSL 450 | Native | `//!VERTEX` / `//!FRAGMENT` split markers in `.glsl` files |
| **Vulkan** | glslang / shaderc | GLSL 450 → SPIR-V | SPIR-V | Pre-compiled or runtime GLSL→SPIR-V; `VkShaderModule` per stage |

- **DXC Integration** — `DXCCompiler` singleton wraps `IDxcCompiler3`, runtime-loaded via `LoadLibrary("dxcompiler.dll")`. DXC runtime DLLs (`dxcompiler.dll` + `dxil.dll`) are automatically copied to the output directory by CMake.
- **ShaderLibrary** auto-selects HLSL (`Shaders/`) or GLSL (`GLShaders/`) source files based on the active RHI backend.

### 🔦 Deferred Rendering Pipeline (UE5-Inspired)

- **G-Buffer** — 3 MRT (all `R8G8B8A8_UNORM`, UE5-inspired layout):
  | RT | Contents |
  |---|---|
  | **GBufferA** (t0) | Normal (Octahedron RG) + Metallic (B) + ShadingModelID (A) |
  | **GBufferB** (t1) | BaseColor (RGB) + Roughness (A) |
  | **GBufferC** (t2) | Emissive (RGB) + Specular (A) |
  | **Depth** (t7) | Hardware depth (`R32_TYPELESS`) → world position via InvViewProj |
- **Octahedron Normal Encoding** — Unit normals stored in 2 channels using octahedron mapping, providing better precision than linear [0,1] packing in R8G8 format.
- **Depth-Based Position Reconstruction** — World-space position is reconstructed from hardware depth buffer + inverse ViewProjection matrix, eliminating the need for a dedicated position render target (~40% bandwidth savings vs R16F position RT).
- **G-Buffer Geometry Pass** — All scene meshes are rendered with the `GBufferPass` shader into the 3 MRT targets + depth buffer. MRT PSO created via `CreateGraphicsPipelineState()` with `PipelineStateDesc`.
- **Deferred Lighting Pass** — Fullscreen triangle (SV_VertexID) reconstructs world position from depth, decodes octahedron normals, reads material properties from G-Buffer, and computes **UE5-style PBR lighting** with multi-light support. BRDF matches UE5's `DefaultLitBxDF`: **D_GGX** (Trowbridge-Reitz NDF), **Vis_SmithJointApprox** (joint Smith visibility with baked-in denominator), **F_Schlick** (with 2% reflectance shadow threshold), **Diffuse_Burley** (Disney diffuse, roughness-dependent), and **EnvBRDFApprox** (Lazarov 2013 analytical approximation, no LUT needed) for indirect specular. Applies cascaded shadow maps and Reinhard tone mapping.
- **Shadow Pass (CSM)** — Cascaded Shadow Mapping with up to 4 cascades rendered into a **single shadow atlas** (2x2 layout). Each cascade occupies one quadrant of the atlas texture (`R32_TYPELESS`, `2*cascadeSize × 2*cascadeSize`). PSSM (Practical Split Scheme) blends logarithmic and uniform cascade splits. Shader selects cascade by view-space distance and computes UV offset into the atlas. 5-tap PCF filtering with comparison sampler for soft shadow edges.
- **Forward Gizmo Pass** — Translation gizmo is rendered on top of the deferred result using forward rendering with depth for correct occlusion.
- **Material Properties** — Each `MeshComponent` has `Roughness` [0,1] and `Metallic` [0,1] properties, stored in G-Buffer and editable via Inspector UI.

> **Note**: The deferred rendering pipeline (G-Buffer, CSM shadows, deferred lighting) is active for DX11 and DX12 backends. The OpenGL and Vulkan backends use a forward rendering path.

### 👁️ View Mode System

- **View Mode Switching** — Menu bar → Rendering → View Mode. Current mode indicator shown in menu bar when not "Lit".
- **Available Modes**:
  | Mode | Type | Description |
  |---|---|---|
  | **Lit** | Default | Full deferred rendering with UE5-style PBR lighting |
  | **Unlit** | Debug | Forward rendering with no lighting (pure albedo) |
  | **BaseColor** | Buffer Visualization | G-Buffer BaseColor (GBufferB RGB) |
  | **Roughness** | Buffer Visualization | G-Buffer Roughness (GBufferB Alpha, grayscale) |
  | **Metallic** | Buffer Visualization | G-Buffer Metallic (GBufferA Blue, grayscale) |
- **Buffer Visualization** — G-Buffer pass runs, then a dedicated `BufferVisualization` shader samples the specific G-Buffer channel and displays it as a fullscreen pass.

### 🎬 Scene & Component System

- **Entity-Component Architecture** — `SceneObject` holds a vector of `Component` via `unique_ptr`. Template methods for `AddComponent<T>`, `GetComponent<T>`, `RemoveComponent<T>`.
- **MeshComponent** — Mesh data, color, shader name, sort order, primitive type, material properties (Roughness, Metallic).
- **CameraComponent** — Perspective/orthographic projection, configurable FOV, near/far planes, Main Camera toggle with mutual exclusion. The active camera drives the engine's rendering viewpoint.
- **DirectionalLightComponent** — Direction derived from rotation, configurable color and intensity. **Cascaded Shadow Mapping (CSM)** parameters: CastShadow, NumCascades (1-4), ShadowMapResolution, ShadowDistance, CascadeSplitLambda, ShadowBias, NormalBias, ShadowStrength.
- **PointLightComponent** — Position-based lighting with configurable radius and quadratic falloff.
- **PostProcessComponent** — Holds multiple `PostProcessMaterial` entries (shader name, intensity, enabled toggle). Post-process effects are applied in order.
- **Scene Management** — `Scene` stores `vector<unique_ptr<SceneObject>>`. Factory methods: `AddMeshObject`, `AddCameraObject`, `AddDirectionalLightObject`, `AddPointLightObject`, `AddPostProcessObject`, `AddEmptyObject`.
- **JSON Serialization** — Full scene save/load with component arrays. File menu → Save Scene (with naming dialog), Open Scene (auto-scans `Scenes/` directory), Create Scene. Default scene (`Scenes/Default.json`) auto-created on first run.
- **Window Title** — Displays current scene name: "Kiwi Engine - \<scene name\>".

### 🎨 Shader System

- **File-Based Shader Library** — Drop `.hlsl` files into `Shaders/` folder (or `.glsl` into `GLShaders/`). `ShaderLibrary` scans and compiles all shaders at startup, auto-selecting the correct folder based on active RHI backend. Per-object shader assignment via UI dropdown.
- **Built-in Shaders**:
  | Shader | Description |
  |---|---|
  | **Default** | Phong lighting — Lambert diffuse + Blinn-Phong specular, supports directional & point lights (up to 8), quadratic falloff |
  | **DefaultLit** | Standard PBR-style material — responds to Roughness and Metallic properties, default shader for new objects |
  | **Unlit** | Pure color output, no lighting |
  | **Wireframe** | Normal visualization — maps world-space normals to RGB |
  | **GBufferPass** | G-Buffer geometry pass — octahedron normal encoding, outputs Normal+Metallic, BaseColor+Roughness, Emissive+Specular to 3 MRT |
  | **DeferredLighting** | Fullscreen PBR deferred lighting (UE5 DefaultLitBxDF) — D_GGX + Vis_SmithJointApprox + F_Schlick + Diffuse_Burley + EnvBRDFApprox, CSM shadow atlas, Reinhard tone mapping |
  | **ShadowPass** | Depth-only vertex shader for shadow map generation (no pixel shader) |
  | **BufferVisualization** | Debug fullscreen pass — visualizes individual G-Buffer channels |
- **GLSL Shaders** — OpenGL versions of DefaultLit, Unlit, and Wireframe in `GLShaders/` directory, using `//!VERTEX` / `//!FRAGMENT` markers for stage separation.
- **Unified Constant Buffer** — World/View/Projection matrices, object color, selection state, light count, camera position, material properties (Roughness, Metallic), and GPU light data (up to 8 lights).
- **Custom Shaders** — Create a `.hlsl` with `VSMain`/`PSMain` entry points using the shared CB layout, drop into `Shaders/`, and it's available at runtime.

### 🌈 Post-Processing System

- **PostProcessShaderLibrary** — Scans `PostProcessShaders/` folder, compiles pixel shaders + shared fullscreen vertex shader, creates PSOs without input layout.
- **Fullscreen Triangle** — Efficient fullscreen pass using `SV_VertexID` (0,1,2) to generate a screen-covering triangle — no vertex buffer needed.
- **Ping-Pong Rendering** — Scene → RT[0] → post-process chain → backbuffer. Intermediate passes alternate between RT[0] and RT[1].
- **Built-in Effects**:
  | Effect | Description |
  |---|---|
  | **Grayscale** | Luminance-based desaturation with intensity control |
  | **Vignette** | Darkened edges with configurable intensity |
- **Per-Object Post-Process** — Each `PostProcessComponent` manages a material list with ordering, enable/disable, and intensity per effect.

### 💡 Lighting

- **Multi-Light Support** — Up to 8 simultaneous lights (directional + point) in a single draw call.
- **GPU Light Data** — Packed struct: color+intensity, type (0=Directional, 1=Point), direction/position, radius.
- **Cascaded Shadow Mapping (CSM)** — Directional lights support real-time cascaded shadow maps:
  - Up to **4 cascades** rendered into a **single shadow atlas** (2x2 layout, `2*cascadeSize × 2*cascadeSize`)
  - **Shadow Atlas** — All cascades share one `R32_TYPELESS` depth texture; each cascade rendered via viewport/scissor into its quadrant
  - **PSSM split scheme** — Blends logarithmic and uniform cascade splits via lambda parameter
  - **Atlas UV Mapping** — Shader selects cascade by view-space Z distance, then applies UV scale (0.5×) and offset to sample the correct atlas quadrant
  - **5-tap PCF** filtering with comparison sampler for soft shadow edges
  - Per-light configurable: shadow distance, depth bias, normal bias, shadow strength
  - Shadow pass renders depth-only (no pixel shader) for maximum performance
  - Full UI controls in Detail inspector for all shadow parameters
- **Fallback Lighting** — When no lights are in the scene, a default directional light (0.5, 0.7, 0.3) is used.
- **AffectWorld Toggle** — Each light component can be individually enabled/disabled.

### 🛠️ Editor & Tools

- **ImGui UI** — Full-featured editor: menu bar (File, Rendering), scene panel with object list, detail inspector, and object placer tab.
- **Camera Navigation** — Hold right mouse button + WASD/Arrow keys to fly the main camera (horizontal plane movement). Hold right mouse button + drag to rotate camera (Yaw/Pitch, sensitivity 0.15°/px, ±89° pitch clamp).
- **Camera Settings** — 📷 button in top-right corner opens a popup with configurable Move Speed (0.5–50 units/s) and FOV (10–120°) sliders.
- **Translation Gizmo** — 3-axis gizmo (X=red, Y=green, Z=blue) on selected objects. Drag to translate; active axis turns yellow. **Constant screen-space size** — gizmo auto-scales based on camera distance so it stays the same pixel size regardless of zoom level.
- **Screen-Space Gizmo Picking** — Gizmo axis selection uses 2D screen-space distance (pixel threshold) instead of 3D ray casting, providing reliable and intuitive picking behavior regardless of camera angle or distance.
- **Model Import** — Load external `.obj` (Wavefront OBJ via tinyobjloader) and `.fbx` (Autodesk FBX via ufbx) model files. Each sub-mesh becomes a separate mesh object with per-material diffuse color. Automatic vertex deduplication, polygon triangulation, flat/smooth normals, and UV coordinate flipping.
- **Ray Picking** — Click viewport to select objects. Gizmo axes have picking priority.
- **Mesh Generation** — Procedural primitives: Cube, Sphere, Cylinder, Floor.
- **Built-in Math Library** — Vec2/3/4, Mat4 (with `Inverse()` via Cramer's rule for InvViewProj), perspective/orthographic projection, LookAt (left-hand coordinate system).
- **RenderDoc Integration** — One-click frame capture (🔵 button), auto-attach at startup, auto-open in RenderDoc.
- **Engine Configuration** — INI-based singleton config (`Config/DefaultEngine.ini`) with auto-discovery.

---

## 📋 Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| **OS** | Windows 10/11 | DirectX 11/12 and OpenGL 4.5 required |
| **Visual Studio** | 2022 (v17.x) | Community / Professional / Enterprise |
| **MSVC Toolchain** | v143+ | Installed with VS2022 "Desktop development with C++" workload |
| **CMake** | 3.20+ | Bundled with VS2022, or install separately |
| **Windows SDK** | 10.0.19041+ | Includes d3d11, d3d12, d3dcompiler, dxgi headers and libs |
| **GPU** | DX11 FL 11.0+ / OpenGL 4.5 | Most modern GPUs support both |
| **RenderDoc** | 1.6+ *(optional)* | For frame capture and GPU debugging |

---

## 🔨 Build Instructions

### Option 1: Command Line (Recommended)

```powershell
# Clone the repository
git clone https://github.com/freemanxliu/KiwiEngine.git
cd KiwiEngine

# Create build directory and generate VS solution
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64

# Build Release
cmake --build . --config Release

# Build Debug (with debug symbols)
cmake --build . --config Debug
```

The executable will be at: `build/bin/KiwiEngine.exe`

### Option 2: Visual Studio IDE

1. Open Visual Studio 2022
2. **File → Open → CMake...** → select `KiwiEngine/CMakeLists.txt`
3. Wait for CMake to configure
4. Select **Release** or **Debug** from the configuration dropdown
5. **Build → Build All** (`Ctrl+Shift+B`)
6. **Debug → Start Without Debugging** (`Ctrl+F5`)

### Option 3: Open .sln

```powershell
cd KiwiEngine/build
cmake .. -G "Visual Studio 17 2022" -A x64
# Open build/KiwiEngine.sln in Visual Studio
```

---

## 🚀 Running

```powershell
./build/bin/KiwiEngine.exe
```

A 1280×720 window opens showing a 3D scene editor. You can:

- **Switch RHI**: Menu bar → Rendering → RHI → Direct3D 11 / Direct3D 12 / OpenGL / Vulkan
- **Switch View Mode**: Menu bar → Rendering → View Mode → Lit / Unlit / BaseColor / Roughness / Metallic
- **Navigate camera**: Hold right mouse button + WASD to fly; right mouse button + drag to look around
- **Camera settings**: Click 📷 button (top-right) to adjust move speed and FOV
- **Add objects**: Placer tab → Cube / Sphere / Cylinder / Floor / Post Process
- **Add lights**: Placer tab → Directional Light / Point Light
- **Add camera**: Placer tab → Camera
- **Select objects**: Click in viewport or select from object list
- **Move objects**: Drag gizmo axes (red=X, green=Y, blue=Z)
- **Edit properties**: Detail tab → Position / Rotation / Scale / Color / Shader / FOV / Light settings
- **Main Camera**: Detail tab → toggle Main Camera checkbox (mutual exclusion)
- **Post-Processing**: Detail tab → Add materials, reorder, adjust intensity, enable/disable
- **Capture frames**: Click 🔵 button (top-right) — auto-opens in RenderDoc
- **Scene management**: File menu → Create Scene / Open Scene (scans `Scenes/` folder) / Save Scene (naming dialog, JSON format)

---

## ⚙️ Engine Configuration

KiwiEngine uses an INI-based configuration system via a singleton `EngineConfig` class.

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

Config file auto-discovery: `exe/Config/` → `../../Config/` → `exe/`

### Usage in Code

```cpp
#include "Core/EngineConfig.h"

auto& config = Kiwi::EngineConfig::Get();
config.LoadDefaultConfig();

std::string path = config.GetString("RenderDoc", "DllPath", "");
int width        = config.GetInt("Window", "Width", 1280);
bool vsync       = config.GetBool("Rendering", "VSync", true);
```

---

## 🎨 Custom Shaders

### Scene Shaders (HLSL — DX11/DX12)

Create a `.hlsl` file in `Shaders/` with entry points `VSMain` and `PSMain`:

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

### Scene Shaders (GLSL — OpenGL)

Create a `.glsl` file in `GLShaders/` with `//!VERTEX` and `//!FRAGMENT` markers:

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

### Post-Process Shaders

Create a `.hlsl` file in `PostProcessShaders/` with entry point `PSMain`:

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
    // Your effect here
    return color;
}
```

---

## 🔍 RenderDoc Integration

- **Auto-Attach**: Loaded at startup before any graphics device creation
- **One-Click Capture**: 🔵 button in top-right corner
- **Visual Feedback**: Orange during capture, hover shows count
- **Zero Configuration**: Just run — RenderDoc is detected automatically
- **GPU Pass Labels**: All rendering passes (Shadow Pass, G-Buffer, Deferred Lighting, Buffer Visualization, Gizmo, Post-Process, ImGui) are annotated with `BeginEvent`/`EndEvent` — visible as hierarchical groups in RenderDoc's Event Browser
- **GPU Resource Names**: All textures and buffers have descriptive names (e.g., `GBufferA_NormalMetallic`, `ShadowAtlas_CSM`, `MeshVB_Cube`) — visible in RenderDoc's Resource Inspector and Texture Viewer

**Custom DLL Path** (`Config/DefaultEngine.ini`):
```ini
[RenderDoc]
DllPath=D:\Tools\RenderDoc\renderdoc.dll
```

DLL search priority: Already loaded → Config `[RenderDoc].DllPath` (required if not already loaded)

---

## 📁 Project Structure

```
KiwiEngine/
├── CMakeLists.txt                    # Build configuration (C++17, CMake 3.20+)
├── Config/
│   └── DefaultEngine.ini             # Engine settings (RenderDoc, rendering, window)
├── docs/
│   └── ShaderCompilationPipeline.drawio  # Shader compilation flow diagram
├── include/
│   ├── RHI/
│   │   ├── RHI.h                     # Abstract interfaces (Device, Context, SwapChain, Buffer)
│   │   ├── RHITypes.h                # Types (Format, BindFlags, ResourceState, BufferDesc)
│   │   ├── README.md                 # RHI architecture documentation
│   │   ├── DX11/                     # DX11 backend headers
│   │   │   ├── DX11Headers.h         # DX11 system headers
│   │   │   ├── DX11Utils.h           # DX11 utility functions
│   │   │   ├── DX11Resources.h       # DX11 resource wrapper classes
│   │   │   └── DX11Device.h          # DX11 Device / SwapChain / CommandContext
│   │   ├── DX12/                     # DX12 backend headers
│   │   │   ├── DX12Headers.h         # DX12 system headers
│   │   │   ├── DX12Resources.h       # DX12 resource wrapper classes
│   │   │   └── DX12Device.h          # DX12 Device / SwapChain / CommandContext
│   │   ├── GL/                       # OpenGL backend headers
│   │   │   ├── GLHeaders.h           # OpenGL / WGL system headers
│   │   │   ├── GLResources.h         # GL resource wrapper classes
│   │   │   └── GLDevice.h            # GL Device / SwapChain / CommandContext
│   │   ├── Vulkan/                   # Vulkan backend headers
│   │   │   ├── VulkanHeaders.h       # Vulkan / Win32 system headers
│   │   │   └── VulkanDevice.h        # Vulkan Device / SwapChain / CommandContext
│   │   └── DXC/                      # DXC shader compiler
│   │       └── DXCCompiler.h         # DXC singleton wrapper
│   ├── Core/
│   │   ├── Window.h                  # Win32 window wrapper + KeyState tracking
│   │   ├── Application.h             # App framework (init, loop, RHI switching)
│   │   ├── EditorInput.h             # Unified editor input (camera move/look)
│   │   └── EngineConfig.h            # Singleton INI config system
│   ├── Debug/
│   │   └── RenderDocIntegration.h    # RenderDoc In-App API wrapper
│   ├── Math/
│   │   └── Math.h                    # Vec2/3/4, Mat4, projections, LookAt
│   └── Scene/
│       ├── Component.h               # Base Component class (Transform, EComponentType)
│       ├── MeshComponent.h           # Mesh + Color + Shader + Material properties
│       ├── CameraComponent.h         # Camera (Perspective/Ortho, FOV, MainCamera)
│       ├── LightComponent.h          # Directional & Point light components
│       ├── PostProcessComponent.h    # Post-process materials container
│       ├── SceneObject.h             # Entity with component list
│       ├── PrimitiveType.h           # EPrimitiveType enum (breaks circular dependency)
│       ├── Scene.h                   # Scene management & serialization
│       ├── Mesh.h                    # Mesh data structure
│       ├── Shaders.h                 # Embedded HLSL & CB layouts
│       ├── ShaderLibrary.h           # File-based shader scanning & compilation
│       ├── PostProcessShaders.h      # Post-process shader definitions (fullscreen VS)
│       ├── PostProcessShaderLibrary.h # Post-process shader scanning & compilation
│       ├── ModelImporter.h           # OBJ/FBX model import (tinyobjloader + ufbx)
│       └── ViewMode.h               # View mode enum (Lit, Unlit, BaseColor, Roughness, Metallic)
├── src/
│   ├── main.cpp                      # Entry point & scene editor (ImGui UI, rendering)
│   ├── Core/                         # Window, Application, EditorInput, EngineConfig
│   ├── RHI/                          # DX11, DX12, GL, Vulkan backend implementations + DXC compiler
│   ├── Scene/                        # Mesh generation, Scene serialization, Model import
│   └── Debug/                        # RenderDoc runtime loading
├── Shaders/                          # HLSL shaders (Default, Unlit, Wireframe, DefaultLit, GBufferPass, DeferredLighting, ShadowPass, BufferVisualization)
├── GLShaders/                        # GLSL shaders (DefaultLit, Unlit, Wireframe)
├── PostProcessShaders/               # Post-process HLSL shaders (Grayscale, Vignette)
├── Scenes/                           # Scene JSON files (Default.json, user scenes)
├── third_party/
│   ├── imgui/                        # Dear ImGui v1.91.8
│   ├── renderdoc/                    # RenderDoc In-App API header
│   ├── tinyobjloader/                # Wavefront OBJ loader
│   ├── ufbx/                         # Autodesk FBX loader
│   ├── glad/                         # glad2 OpenGL 4.5 core loader
│   └── vulkan-headers/               # Vulkan SDK headers + vulkan-1.lib
└── tools/
    └── compile_shaders.mjs           # GLSL → SPIR-V compiler (Vulkan)
```

---

## 🏗️ Architecture

```
┌──────────────────────────────────────────────────────┐
│              Application / Scene Editor              │  ← 100% backend-agnostic
│   (zero DX11/DX12/GL/VK includes or casts)           │
├──────────────────────────────────────────────────────┤
│   Component System                                   │  ← Mesh, Camera, Light, PostProcess
│   (SceneObject → vector<Component>)                  │
├──────────────────────────────────────────────────────┤
│   Shader Libraries                                   │  ← HLSL (Shaders/) + GLSL (GLShaders/)
│   (file-based, compiled at runtime)                  │
├──────────────────────────────────────────────────────┤
│   Shader Compilers                                   │  ← FXC/DXC/GLSL/SPIR-V
├──────────────────────────────────────────────────────┤
│   Debug / RenderDoc Integration                      │  ← One-click frame capture
├──────────────────────────────────────────────────────┤
│              RHI Abstract Layer                      │  ← RHIDevice, RHICommandContext,
│                                                      │     RHISwapChain, RHIBuffer...
├─────────────┬─────────────┬────────────┬─────────────┤
│    DX11     │    DX12     │  OpenGL    │   Vulkan    │  ← Backend implementations
│  (deferred) │  (deferred) │ (forward)  │  (forward)  │
└─────────────┴─────────────┴────────────┴─────────────┘
```

### Key RHI Virtual Methods

| Interface | Method | Purpose |
|---|---|---|
| `RHIDevice` | `CompileShader()` | Compile HLSL/GLSL → shader object (backend handles internals) |
| `RHIDevice` | `CreateGraphicsPipelineState()` | DX12: full PSO; DX11: lightweight wrapper; GL: linked program; VK: VkPipeline |
| `RHIDevice` | `CreateTexture()` / `CreateTextureView()` | Create textures with bind flags (SRV, RTV, DSV) |
| `RHIDevice` | `InitImGui()` / `ImGuiNewFrame()` / `ImGuiRenderDrawData()` | Backend-specific ImGui lifecycle |
| `RHICommandContext` | `BeginFrame()` / `EndFrame()` | DX12: full frame setup/teardown; VK: acquire/present with semaphores; DX11/GL: no-op |
| `RHICommandContext` | `SetShaderResourceView()` | Bind SRV to pixel shader slot |
| `RHICommandContext` | `ResourceBarrier()` | DX12: state transitions; VK: pipeline barriers; DX11/GL: no-op |
| `RHICommandContext` | `BeginEvent()` / `EndEvent()` / `SetMarker()` | GPU debug annotations for RenderDoc/PIX pass grouping |

### Adding a New Backend

1. Create `include/RHI/<API>/` and `src/RHI/<API>Device.cpp`
2. Implement all abstract interfaces from `RHI.h`
3. Add a new case in the `CreateRHI()` factory function
4. **No changes needed** in `main.cpp`, `Application.cpp`, or any scene code

---

## 🔧 Troubleshooting

| Problem | Solution |
|---|---|
| `cmake` not found | Add CMake to PATH, or use VS2022's bundled CMake |
| Missing Windows SDK | Install via VS Installer → Individual components → Windows 10/11 SDK |
| Linker errors (d3d11.lib) | Ensure Windows SDK is installed and detected |
| Black window | Check console for errors; ensure GPU supports DX11 Feature Level 11.0 |
| OpenGL context fails | Ensure GPU drivers support OpenGL 4.5 core profile |
| C4819 compiler warning | Source files with Chinese comments — harmless |
| RenderDoc not detected | Set `DllPath` in `Config/DefaultEngine.ini` or launch from RenderDoc UI |
| Post-process not rendering | Ensure a PostProcess object is in the scene with at least one enabled material |
| DXC compilation fails | `dxcompiler.dll` + `dxil.dll` are auto-copied by CMake; verify they exist in `build/bin/` |

---

## 📝 License

This project is for educational and personal use.
