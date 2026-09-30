#include "Core/RendererUtils.h"

#include "Scene/GLShaders.h"
#include "Scene/PrimitiveComponent.h"
#include "Scene/MetalShaders.h"
#include "Scene/Shaders.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

namespace Kiwi
{

void ComputeWorldAABB(const PrimitiveComponent& mesh, Vec3& outMin, Vec3& outMax)
{
    Mat4 world = mesh.GetWorldMatrix();
    const auto& verts = mesh.MeshData.GetVertices();

    if (verts.empty())
    {
        outMin = outMax = mesh.Position;
        return;
    }

    outMin = { 1e30f, 1e30f, 1e30f };
    outMax = { -1e30f, -1e30f, -1e30f };

    for (const auto& v : verts)
    {
        float wx = v.Position.x * world.m[0][0] + v.Position.y * world.m[1][0] + v.Position.z * world.m[2][0] + world.m[3][0];
        float wy = v.Position.x * world.m[0][1] + v.Position.y * world.m[1][1] + v.Position.z * world.m[2][1] + world.m[3][1];
        float wz = v.Position.x * world.m[0][2] + v.Position.y * world.m[1][2] + v.Position.z * world.m[2][2] + world.m[3][2];

        outMin.x = std::min(outMin.x, wx); outMin.y = std::min(outMin.y, wy); outMin.z = std::min(outMin.z, wz);
        outMax.x = std::max(outMax.x, wx); outMax.y = std::max(outMax.y, wy); outMax.z = std::max(outMax.z, wz);
    }
}

bool IsDeferredRHI(RHI_API_TYPE api)
{
    return api == RHI_API_TYPE::DX11 || api == RHI_API_TYPE::DX12 || api == RHI_API_TYPE::METAL;
}

const char* BuiltinVertexShader(RHI_API_TYPE api)
{
    if (api == RHI_API_TYPE::METAL)
        return g_VertexShaderMSL;
    if (api == RHI_API_TYPE::OPENGL || api == RHI_API_TYPE::VULKAN)
        return g_VertexShaderGLSL;
    return g_VertexShaderHLSL;
}

std::string ForwardShaderDirectory(RHI_API_TYPE api, const std::string& hlslDir)
{
    namespace fs = std::filesystem;
    if (api == RHI_API_TYPE::METAL || api == RHI_API_TYPE::OPENGL || api == RHI_API_TYPE::VULKAN)
    {
        const char* folder = api == RHI_API_TYPE::METAL ? "MetalShaders" : "GLShaders";
        std::string dir = hlslDir + "/../" + folder;
        if (fs::exists(dir))
            return dir;
        std::string fallback = hlslDir + "/../../../" + folder;
        if (fs::exists(fallback))
            return fallback;
        return dir;
    }
    return hlslDir;
}

std::string ReadShaderFileWithIncludes(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        std::cerr << "[Kiwi] Failed to read shader file: " << filePath << std::endl;
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    std::string parentDir = filePath.substr(0, filePath.find_last_of("/\\"));
    std::string result;
    std::istringstream stream(source);
    std::string line;
    while (std::getline(stream, line))
    {
        size_t pos = line.find("#include");
        if (pos != std::string::npos)
        {
            size_t q1 = line.find('"', pos);
            size_t q2 = line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos)
            {
                std::string includeName = line.substr(q1 + 1, q2 - q1 - 1);
                std::string includePath = parentDir + "/" + includeName;
                std::ifstream incFile(includePath);
                if (incFile.is_open())
                {
                    std::string incContent((std::istreambuf_iterator<char>(incFile)),
                                            std::istreambuf_iterator<char>());
                    result += incContent + "\n";
                    continue;
                }
            }
        }
        result += line + "\n";
    }
    return result;
}

} // namespace Kiwi
