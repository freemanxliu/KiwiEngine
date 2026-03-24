# 🥝 KiwiEngine

A lightweight 3D rendering engine built from scratch with C++17 and DirectX, featuring a custom RHI (Render Hardware Interface) abstraction layer with runtime DX11/DX12 switching, a built-in scene editor, and integrated RenderDoc frame capture.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![DirectX 11/12](https://img.shields.io/badge/DirectX-11%20%7C%2012-green)
![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey)
![Build](https://img.shields.io/badge/Build-CMake-orange)
![RenderDoc](https://img.shields.io/badge/RenderDoc-Integrated-red)

## ✨ Features

- **RHI Abstraction Layer** — Clean separation between rendering logic and graphics API, with runtime backend switching
- **DX11 Backend** — Full DirectX 11 implementation (device, context, swap chain, shaders, buffers, pipeline state)
- **DX12 Backend** — Full DirectX 12 implementation (root signature, PSO, descriptor heaps, fence sync, resource barriers)
- **Runtime RHI Switching** — Hot-switch between DX11 and DX12 at runtime via the menu bar (no restart needed)
- **Built-in Math Library** — Vec2/3/4, Mat4, perspective/orthographic projection, LookAt camera (left-hand coordinate system)
- **Scene Editor** — Interactive scene management with object selection, transform editing, and JSON serialization
- **Mesh Generation** — Procedural cube, sphere, cylinder, and floor primitives
- **Runtime Shader Compilation** — HLSL vertex/pixel shaders compiled at startup via D3DCompile
- **Phong Lighting** — Lambert diffuse + Blinn-Phong specular shading
- **RenderDoc Integration** — Built-in frame capture with one-click button (auto-attaches on startup)
- **ImGui UI** — Full-featured editor interface with menu bar, scene panel, detail inspector, and object placer

## 🔍 RenderDoc Integration

KiwiEngine includes built-in [RenderDoc](https://renderdoc.org) support for GPU frame capture and debugging.

### How It Works

- **Auto-Attach**: RenderDoc is automatically loaded at startup (before any graphics device is created)
- **One-Click Capture**: A fixed overlay button in the top-right corner of the window lets you capture a frame instantly
- **Zero Configuration**: No need to launch from RenderDoc UI — just run the engine normally
- **Replay UI**: After capturing, click "Open in RenderDoc" to launch the replay viewer directly

### Setup

1. **Install RenderDoc** from [https://renderdoc.org](https://renderdoc.org) (default install path: `C:\Program Files\RenderDoc\`)
2. Run `KiwiEngine.exe` normally — RenderDoc will be detected and attached automatically
3. Click the red **"Capture Frame"** button in the top-right overlay to capture
4. Captures are saved to the `captures/` directory

> **Note**: If RenderDoc is not installed, the engine runs normally without frame capture functionality. The overlay will show "RenderDoc N/A" status.

### Alternative: Launch from RenderDoc

You can also launch KiwiEngine from within RenderDoc:
1. Open RenderDoc → **Launch Application** → Browse to `KiwiEngine.exe`
2. Click **Launch** — RenderDoc will inject automatically
3. Use either RenderDoc's F12 key or the in-app button to capture

## 📋 Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| **OS** | Windows 10/11 | DirectX 11/12 required |
| **Visual Studio** | 2022 (v17.x) | Community / Professional / Enterprise |
| **MSVC Toolchain** | v143+ | Installed with VS2022 "Desktop development with C++" workload |
| **CMake** | 3.20+ | Bundled with VS2022, or install separately |
| **Windows SDK** | 10.0.19041+ | Includes d3d11, d3d12, d3dcompiler, dxgi headers and libs |
| **RenderDoc** | 1.6+ (optional) | For frame capture and GPU debugging |

### Installing Visual Studio Workloads

If you don't have the required components, open **Visual Studio Installer** and ensure these workloads/components are installed:

1. ✅ **Desktop development with C++**
2. ✅ **Windows 10/11 SDK** (any version ≥ 10.0.19041)
3. ✅ **C++ CMake tools for Windows** (optional — only if you want to use CMake from VS)

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

# Build Debug (with debug symbols, slower)
cmake --build . --config Debug
```

The executable will be at: `build/bin/KiwiEngine.exe`

### Option 2: Visual Studio IDE

1. Open Visual Studio 2022
2. **File → Open → CMake...** → select `KiwiEngine/CMakeLists.txt`
3. Wait for CMake to configure (watch the Output window)
4. Select **Release** or **Debug** from the configuration dropdown
5. **Build → Build All** (or press `Ctrl+Shift+B`)
6. **Debug → Start Without Debugging** (or press `Ctrl+F5`)

### Option 3: Generate and Open .sln

```powershell
cd KiwiEngine/build
cmake .. -G "Visual Studio 17 2022" -A x64
```

Then open `build/KiwiEngine.sln` in Visual Studio and build from there.

## 🚀 Running

Simply run the built executable:

```powershell
./build/bin/KiwiEngine.exe
```

A 1280×720 window will appear showing a **3D scene editor** with Phong lighting. You can:

- **Switch RHI**: Menu bar → Rendering → RHI → Direct3D 11 / Direct3D 12
- **Add objects**: Scene Panel → Placer tab → Click Cube/Sphere/Cylinder/Floor
- **Select objects**: Click in the viewport or select from the object list
- **Edit transforms**: Scene Panel → Detail tab → Drag Position/Rotation/Scale
- **Capture frames**: Click the red "Capture Frame" button (top-right) for RenderDoc capture
- **Save/Load scenes**: Scene Panel → Save/Load buttons (JSON format)

No external assets, textures, or config files are needed — everything (including shaders) is embedded in the source code.

## 📁 Project Structure

```
KiwiEngine/
├── CMakeLists.txt              # Build configuration
├── include/
│   ├── RHI/
│   │   ├── RHI.h               # Abstract RHI interfaces (Device, Context, SwapChain, Buffer...)
│   │   ├── RHITypes.h          # Type definitions (Format, BufferDesc, Viewport...)
│   │   ├── DX11/
│   │   │   ├── DX11Headers.h   # Centralized DX11/DXGI includes
│   │   │   ├── DX11Utils.h     # Format conversion utilities
│   │   │   ├── DX11Resources.h # DX11 resource implementations
│   │   │   └── DX11Device.h    # DX11 Device/Context/SwapChain declarations
│   │   └── DX12/
│   │       ├── DX12Headers.h   # Centralized DX12 includes
│   │       └── DX12Device.h    # DX12 Device/Context/SwapChain declarations
│   ├── Core/
│   │   ├── Window.h            # Win32 window wrapper
│   │   └── Application.h       # Application framework (init, update, render loop)
│   ├── Debug/
│   │   └── RenderDocIntegration.h  # RenderDoc In-App API wrapper
│   ├── Math/
│   │   └── Math.h              # Math library (Vec2/3/4, Mat4, Perspective, LookAt)
│   └── Scene/
│       ├── Mesh.h              # Mesh data (vertices, indices)
│       ├── SceneObject.h       # Scene object with transform and material
│       ├── Scene.h             # Scene management and serialization
│       └── Shaders.h           # Embedded HLSL shaders (compiled at runtime)
├── src/
│   ├── main.cpp                # Entry point — scene editor with ImGui UI
│   ├── Core/
│   │   ├── Window.cpp          # Win32 window implementation
│   │   └── Application.cpp     # App framework, message loop, RHI switching
│   ├── RHI/
│   │   ├── DX11Device.cpp      # DX11 backend implementation
│   │   └── DX12Device.cpp      # DX12 backend implementation
│   ├── Debug/
│   │   └── RenderDocIntegration.cpp  # RenderDoc runtime loading and capture API
│   └── Scene/
│       ├── Mesh.cpp            # Procedural mesh generation (Cube, Sphere, Cylinder, Floor)
│       └── Scene.cpp           # Scene serialization (JSON)
└── third_party/
    ├── imgui/                  # Dear ImGui v1.91.8 (DX11 + DX12 + Win32 backends)
    └── renderdoc/
        └── renderdoc_app.h     # RenderDoc In-App API header (MIT License)
```

## 🏗️ Architecture

```
┌──────────────────────────────────────┐
│            Application               │  ← Scene Editor + ImGui UI
├──────────────────────────────────────┤
│      Debug / RenderDoc Integration   │  ← Frame capture overlay
├──────────────────────────────────────┤
│         RHI Abstract Layer           │  ← RHIDevice, RHIContext, RHIBuffer...
├────────────┬────────────┬────────────┤
│   DX11     │   DX12     │  Vulkan    │  ← Backend implementations
│ (active)   │ (active)   │ (planned)  │
└────────────┴────────────┴────────────┘
```

The RHI layer provides a unified interface. To add a new backend:

1. Create `include/RHI/<API>/` and `src/RHI/<API>Device.cpp`
2. Implement all abstract interfaces from `RHI.h`
3. Add a new case in the `CreateRHI()` factory function
4. The `RHI_API_TYPE` enum already has `DX12` and `Vulkan` reserved

## 🔧 Troubleshooting

| Problem | Solution |
|---|---|
| `cmake` not found | Add CMake to PATH, or use the one bundled with VS2022 at `C:\Program Files\Microsoft Visual Studio\2022\<Edition>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin` |
| Missing Windows SDK | Install via Visual Studio Installer → Individual components → Windows 10/11 SDK |
| Linker errors (d3d11.lib) | Ensure Windows SDK is installed and CMake detected it correctly |
| Black window (no cube) | Check console output for errors. Ensure your GPU supports DirectX 11 Feature Level 11.0 |
| C4819 compiler warning | Source files contain Chinese comments; harmless — does not affect build or runtime |
| RenderDoc not detected | Install RenderDoc to `C:\Program Files\RenderDoc\`, or launch KiwiEngine from RenderDoc UI |
| Frame capture not working | Ensure RenderDoc is loaded before graphics device creation (the engine handles this automatically) |

## 📝 License

This project is for educational and personal use.
