# 🥝 KiwiEngine

A lightweight 3D rendering engine built from scratch with DirectX 11, featuring a custom RHI (Render Hardware Interface) abstraction layer designed for future multi-backend support.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![DirectX 11](https://img.shields.io/badge/DirectX-11-green)
![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey)
![Build](https://img.shields.io/badge/Build-CMake-orange)

## ✨ Features

- **RHI Abstraction Layer** — Clean separation between rendering logic and graphics API, with extension points for DX12/Vulkan
- **DX11 Backend** — Full DirectX 11 implementation (device, context, swap chain, shaders, buffers, pipeline state)
- **Built-in Math Library** — Vec2/3/4, Mat4, perspective/orthographic projection, LookAt camera (left-hand coordinate system)
- **Mesh Generation** — Procedural cube, sphere, and plane primitives
- **Runtime Shader Compilation** — HLSL vertex/pixel shaders compiled at startup via D3DCompile
- **Phong Lighting** — Lambert diffuse + Blinn-Phong specular shading

## 📋 Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| **OS** | Windows 10/11 | DirectX 11 required |
| **Visual Studio** | 2022 (v17.x) | Community / Professional / Enterprise |
| **MSVC Toolchain** | v143+ | Installed with VS2022 "Desktop development with C++" workload |
| **CMake** | 3.20+ | Bundled with VS2022, or install separately |
| **Windows SDK** | 10.0.19041+ | Includes d3d11, d3dcompiler, dxgi headers and libs |

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

A 1280×720 window will appear showing a **rotating colored cube** with Phong lighting. Close the window or press the close button to exit.

No external assets, textures, or config files are needed — everything (including shaders) is embedded in the source code.

## 📁 Project Structure

```
KiwiEngine/
├── CMakeLists.txt              # Build configuration
├── include/
│   ├── RHI/
│   │   ├── RHI.h               # Abstract RHI interfaces (Device, Context, SwapChain, Buffer...)
│   │   ├── RHITypes.h          # Type definitions (Format, BufferDesc, Viewport...)
│   │   └── DX11/
│   │       ├── DX11Headers.h   # Centralized DX11/DXGI includes
│   │       ├── DX11Utils.h     # Format conversion utilities
│   │       ├── DX11Resources.h # DX11 resource implementations (Buffer, Texture, Shader...)
│   │       └── DX11Device.h    # DX11 Device/Context/SwapChain declarations
│   ├── Core/
│   │   ├── Window.h            # Win32 window wrapper
│   │   └── Application.h       # Application framework (init, update, render loop)
│   ├── Math/
│   │   └── Math.h              # Math library (Vec2/3/4, Mat4, Perspective, LookAt)
│   └── Scene/
│       ├── Mesh.h              # Mesh data (vertices, indices)
│       └── Shaders.h           # Embedded HLSL shaders (compiled at runtime)
└── src/
    ├── main.cpp                # Entry point — creates scene and renders a rotating cube
    ├── Core/
    │   ├── Window.cpp          # Win32 window implementation
    │   └── Application.cpp     # App framework, message loop, resize handling
    ├── RHI/
    │   └── DX11Device.cpp      # Complete DX11 backend + RHI factory
    └── Scene/
        └── Mesh.cpp            # Procedural mesh generation (Cube, Sphere, Plane)
```

## 🏗️ Architecture

```
┌──────────────────────────────────────┐
│            Application               │  ← Game/App logic
├──────────────────────────────────────┤
│         RHI Abstract Layer           │  ← RHIDevice, RHIContext, RHIBuffer...
├────────────┬────────────┬────────────┤
│   DX11     │   DX12     │  Vulkan    │  ← Backend implementations
│ (current)  │ (planned)  │ (planned)  │
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

## 📝 License

This project is for educational and personal use.
