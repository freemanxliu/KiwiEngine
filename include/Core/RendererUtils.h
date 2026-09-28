#pragma once

#include "RHI/RHI.h"

#include <string>

namespace Kiwi
{

// Backends with a G-Buffer path. GL/Vulkan only render forward.
bool IsDeferredRHI(RHI_API_TYPE api);

// Built-in vertex shader source used to create the shared input layout.
const char* BuiltinVertexShader(RHI_API_TYPE api);

// Forward/material shaders live in MetalShaders/ or GLShaders/ next to the HLSL Shaders/ folder.
std::string ForwardShaderDirectory(RHI_API_TYPE api, const std::string& hlslDir);

// Reads a shader file and inlines its #include "..." lines (FXC D3DCompile needs expanded source).
// Returns an empty string if the file cannot be opened.
std::string ReadShaderFileWithIncludes(const std::string& filePath);

} // namespace Kiwi
