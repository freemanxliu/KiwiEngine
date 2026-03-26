#include "RHI/GL/GLDevice.h"
#include <imgui.h>
#include <imgui_impl_win32.h>

// Tell ImGui OpenGL3 backend to NOT load its own GL loader (we use glad)
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <imgui_impl_opengl3.h>

#include <iostream>
#include <stdexcept>
#include <cstring>

namespace Kiwi
{

    // ============================================================
    // GL Format Helpers
    // ============================================================

    GLenum GLFormatToInternalFormat(EFormat format)
    {
        switch (format)
        {
        case EFormat::R8G8B8A8_UNORM:     return GL_RGBA8;
        case EFormat::R16G16B16A16_FLOAT:  return GL_RGBA16F;
        case EFormat::R16G16_FLOAT:        return GL_RG16F;
        case EFormat::R32G32B32A32_FLOAT:  return GL_RGBA32F;
        case EFormat::R32G32B32_FLOAT:     return GL_RGB32F;
        case EFormat::R32G32_FLOAT:        return GL_RG32F;
        case EFormat::R32_FLOAT:           return GL_R32F;
        case EFormat::R32_UINT:            return GL_R32UI;
        case EFormat::R16_UINT:            return GL_R16UI;
        case EFormat::D24_UNORM_S8_UINT:   return GL_DEPTH24_STENCIL8;
        case EFormat::D32_FLOAT:           return GL_DEPTH_COMPONENT32F;
        case EFormat::R32_TYPELESS:        return GL_DEPTH_COMPONENT32F;
        default:                           return GL_RGBA8;
        }
    }

    GLenum GLFormatToBaseFormat(EFormat format)
    {
        switch (format)
        {
        case EFormat::R8G8B8A8_UNORM:
        case EFormat::R16G16B16A16_FLOAT:
        case EFormat::R32G32B32A32_FLOAT:  return GL_RGBA;
        case EFormat::R32G32B32_FLOAT:     return GL_RGB;
        case EFormat::R16G16_FLOAT:
        case EFormat::R32G32_FLOAT:        return GL_RG;
        case EFormat::R32_FLOAT:           return GL_RED;
        case EFormat::R32_UINT:
        case EFormat::R16_UINT:            return GL_RED_INTEGER;
        case EFormat::D24_UNORM_S8_UINT:   return GL_DEPTH_STENCIL;
        case EFormat::D32_FLOAT:
        case EFormat::R32_TYPELESS:        return GL_DEPTH_COMPONENT;
        default:                           return GL_RGBA;
        }
    }

    GLenum GLFormatToType(EFormat format)
    {
        switch (format)
        {
        case EFormat::R8G8B8A8_UNORM:     return GL_UNSIGNED_BYTE;
        case EFormat::R16G16B16A16_FLOAT:
        case EFormat::R16G16_FLOAT:        return GL_HALF_FLOAT;
        case EFormat::R32G32B32A32_FLOAT:
        case EFormat::R32G32B32_FLOAT:
        case EFormat::R32G32_FLOAT:
        case EFormat::R32_FLOAT:
        case EFormat::D32_FLOAT:
        case EFormat::R32_TYPELESS:        return GL_FLOAT;
        case EFormat::R32_UINT:            return GL_UNSIGNED_INT;
        case EFormat::R16_UINT:            return GL_UNSIGNED_SHORT;
        case EFormat::D24_UNORM_S8_UINT:   return GL_UNSIGNED_INT_24_8;
        default:                           return GL_UNSIGNED_BYTE;
        }
    }

    GLenum GLTopology(EPrimitiveTopology topology)
    {
        switch (topology)
        {
        case EPrimitiveTopology::TriangleList:  return GL_TRIANGLES;
        case EPrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
        case EPrimitiveTopology::LineList:      return GL_LINES;
        case EPrimitiveTopology::LineStrip:     return GL_LINE_STRIP;
        case EPrimitiveTopology::PointList:     return GL_POINTS;
        default:                                return GL_TRIANGLES;
        }
    }

    // ============================================================
    // GLSwapChain
    // ============================================================

    GLSwapChain::GLSwapChain(HWND hwnd, HDC hdc, const SwapChainDesc& desc)
        : m_HWND(hwnd), m_HDC(hdc), m_Desc(desc)
        , m_BackBuffer(0, TextureDesc{desc.Width, desc.Height, 1, 1, desc.Format})
        , m_BackBufferRTV(0, GLTextureView::Type::RTV, GL_RGBA8)
    {
    }

    GLSwapChain::~GLSwapChain() {}

    void GLSwapChain::Present(uint32_t syncInterval)
    {
        SwapBuffers(m_HDC);
    }

    void GLSwapChain::ResizeBuffers(uint32_t width, uint32_t height)
    {
        m_Desc.Width = width;
        m_Desc.Height = height;
        glViewport(0, 0, width, height);
    }

    // ============================================================
    // GLDevice — WGL Context Creation
    // ============================================================

    GLDevice::GLDevice(bool enableDebug)
        : m_EnableDebug(enableDebug)
    {
    }

    GLDevice::~GLDevice()
    {
        if (m_HGLRC)
        {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(m_HGLRC);
        }
        if (m_HDC && m_HWND)
        {
            ReleaseDC(m_HWND, m_HDC);
        }
    }

    void GLDevice::CreateWGLContext(HWND hwnd)
    {
        m_HWND = hwnd;
        m_HDC = GetDC(hwnd);
        if (!m_HDC)
            throw std::runtime_error("GLDevice: Failed to get DC");

        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;
        pfd.iLayerType = PFD_MAIN_PLANE;

        int pixelFormat = ChoosePixelFormat(m_HDC, &pfd);
        if (!pixelFormat)
            throw std::runtime_error("GLDevice: ChoosePixelFormat failed");
        SetPixelFormat(m_HDC, pixelFormat, &pfd);

        // Create legacy context first to get wglCreateContextAttribsARB
        HGLRC tempRC = wglCreateContext(m_HDC);
        wglMakeCurrent(m_HDC, tempRC);

        // Try to create a core profile context via WGL_ARB_create_context
        typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
        auto wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)
            wglGetProcAddress("wglCreateContextAttribsARB");

        if (wglCreateContextAttribsARB)
        {
            const int attribs[] = {
                0x2091, 4,  // WGL_CONTEXT_MAJOR_VERSION_ARB
                0x2092, 5,  // WGL_CONTEXT_MINOR_VERSION_ARB
                0x9126, 0x00000001,  // WGL_CONTEXT_PROFILE_MASK_ARB = CORE
                0x2094, m_EnableDebug ? 0x00000001 : 0,  // WGL_CONTEXT_FLAGS_ARB = DEBUG
                0
            };
            m_HGLRC = wglCreateContextAttribsARB(m_HDC, nullptr, attribs);
        }

        if (!m_HGLRC)
        {
            // Fallback: use the legacy context
            m_HGLRC = tempRC;
            tempRC = nullptr;
        }
        else
        {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(tempRC);
        }

        wglMakeCurrent(m_HDC, m_HGLRC);

        // Load GL functions via glad
        // wglGetProcAddress only works for GL 1.2+ extensions;
        // GL 1.0/1.1 core functions must come from opengl32.dll via GetProcAddress.
        static HMODULE hOpenGL32 = GetModuleHandleA("opengl32.dll");
        auto combinedLoader = [](const char* name) -> GLADapiproc {
            // Try wglGetProcAddress first (extensions + GL 1.2+)
            GLADapiproc proc = (GLADapiproc)wglGetProcAddress(name);
            if (!proc || proc == (GLADapiproc)0x1 || proc == (GLADapiproc)0x2
                || proc == (GLADapiproc)0x3 || proc == (GLADapiproc)-1)
            {
                // Fallback to opengl32.dll for GL 1.0/1.1 functions
                HMODULE hGL = GetModuleHandleA("opengl32.dll");
                proc = (GLADapiproc)GetProcAddress(hGL, name);
            }
            return proc;
        };
        int version = gladLoadGL(combinedLoader);
        if (!version)
            throw std::runtime_error("GLDevice: gladLoadGL failed");

        int glMajor = GLAD_VERSION_MAJOR(version);
        int glMinor = GLAD_VERSION_MINOR(version);
        std::cout << "[Kiwi] OpenGL " << glMajor << "." << glMinor
                  << " context created" << std::endl;
        std::cout << "[Kiwi] GL Renderer: " << glGetString(GL_RENDERER) << std::endl;

        // Enable depth test by default
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CW); // Match DX left-handed (CW front face)

        if (m_EnableDebug && GLAD_GL_KHR_debug)
        {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        }
    }

    // ============================================================
    // GLDevice — Resource Creation
    // ============================================================

    std::unique_ptr<RHISwapChain> GLDevice::CreateSwapChain(const SwapChainDesc& desc)
    {
        HWND hwnd = (HWND)desc.WindowHandle;
        if (!m_HGLRC)
            CreateWGLContext(hwnd);

        return std::make_unique<GLSwapChain>(hwnd, m_HDC, desc);
    }

    std::unique_ptr<RHIBuffer> GLDevice::CreateBuffer(const BufferDesc& desc, const void* initialData)
    {
        GLuint id = 0;
        glGenBuffers(1, &id);

        GLenum target = GL_ARRAY_BUFFER;
        if (desc.BindFlags & BUFFER_USAGE_INDEX)    target = GL_ELEMENT_ARRAY_BUFFER;
        if (desc.BindFlags & BUFFER_USAGE_CONSTANT) target = GL_UNIFORM_BUFFER;

        GLenum usage = (desc.Usage == EResourceUsage::Dynamic) ? GL_DYNAMIC_DRAW :
                        (desc.Usage == EResourceUsage::Immutable) ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW;

        glBindBuffer(target, id);
        glBufferData(target, desc.SizeInBytes, initialData, usage);
        glBindBuffer(target, 0);

        return std::make_unique<GLBuffer>(id, desc);
    }

    std::unique_ptr<RHITexture> GLDevice::CreateTexture(const TextureDesc& desc, const void* initialData)
    {
        GLuint id = 0;

        bool isDepth = (desc.BindFlags & TEXTURE_HINT_DEPTH_STENCIL) != 0;
        GLenum internalFormat;
        if (isDepth)
            internalFormat = GLFormatToInternalFormat(EFormat::D32_FLOAT);
        else
            internalFormat = GLFormatToInternalFormat(desc.Format);

        if (isDepth)
        {
            // Use renderbuffer for depth (simpler, better driver support)
            glGenRenderbuffers(1, &id);
            glBindRenderbuffer(GL_RENDERBUFFER, id);
            glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, desc.Width, desc.Height);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
        }
        else
        {
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, desc.Width, desc.Height, 0,
                         GLFormatToBaseFormat(desc.Format), GLFormatToType(desc.Format), initialData);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        return std::make_unique<GLTexture>(id, desc);
    }

    std::unique_ptr<RHITextureView> GLDevice::CreateTextureView(
        RHITexture* texture, EDescriptorHeapType heapType,
        EFormat format, int mipSlice, int arraySlice)
    {
        auto* glTex = static_cast<GLTexture*>(texture);
        GLuint texID = glTex->GetID();

        GLTextureView::Type viewType;
        switch (heapType)
        {
        case EDescriptorHeapType::RTV: viewType = GLTextureView::Type::RTV; break;
        case EDescriptorHeapType::DSV: viewType = GLTextureView::Type::DSV; break;
        default:                       viewType = GLTextureView::Type::SRV; break;
        }

        GLenum glFormat = (format != EFormat::Unknown) ? GLFormatToInternalFormat(format)
                                                        : GLFormatToInternalFormat(texture->GetDesc().Format);

        return std::make_unique<GLTextureView>(texID, viewType, glFormat);
    }

    std::unique_ptr<RHIShader> GLDevice::CreateShader(EShaderType type, const void* byteCode, size_t byteCodeSize)
    {
        // In OpenGL, "bytecode" is GLSL source text
        std::string source((const char*)byteCode, byteCodeSize);

        GLenum glType = (type == EShaderType::Vertex) ? GL_VERTEX_SHADER :
                         (type == EShaderType::Pixel)  ? GL_FRAGMENT_SHADER :
                         (type == EShaderType::Geometry) ? GL_GEOMETRY_SHADER : GL_VERTEX_SHADER;

        GLuint shader = glCreateShader(glType);
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        GLint success = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            char log[1024];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::cerr << "[Kiwi GL] Shader compile error: " << log << std::endl;
            glDeleteShader(shader);
            return nullptr;
        }

        return std::make_unique<GLShader>(type, shader, source);
    }

    // Extract GLSL source for a specific shader stage from a combined file
    // Format: file starts with #version, then //!VERTEX section, then //!FRAGMENT section
    static std::string ExtractGLSLStage(const char* source, EShaderType type)
    {
        std::string src(source);
        std::string version;

        // Extract #version line if present
        auto versionPos = src.find("#version");
        if (versionPos != std::string::npos)
        {
            auto endOfLine = src.find('\n', versionPos);
            version = src.substr(versionPos, endOfLine - versionPos + 1);
        }

        std::string marker = (type == EShaderType::Vertex) ? "//!VERTEX" : "//!FRAGMENT";
        auto markerPos = src.find(marker);

        if (markerPos == std::string::npos)
        {
            // No markers found — source is a single stage, use as-is
            return src;
        }

        // Find the start of this stage (after the marker line)
        auto startPos = src.find('\n', markerPos);
        if (startPos == std::string::npos) return "";
        startPos++;

        // Find the end (next marker or end of string)
        std::string otherMarker = (type == EShaderType::Vertex) ? "//!FRAGMENT" : "//!VERTEX";
        auto endPos = src.find(otherMarker, startPos);
        if (endPos == std::string::npos)
            endPos = src.size();

        std::string stage = src.substr(startPos, endPos - startPos);

        // Prepend #version if the stage doesn't already have it
        if (stage.find("#version") == std::string::npos && !version.empty())
            stage = version + stage;

        return stage;
    }

    std::unique_ptr<RHIShader> GLDevice::CompileShader(
        EShaderType type, const char* hlslSource, const char* entryPoint,
        const char* shaderModel, const ShaderMacro* macros, uint32_t macroCount)
    {
        // For OpenGL, source is GLSL. If the source contains //!VERTEX / //!FRAGMENT
        // markers, extract the appropriate stage.
        std::string glslSource = ExtractGLSLStage(hlslSource, type);
        if (glslSource.empty())
        {
            std::cerr << "[Kiwi GL] Empty GLSL source for stage "
                      << (type == EShaderType::Vertex ? "VS" : "FS") << std::endl;
            return nullptr;
        }
        return CreateShader(type, glslSource.c_str(), glslSource.size());
    }

    std::unique_ptr<RHIInputLayout> GLDevice::CreateInputLayout(
        const InputElementDesc* elements, uint32_t elementCount, RHIShader* vertexShader)
    {
        std::vector<InputElementDesc> elems(elements, elements + elementCount);
        return std::make_unique<GLInputLayout>(elems);
    }

    std::unique_ptr<RHIPipelineState> GLDevice::CreatePipelineState()
    {
        return std::make_unique<GLPipelineState>();
    }

    std::unique_ptr<RHIPipelineState> GLDevice::CreateGraphicsPipelineState(
        RHIShader* vertexShader, RHIShader* pixelShader, RHIInputLayout* inputLayout)
    {
        auto pso = std::make_unique<GLPipelineState>();

        GLuint program = glCreateProgram();
        if (vertexShader)
            glAttachShader(program, static_cast<GLShader*>(vertexShader)->GetShaderID());
        if (pixelShader)
            glAttachShader(program, static_cast<GLShader*>(pixelShader)->GetShaderID());

        glLinkProgram(program);

        GLint success = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success)
        {
            char log[1024];
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            std::cerr << "[Kiwi GL] Program link error: " << log << std::endl;
            glDeleteProgram(program);
            return pso; // Return empty PSO
        }

        pso->SetProgram(program);
        pso->CullEnabled = (inputLayout != nullptr);
        return pso;
    }

    std::unique_ptr<RHIPipelineState> GLDevice::CreateGraphicsPipelineState(
        RHIShader* vertexShader, RHIShader* pixelShader, RHIInputLayout* inputLayout,
        const PipelineStateDesc& pipelineDesc)
    {
        auto pso = CreateGraphicsPipelineState(vertexShader, pixelShader, inputLayout);
        auto* glPSO = static_cast<GLPipelineState*>(pso.get());
        glPSO->DepthEnabled = pipelineDesc.DepthEnabled;
        glPSO->DepthWrite = pipelineDesc.DepthWrite;
        return pso;
    }

    std::unique_ptr<RHISampler> GLDevice::CreateSampler()
    {
        GLuint id = 0;
        glGenSamplers(1, &id);
        glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(id, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glSamplerParameteri(id, GL_TEXTURE_WRAP_T, GL_REPEAT);
        return std::make_unique<GLSampler>(id);
    }

    std::unique_ptr<RHISampler> GLDevice::CreateComparisonSampler()
    {
        GLuint id = 0;
        glGenSamplers(1, &id);
        glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(id, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(id, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
        return std::make_unique<GLSampler>(id);
    }

    // ============================================================
    // GLDevice — ImGui Integration
    // ============================================================

    void GLDevice::InitImGui(void* windowHandle)
    {
        if (!m_ImGuiInitialized)
        {
            ImGui_ImplWin32_Init((HWND)windowHandle);
        }
        ImGui_ImplOpenGL3_Init("#version 450");
        m_ImGuiInitialized = true;
    }

    void GLDevice::ShutdownImGui()
    {
        if (m_ImGuiInitialized)
        {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplWin32_Shutdown();
            m_ImGuiInitialized = false;
        }
    }

    void GLDevice::ImGuiNewFrame()
    {
        ImGui_ImplWin32_NewFrame();
        ImGui_ImplOpenGL3_NewFrame();
    }

    void GLDevice::ImGuiRenderDrawData(RHICommandContext* ctx)
    {
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    // ============================================================
    // GLCommandContext
    // ============================================================

    GLCommandContext::GLCommandContext()
    {
        // GL resources created lazily — GL context may not exist yet
    }

    GLCommandContext::~GLCommandContext()
    {
        if (m_FBO) glDeleteFramebuffers(1, &m_FBO);
        if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
    }

    void GLCommandContext::EnsureGLResources()
    {
        if (m_GLResourcesReady) return;
        glGenFramebuffers(1, &m_FBO);
        glGenVertexArrays(1, &m_VAO);
        m_GLResourcesReady = true;
    }

    void GLCommandContext::SetRenderTargets(RHITextureView** rtvs, uint32_t rtvCount, RHITextureView* dsv)
    {
        EnsureGLResources();

        // Check if rendering to backbuffer (texture ID == 0)
        bool isBackbuffer = (rtvCount > 0 && rtvs[0] != nullptr);
        if (isBackbuffer)
        {
            auto* view = static_cast<GLTextureView*>(rtvs[0]);
            if (view->GetTextureID() == 0)
            {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                return;
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

        for (uint32_t i = 0; i < rtvCount; i++)
        {
            if (rtvs[i])
            {
                auto* view = static_cast<GLTextureView*>(rtvs[i]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
                                       GL_TEXTURE_2D, view->GetTextureID(), 0);
            }
            else
            {
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
                                       GL_TEXTURE_2D, 0, 0);
            }
        }

        // Set draw buffers
        if (rtvCount > 0)
        {
            GLenum drawBuffers[8];
            for (uint32_t i = 0; i < rtvCount && i < 8; i++)
                drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
            glDrawBuffers(rtvCount, drawBuffers);
        }
        else
        {
            glDrawBuffer(GL_NONE);
        }

        // Depth
        if (dsv)
        {
            auto* dsvView = static_cast<GLTextureView*>(dsv);
            // Check if it's a renderbuffer (depth) or texture
            GLuint id = dsvView->GetTextureID();
            // Try as renderbuffer first
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, id);
        }
        else
        {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
        }
    }

    void GLCommandContext::ClearRenderTargetView(RHITextureView* rtv, const ClearColorValue& color)
    {
        glClearColor(color.R, color.G, color.B, color.A);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void GLCommandContext::ClearDepthStencilView(RHITextureView* dsv,
        const ClearDepthStencilValue& value, uint8_t clearFlags)
    {
        glClearDepthf(value.Depth);
        GLbitfield mask = 0;
        if (clearFlags & 1) mask |= GL_DEPTH_BUFFER_BIT;
        if (clearFlags & 2) { glClearStencil(value.Stencil); mask |= GL_STENCIL_BUFFER_BIT; }
        glClear(mask);
    }

    void GLCommandContext::SetPipelineState(RHIPipelineState* pso)
    {
        auto* glPSO = static_cast<GLPipelineState*>(pso);
        m_CurrentPSO = glPSO;

        if (glPSO)
        {
            GLuint prog = glPSO->GetProgram();
            if (prog) glUseProgram(prog);

            if (glPSO->DepthEnabled)
            {
                glEnable(GL_DEPTH_TEST);
                glDepthMask(glPSO->DepthWrite ? GL_TRUE : GL_FALSE);
            }
            else
            {
                glDisable(GL_DEPTH_TEST);
            }

            if (glPSO->CullEnabled)
            {
                glEnable(GL_CULL_FACE);
            }
            else
            {
                glDisable(GL_CULL_FACE);
            }
        }
    }

    void GLCommandContext::SetPrimitiveTopology(EPrimitiveTopology topology)
    {
        m_Topology = GLTopology(topology);
    }

    void GLCommandContext::SetVertexBuffers(uint32_t startSlot, RHIBuffer* const* buffers,
        const VertexBufferView* views, uint32_t count)
    {
        glBindVertexArray(m_VAO);

        for (uint32_t i = 0; i < count; i++)
        {
            auto* glBuf = static_cast<GLBuffer*>(buffers[i]);
            glBindBuffer(GL_ARRAY_BUFFER, glBuf->GetID());

            // Setup vertex attributes based on current input layout
            if (m_CurrentLayout)
            {
                const auto& elements = m_CurrentLayout->GetElements();
                for (size_t a = 0; a < elements.size(); a++)
                {
                    const auto& elem = elements[a];
                    if (elem.InputSlot != startSlot + i) continue;

                    GLint size = 3;
                    GLenum type = GL_FLOAT;
                    GLboolean normalized = GL_FALSE;

                    switch (elem.Format)
                    {
                    case EFormat::R32G32B32A32_FLOAT: size = 4; type = GL_FLOAT; break;
                    case EFormat::R32G32B32_FLOAT:    size = 3; type = GL_FLOAT; break;
                    case EFormat::R32G32_FLOAT:       size = 2; type = GL_FLOAT; break;
                    case EFormat::R32_FLOAT:          size = 1; type = GL_FLOAT; break;
                    default: break;
                    }

                    glEnableVertexAttribArray((GLuint)a);
                    glVertexAttribPointer((GLuint)a, size, type, normalized,
                                         views[i].StrideInBytes,
                                         (const void*)(uintptr_t)elem.AlignedByteOffset);
                }
            }
        }
    }

    void GLCommandContext::SetIndexBuffer(RHIBuffer* buffer, const IndexBufferView* view)
    {
        if (buffer)
        {
            auto* glBuf = static_cast<GLBuffer*>(buffer);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuf->GetID());
            m_IndexFormat = (view && view->Format == EFormat::R16_UINT) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
        }
    }

    void GLCommandContext::SetVertexShader(RHIShader* shader)
    {
        m_CurrentVS = static_cast<GLShader*>(shader);
    }

    void GLCommandContext::SetPixelShader(RHIShader* shader)
    {
        m_CurrentPS = static_cast<GLShader*>(shader);
    }

    void GLCommandContext::SetGeometryShader(RHIShader* shader)
    {
        // OpenGL GS support — not commonly used in this engine
    }

    void GLCommandContext::SetInputLayout(RHIInputLayout* layout)
    {
        m_CurrentLayout = static_cast<GLInputLayout*>(layout);
    }

    void GLCommandContext::SetConstantBuffer(uint32_t slot, RHIBuffer* buffer)
    {
        if (buffer)
        {
            auto* glBuf = static_cast<GLBuffer*>(buffer);
            glBindBufferBase(GL_UNIFORM_BUFFER, slot, glBuf->GetID());
        }
    }

    void GLCommandContext::SetShaderResourceView(uint32_t slot, RHITextureView* srv)
    {
        if (srv)
        {
            auto* view = static_cast<GLTextureView*>(srv);
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_2D, view->GetTextureID());
        }
        else
        {
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    void GLCommandContext::SetSampler(uint32_t slot, RHISampler* sampler)
    {
        if (sampler)
        {
            auto* glSamp = static_cast<GLSampler*>(sampler);
            glBindSampler(slot, glSamp->GetID());
        }
    }

    void GLCommandContext::SetViewports(const Viewport* viewports, uint32_t count)
    {
        if (count > 0)
        {
            // GL viewport Y is bottom-up, but we handle it at projection level
            glViewport((GLint)viewports[0].TopLeftX, (GLint)viewports[0].TopLeftY,
                       (GLsizei)viewports[0].Width, (GLsizei)viewports[0].Height);
            glDepthRangef(viewports[0].MinDepth, viewports[0].MaxDepth);
        }
    }

    void GLCommandContext::SetScissorRects(const ScissorRect* rects, uint32_t count)
    {
        if (count > 0)
        {
            glEnable(GL_SCISSOR_TEST);
            glScissor(rects[0].Left, rects[0].Top,
                      rects[0].Right - rects[0].Left,
                      rects[0].Bottom - rects[0].Top);
        }
    }

    void GLCommandContext::Draw(uint32_t vertexCount, uint32_t vertexStart)
    {
        EnsureGLResources();
        glBindVertexArray(m_VAO);
        glDrawArrays(m_Topology, vertexStart, vertexCount);
    }

    void GLCommandContext::DrawIndexed(uint32_t indexCount, uint32_t indexStart, int32_t vertexOffset)
    {
        EnsureGLResources();
        glBindVertexArray(m_VAO);
        size_t indexSize = (m_IndexFormat == GL_UNSIGNED_SHORT) ? 2 : 4;
        glDrawElementsBaseVertex(m_Topology, indexCount, m_IndexFormat,
                                 (const void*)(uintptr_t)(indexStart * indexSize),
                                 vertexOffset);
    }

    void GLCommandContext::Flush()
    {
        glFlush();
    }

    // ============================================================
    // Factory
    // ============================================================

    void CreateGLRHI(const RHIInitParams& params,
        std::unique_ptr<RHIDevice>& outDevice,
        std::unique_ptr<RHICommandContext>& outContext)
    {
        auto device = std::make_unique<GLDevice>(params.EnableDebug);
        // Note: GL context is created lazily in CreateSwapChain (needs HWND)
        // CommandContext doesn't need much for GL
        auto context = std::make_unique<GLCommandContext>();

        outDevice = std::move(device);
        outContext = std::move(context);
    }

} // namespace Kiwi
