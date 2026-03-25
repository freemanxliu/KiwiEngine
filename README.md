# 🥝 KiwiEngine

**[中文版 README](README_CN.md)**

A lightweight 3D rendering engine and scene editor built from scratch with C++17 and DirectX. Features a **deferred rendering pipeline** with G-Buffer visualization, a **fully abstract RHI (Render Hardware Interface) layer** — application code is 100% backend-agnostic with zero `static_cast` to specific backends, zero `isDX12` branching, and zero DX11/DX12 header includes in application code.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![DirectX 11/12](https://img.shields.io/badge/DirectX-11%20%7C%2012-green)
![Vulkan](https://img.shields.io/badge/Vulkan-WIP-yellow)
![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey)
![Build](https://img.shields.io/badge/Build-CMake-orange)
![ImGui](https://img.shields.io/badge/ImGui-v1.91.8-purple)
![RenderDoc](https://img.shields.io/badge/RenderDoc-Integrated-red)

---

## ✨ Features

### 🖥️ Rendering Hardware Interface (RHI)

- **Deep RHI Abstraction** — Application and scene code contain zero references to DX11/DX12 headers. All backend-specific logic (shader compilation, PSO creation, ImGui integration, command list lifecycle, resource barriers) lives behind virtual interfaces.
- **DX11 Backend** — Full DirectX 11 implementation: device, context, swap chain, shaders, buffers, pipeline state, ImGui backend.
- **DX12 Backend** — Full DirectX 12 implementation: root signature (CBV + SRV descriptor table + static sampler), PSO, descriptor heaps (RTV/DSV/SRV/offscreen RTV), fence sync, resource barriers, upload heap buffers, ImGui backend.
- **Vulkan Backend** *(WIP)* — Vulkan implementation in progress: device, swap chain, render pass, pipeline, SPIR-V shaders.
- **Runtime RHI Switching** — Hot-switch between DX11 and DX12 at runtime via the menu bar (deferred to frame boundary, no restart needed).
- **Unified Shader Compilation** — `RHIDevice::CompileShader()` and `RHIDevice::CreateGraphicsPipelineState()` — each backend handles its own compilation and PSO creation internally.
- **Frame Lifecycle Abstraction** — `RHICommandContext::BeginFrame()` / `EndFrame()` encapsulate DX12's Reset/RootSignature/DescriptorHeaps/ResourceBarrier cycle (DX11: no-op).
- **SRV Binding Abstraction** — `RHICommandContext::SetShaderResourceView()` for backend-agnostic texture binding.
- **Resource State Management** — `EResourceState` enum and `ResourceBarrier()` for DX12 transitions (DX11: no-op).
- **GPU Debug Annotations** — `RHICommandContext::BeginEvent()` / `EndEvent()` / `SetMarker()` for GPU debugger pass labeling. Each rendering pass (G-Buffer, Deferred Lighting, Gizmo, Post-Process, ImGui) is annotated, making them clearly identifiable in RenderDoc, PIX, and other GPU profilers. DX12 uses `ID3D12GraphicsCommandList` event API; DX11 uses `ID3DUserDefinedAnnotation`.

### 🔦 Deferred Rendering Pipeline

- **G-Buffer** — 3 MRT (Multiple Render Targets):
  | RT | Format | Contents |
  |---|---|---|
  | RT0 | `R16G16B16A16_FLOAT` | World-space Position (RGB) |
  | RT1 | `R16G16B16A16_FLOAT` | World-space Normal (RGB, packed [0,1]) + Roughness (A) |
  | RT2 | `R8G8B8A8_UNORM` | Albedo Color (RGB) + Metallic (A) |
- **G-Buffer Geometry Pass** — All scene meshes are rendered with the `GBufferPass` shader into the 3 MRT targets + depth buffer. MRT PSO created via `CreateGraphicsPipelineState()` with `PipelineStateDesc`.
- **Deferred Lighting Pass** — Fullscreen triangle (SV_VertexID) reads G-Buffer SRVs (t0-t2), computes full Phong lighting (Lambert diffuse + Blinn-Phong specular) with multi-light support, outputs to scene render target.
- **Forward Gizmo Pass** — Translation gizmo is rendered on top of the deferred result using forward rendering with depth for correct occlusion.
- **Material Properties** — Each `MeshComponent` has `Roughness` [0,1] and `Metallic` [0,1] properties, stored in G-Buffer alpha channels and editable via Inspector UI.

### 👁️ View Mode System

- **View Mode Switching** — Menu bar → Rendering → View Mode. Current mode indicator shown in menu bar when not "Lit".
- **Available Modes**:
  | Mode | Type | Description |
  |---|---|---|
  | **Lit** | Default | Full deferred rendering with lighting |
  | **Unlit** | Debug | Forward rendering with no lighting (pure albedo) |
  | **BaseColor** | Buffer Visualization | G-Buffer Albedo (RT2 RGB) |
  | **Roughness** | Buffer Visualization | G-Buffer Roughness (RT1 Alpha, grayscale) |
  | **Metallic** | Buffer Visualization | G-Buffer Metallic (RT2 Alpha, grayscale) |
- **Buffer Visualization** — G-Buffer pass runs, then a dedicated `BufferVisualization` shader samples the specific G-Buffer channel and displays it as a fullscreen pass.

### 🎬 Scene & Component System

- **Entity-Component Architecture** — `SceneObject` holds a vector of `Component` via `unique_ptr`. Template methods for `AddComponent<T>`, `GetComponent<T>`, `RemoveComponent<T>`.
- **MeshComponent** — Mesh data, color, shader name, sort order, material properties (Roughness, Metallic).
- **CameraComponent** — Perspective/orthographic projection, configurable FOV, near/far planes, Main Camera toggle with mutual exclusion. The active camera drives the engine's rendering viewpoint.
- **DirectionalLightComponent** — Direction derived from rotation, configurable color and intensity.
- **PointLightComponent** — Position-based lighting with configurable radius and quadratic falloff.
- **PostProcessComponent** — Holds multiple `PostProcessMaterial` entries (shader name, intensity, enabled toggle). Post-process effects are applied in order.
- **Scene Management** — `Scene` stores `vector<unique_ptr<SceneObject>>`. Factory methods: `AddMeshObject`, `AddCameraObject`, `AddDirectionalLightObject`, `AddPointLightObject`, `AddPostProcessObject`, `AddEmptyObject`.
- **JSON Serialization** — Full scene save/load with component arrays. Backward compatible with older flat formats.

### 🎨 Shader System

- **File-Based Shader Library** — Drop `.hlsl` files into `Shaders/` folder. `ShaderLibrary` scans and compiles all shaders at startup. Per-object shader assignment via UI dropdown.
- **Built-in Shaders**:
  | Shader | Description |
  |---|---|
  | **Default** | Phong lighting — Lambert diffuse + Blinn-Phong specular, supports directional & point lights (up to 8), quadratic falloff |
  | **Unlit** | Pure color output, no lighting |
  | **Wireframe** | Normal visualization — maps world-space normals to RGB |
  | **GBufferPass** | G-Buffer geometry pass — outputs Position, Normal+Roughness, Albedo+Metallic to 3 MRT |
  | **DeferredLighting** | Fullscreen deferred lighting — reads G-Buffer, computes Phong lighting |
  | **BufferVisualization** | Debug fullscreen pass — visualizes individual G-Buffer channels |
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
- **Fallback Lighting** — When no lights are in the scene, a default directional light (0.5, 0.7, 0.3) is used.
- **AffectWorld Toggle** — Each light component can be individually enabled/disabled.

### 🛠️ Editor & Tools

- **ImGui UI** — Full-featured editor: menu bar, scene panel with object list, detail inspector, and object placer tab.
- **Translation Gizmo** — 3-axis gizmo (X=red, Y=green, Z=blue) on selected objects. Drag to translate; active axis turns yellow.
- **Ray Picking** — Click viewport to select objects. Gizmo axes have picking priority.
- **Mesh Generation** — Procedural primitives: Cube, Sphere, Cylinder, Floor.
- **Built-in Math Library** — Vec2/3/4, Mat4, perspective/orthographic projection, LookAt (left-hand coordinate system).
- **RenderDoc Integration** — One-click frame capture (🔵 button), auto-attach at startup, auto-open in RenderDoc.
- **Engine Configuration** — INI-based singleton config (`Config/DefaultEngine.ini`) with auto-discovery.

---

## 📋 Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| **OS** | Windows 10/11 | DirectX 11/12 required |
| **Visual Studio** | 2022 (v17.x) | Community / Professional / Enterprise |
| **MSVC Toolchain** | v143+ | Installed with VS2022 "Desktop development with C++" workload |
| **CMake** | 3.20+ | Bundled with VS2022, or install separately |
| **Windows SDK** | 10.0.19041+ | Includes d3d11, d3d12, d3dcompiler, dxgi headers and libs |
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

- **Switch RHI**: Menu bar → Rendering → RHI → Direct3D 11 / Direct3D 12
- **Switch View Mode**: Menu bar → Rendering → View Mode → Lit / Unlit / BaseColor / Roughness / Metallic
- **Add objects**: Placer tab → Cube / Sphere / Cylinder / Floor / Post Process
- **Add lights**: Placer tab → Directional Light / Point Light
- **Add camera**: Placer tab → Camera
- **Select objects**: Click in viewport or select from object list
- **Move objects**: Drag gizmo axes (red=X, green=Y, blue=Z)
- **Edit properties**: Detail tab → Position / Rotation / Scale / Color / Shader / FOV / Light settings
- **Main Camera**: Detail tab → toggle Main Camera checkbox (mutual exclusion)
- **Post-Processing**: Detail tab → Add materials, reorder, adjust intensity, enable/disable
- **Capture frames**: Click 🔵 button (top-right) — auto-opens in RenderDoc
- **Save/Load**: Scene Panel → Save / Load buttons (JSON format)

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

### Scene Shaders

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
- **GPU Pass Labels**: All rendering passes (G-Buffer, Deferred Lighting, Buffer Visualization, Gizmo, Post-Process, ImGui) are annotated with `BeginEvent`/`EndEvent` — visible as hierarchical groups in RenderDoc's Event Browser

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
├── include/
│   ├── RHI/
│   │   ├── RHI.h                     # Abstract interfaces (Device, Context, SwapChain, Buffer)
│   │   ├── RHITypes.h                # Types (Format, BindFlags, ResourceState, BufferDesc)
│   │   ├── DX11/                     # DX11 headers & implementation declarations
│   │   ├── DX12/                     # DX12 headers & implementation declarations
│   │   └── Vulkan/                   # Vulkan headers & implementation declarations
│   ├── Core/
│   │   ├── Window.h                  # Win32 window wrapper
│   │   ├── Application.h             # App framework (init, loop, RHI switching)
│   │   └── EngineConfig.h            # Singleton INI config system
│   ├── Debug/
│   │   └── RenderDocIntegration.h    # RenderDoc In-App API wrapper
│   ├── Math/
│   │   └── Math.h                    # Vec2/3/4, Mat4, projections, LookAt
│   └── Scene/
│       ├── Component.h               # Base Component class (Transform, EComponentType)
│       ├── MeshComponent.h           # Mesh + Color + Shader reference
│       ├── CameraComponent.h         # Camera (Perspective/Ortho, FOV, MainCamera)
│       ├── LightComponent.h          # Directional & Point light components
│       ├── PostProcessComponent.h    # Post-process materials container
│       ├── SceneObject.h             # Entity with component list
│       ├── Scene.h                   # Scene management & serialization
│       ├── Mesh.h                    # Mesh data structure
│       ├── Shaders.h                 # Embedded HLSL & CB layouts
│       ├── ShaderLibrary.h           # File-based shader scanning & compilation
│       ├── PostProcessShaders.h      # Post-process shader definitions (fullscreen VS)
│       ├── PostProcessShaderLibrary.h # Post-process shader scanning & compilation
│       └── ViewMode.h                # View mode enum (Lit, Unlit, BaseColor, Roughness, Metallic)
├── src/
│   ├── main.cpp                      # Entry point & scene editor (ImGui UI, rendering)
│   ├── Core/                         # Window, Application, EngineConfig implementations
│   ├── RHI/                          # DX11, DX12, Vulkan backend implementations
│   ├── Scene/                        # Mesh generation, Scene serialization
│   └── Debug/                        # RenderDoc runtime loading
├── Shaders/                          # Scene HLSL shaders (Default, Unlit, Wireframe, GBufferPass, DeferredLighting, BufferVisualization)
├── PostProcessShaders/               # Post-process HLSL shaders (Grayscale, Vignette)
├── third_party/
│   ├── imgui/                        # Dear ImGui v1.91.8
│   ├── renderdoc/                    # RenderDoc In-App API header
│   └── vulkan-headers/               # Vulkan SDK headers
└── tools/
    └── compile_shaders.mjs           # GLSL → SPIR-V compiler (Vulkan)
```

---

## 🏗️ Architecture

```
┌──────────────────────────────────────────┐
│          Application / Scene Editor      │  ← 100% backend-agnostic
│   (zero DX11/DX12 includes or casts)     │
├──────────────────────────────────────────┤
│   Component System                       │  ← Mesh, Camera, Light, PostProcess
│   (SceneObject → vector<Component>)      │
├──────────────────────────────────────────┤
│   Shader Libraries                       │  ← Scene shaders + Post-process shaders
│   (file-based, compiled at runtime)      │
├──────────────────────────────────────────┤
│   Debug / RenderDoc Integration          │  ← One-click frame capture
├──────────────────────────────────────────┤
│          RHI Abstract Layer              │  ← RHIDevice, RHICommandContext,
│                                          │     RHISwapChain, RHIBuffer...
├──────────────┬──────────────┬────────────┤
│    DX11      │    DX12      │  Vulkan    │  ← Backend implementations
│  (active)    │  (active)    │   (WIP)    │
└──────────────┴──────────────┴────────────┘
```

### Key RHI Virtual Methods

| Interface | Method | Purpose |
|---|---|---|
| `RHIDevice` | `CompileShader()` | Compile HLSL → shader blob (backend handles internals) |
| `RHIDevice` | `CreateGraphicsPipelineState()` | DX12: full PSO; DX11: lightweight wrapper |
| `RHIDevice` | `CreateTexture()` / `CreateTextureView()` | Create textures with bind flags (SRV, RTV, DSV) |
| `RHIDevice` | `InitImGui()` / `ImGuiNewFrame()` / `ImGuiRenderDrawData()` | Backend-specific ImGui lifecycle |
| `RHICommandContext` | `BeginFrame()` / `EndFrame()` | DX12: full frame setup/teardown; DX11: no-op |
| `RHICommandContext` | `SetShaderResourceView()` | Bind SRV to pixel shader slot |
| `RHICommandContext` | `ResourceBarrier()` | DX12: state transitions; DX11: no-op |
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
| C4819 compiler warning | Source files with Chinese comments — harmless |
| RenderDoc not detected | Set `DllPath` in `Config/DefaultEngine.ini` or launch from RenderDoc UI |
| Post-process not rendering | Ensure a PostProcess object is in the scene with at least one enabled material |

---

## 📝 License

This project is for educational and personal use.
