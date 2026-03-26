# KiwiEngine RHI (Rendering Hardware Interface)

KiwiEngine 的 RHI 层提供统一的图形 API 抽象，支持 DX11 / DX12 / OpenGL 三套后端运行时切换。应用层代码 100% 后端无关，零 `isDX12` 分支，零 `static_cast` 到后端类型。

## 架构总览

```
RHI 抽象层
├── RHITypes.h              — 枚举 / 结构体 / 描述符定义
├── RHI.h                   — 抽象基类接口 + 工厂函数
├── DX11/                   — Direct3D 11 后端
│   ├── DX11Headers.h       — DX11 系统头文件
│   ├── DX11Utils.h         — DX11 工具函数（格式转换等）
│   ├── DX11Resources.h     — 资源包装类
│   └── DX11Device.h        — Device / SwapChain / CommandContext
├── DX12/                   — Direct3D 12 后端
│   ├── DX12Headers.h       — DX12 系统头文件
│   ├── DX12Resources.h     — 资源包装类
│   └── DX12Device.h        — Device / SwapChain / CommandContext + 格式转换
├── GL/                     — OpenGL 后端
│   ├── GLHeaders.h          — OpenGL / WGL 系统头文件
│   ├── GLResources.h        — 资源包装类
│   └── GLDevice.h           — Device / SwapChain / CommandContext
└── DXC/                    — DXC 着色器编译器
    └── DXCCompiler.h        — DXC 单例封装（IDxcCompiler3）
```

## 继承关系

```
RHIBuffer ─────────── DX11Buffer         DX12Buffer         GLBuffer
RHITexture ────────── DX11Texture        DX12Texture        GLTexture
RHITextureView ────── DX11TextureView    DX12TextureView    GLTextureView
RHIShader ─────────── DX11Shader         DX12Shader         GLShader
RHIInputLayout ────── DX11InputLayout    DX12InputLayout    GLInputLayout
RHIPipelineState ──── DX11PipelineState  DX12PipelineState  GLPipelineState
RHISampler ────────── DX11Sampler        DX12Sampler        GLSampler
RHISwapChain ──────── DX11SwapChain      DX12SwapChain      GLSwapChain
RHIDevice ─────────── DX11Device         DX12Device         GLDevice
RHICommandContext ─── DX11CommandContext  DX12CommandContext  GLCommandContext
```

## 核心接口

### RHIDevice（工厂类）

所有 GPU 资源通过 Device 创建，返回 `std::unique_ptr<>`：

| 方法 | 说明 |
|------|------|
| `CreateSwapChain(desc)` | 创建交换链 |
| `CreateBuffer(desc, data)` | 创建顶点/索引/常量缓冲 |
| `CreateTexture(desc, data)` | 创建纹理（含深度缓冲） |
| `CreateTextureView(texture, heapType, format)` | 创建 RTV / DSV / SRV |
| `CompileShader(type, source, entry, model)` | 从源码编译着色器 |
| `CreateShader(type, bytecode, size)` | 从字节码创建着色器 |
| `CreateInputLayout(elements, count, vs)` | 创建输入布局 |
| `CreatePipelineState()` | 创建空管线状态 |
| `CreateGraphicsPipelineState(vs, ps, layout)` | 创建图形管线状态 |
| `CreateGraphicsPipelineState(vs, ps, layout, desc)` | 创建 MRT 管线状态 |
| `CreateSampler()` | 创建采样器 |
| `CreateComparisonSampler()` | 创建比较采样器（阴影 PCF） |
| `InitImGui / ShutdownImGui / ImGuiNewFrame / ImGuiRenderDrawData` | ImGui 集成 |

### RHICommandContext（命令录制）

| 方法 | 说明 |
|------|------|
| `BeginFrame / EndFrame` | 帧生命周期（DX12 需要，DX11/GL 空操作） |
| `ResourceBarrier` | 资源状态转换（DX12 需要，DX11/GL 空操作） |
| `SetRenderTargets / ClearRenderTargetView / ClearDepthStencilView` | 渲染目标 |
| `SetPipelineState` | 绑定管线状态 |
| `SetVertexBuffers / SetIndexBuffer / SetInputLayout` | 顶点输入 |
| `SetVertexShader / SetPixelShader / SetGeometryShader` | 着色器绑定 |
| `SetConstantBuffer / SetShaderResourceView / SetSampler` | 资源绑定 |
| `SetViewports / SetScissorRects` | 视口 / 裁剪 |
| `Draw / DrawIndexed` | 绘制调用 |
| `Flush` | 提交命令 |

## Shader 编译

### DX11：HLSL → DXBC（FXC）

```
HLSL 源码 (.hlsl)
    ↓  D3DCompile()   ← d3dcompiler.dll 内的 FXC 编译器
DXBC 字节码 (ID3DBlob)   ← DirectX Bytecode Container，魔数 0x44584243
    ↓
GPU 原生指令              ← 驱动翻译，对用户不可见
```

- **编译器**：FXC (Microsoft HLSL Compiler)，封装在 `d3dcompiler.dll` 中
- **Shader Model**：SM 5.0（`vs_5_0` / `ps_5_0`）
- **编译产物**：DXBC (DirectX Bytecode) — 中间字节码，非 GPU 原生机器码

### DX12：HLSL → DXIL（DXC）+ FXC 回退

```
HLSL 源码 (.hlsl)
    ↓  UpgradeProfileForDXC()  ← 自动将 vs_5_0 升级为 vs_6_0
    ↓  IDxcCompiler3::Compile()  ← dxcompiler.dll 内的 DXC 编译器
DXIL 字节码 (IDxcBlob)   ← DirectX Intermediate Language
    ↓
GPU 原生指令              ← 驱动翻译

失败时回退：
    ↓  D3DCompile()  ← FXC 编译器，使用原始 SM 5.0
DXBC 字节码
```

- **编译器**：DXC (DirectX Shader Compiler)，运行时通过 `LoadLibrary("dxcompiler.dll")` 加载
- **DXCCompiler**：单例封装，`include/RHI/DXC/DXCCompiler.h`
- **Shader Model**：SM 6.0（DXC 不支持 SM 5.x，自动升级）
- **编译产物**：DXIL (DirectX Intermediate Language) — 比 DXBC 更现代的中间格式
- **运行时依赖**：`dxcompiler.dll` + `dxil.dll`（CMake 自动复制到 `build/bin/`）

#### DX11 vs DX12 的差异

| | DX11 | DX12 |
|---|---|---|
| 编译器 | FXC (`D3DCompile`) | DXC (`IDxcCompiler3`) + FXC 回退 |
| Shader Model | SM 5.0 | SM 6.0 (DXIL) / SM 5.0 (DXBC 回退) |
| 创建 Shader 对象 | `CreateVertexShader(blob)` 等，每种类型一个 API 对象 | 不创建独立对象，blob 直接存储 |
| 绑定方式 | `VSSetShader()` / `PSSetShader()` 分别设置 | 所有 shader 打包进 PSO，`SetPipelineState()` 一步切换 |
| InputLayout | `CreateInputLayout(blob, elements)` → API 对象 | 只是 element desc 数组，塞进 PSO 描述 |
| PSO | 不存在真正的 PSO，用 Blend/Rasterizer/DepthStencil 三个独立 state 模拟 | `ID3D12PipelineState` 真正的 PSO 对象 |
| 驱动编译时机 | 每次 `Create*Shader` 时 | `CreateGraphicsPipelineState` 时（较慢） |

### OpenGL：GLSL

```
GLSL 源码 (.glsl)
    ↓  ExtractGLSLStage()  ← 通过 //!VERTEX / //!FRAGMENT 标记提取各阶段
    ↓  glCreateShader + glShaderSource + glCompileShader
编译后的 Shader 对象 (GLuint)
    ↓  glCreateProgram + glAttachShader + glLinkProgram
Linked Program (GLuint)   ← 相当于 DX12 的 PSO
    ↓
GPU 原生指令               ← 驱动翻译
```

- **编译器**：GPU 驱动内置的 GLSL 编译器（NVIDIA/AMD/Intel 各自实现）
- **GLSL 版本**：`#version 450 core`（OpenGL 4.5）
- **文件格式**：单个 `.glsl` 文件包含 vertex 和 fragment 两个阶段，用 `//!VERTEX` / `//!FRAGMENT` 标记分割
- **着色器目录**：`GLShaders/`（内置：DefaultLit.glsl、Unlit.glsl、Wireframe.glsl）
- **ShaderLibrary** 根据 `GetApiType()` 自动选择 HLSL (`Shaders/`) 或 GLSL (`GLShaders/`) 源文件
- **无标准中间字节码**：GLSL 编译直接由驱动完成，不像 DXBC/DXIL 有统一中间格式

## 运行时 RHI 切换

通过 `Application::SwitchRHI(RHI_API_TYPE)` 实现，在帧边界安全执行：

```
SwitchRHI(newType):
  1. OnRHIShutdown()              ← 通知子类释放所有 GPU 资源
  2. 释放 DepthStencil / DSV / DepthSRV
  3. 释放 SwapChain
  4. 释放 CommandContext + Device
  5. CreateRHI(newType)           ← 工厂函数创建新后端
  6. CreateSwapChain()
  7. RecreateDepthStencil()
  8. SetViewports + SetScissorRects
  9. OnRHIReady()                 ← 通知子类重建所有 GPU 资源
```

UI 菜单路径：`Rendering → RHI → { Direct3D 11 | Direct3D 12 | OpenGL }`

## 设计要点

1. **抽象粒度**：所有资源基类只暴露 `GetNativeHandle() → void*`，后端内部通过 `static_cast` 向下转换
2. **所有权模型**：Device 作为工厂，返回 `std::unique_ptr<>` 管理资源生命周期
3. **差异处理**：
   - `BeginFrame/EndFrame`、`ResourceBarrier` — DX12 真正执行，DX11/GL 空实现
   - `PipelineState` — DX11 是离散 state 包装，DX12 是真 PSO，GL 是 linked program
   - `InputLayout` — DX11 是 API 对象，DX12 是 desc 数组，GL 是 element 描述 + VAO
4. **ImGui 集成**：每个后端有独立的 ImGui 实现后端（`imgui_impl_dx11` / `imgui_impl_dx12` / `imgui_impl_opengl3`）
5. **第三方依赖**：
   - DX11/DX12：`d3d11.lib` / `d3d12.lib` / `d3dcompiler.lib` / `dxgi.lib`
   - OpenGL：`opengl32.lib` + glad (OpenGL 4.5 core loader, `third_party/glad/`)

## 未来扩展

- `RHI_API_TYPE::VULKAN` 已预留枚举值
- DX12 已集成 DXC 编译器，支持 SM 6.0 特性；如需 wave intrinsics、mesh shader 等高级特性，只需在 shader 中使用对应 SM 6.x profile
- OpenGL 后端可通过 `GL_ARB_gl_spirv` 扩展支持 SPIR-V，实现与 Vulkan 共享 shader 编译管线
- OpenGL 后端的延迟渲染管线（G-Buffer + CSM 阴影）待实现，当前为前向渲染
