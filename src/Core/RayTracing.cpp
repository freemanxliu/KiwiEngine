#include "KiwiEngineApp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{

struct TraceRay
{
    Vec3 Origin;
    Vec3 Direction;
    Vec3 InvDirection;
};

struct Triangle
{
    Vec3 A, B, C;
    Vec3 Normal;
    Vec3 Color;
};

struct BvhNode
{
    Vec3 Min;
    Vec3 Max;
    int Left = -1;
    int Right = -1;
    int Start = 0;
    int Count = 0;
};

float Sample01(uint32_t n)
{
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return (n & 0x00ffffffu) / float(0x01000000u);
}

Vec3 TransformPoint(const Mat4& world, const Vec3& p)
{
    return {
        p.x * world.m[0][0] + p.y * world.m[1][0] + p.z * world.m[2][0] + world.m[3][0],
        p.x * world.m[0][1] + p.y * world.m[1][1] + p.z * world.m[2][1] + world.m[3][1],
        p.x * world.m[0][2] + p.y * world.m[1][2] + p.z * world.m[2][2] + world.m[3][2]
    };
}

Vec3 TransformDirection(const Mat4& m, float x, float y, float z, float w, float& outW)
{
    outW = x * m.m[0][3] + y * m.m[1][3] + z * m.m[2][3] + w * m.m[3][3];
    return {
        x * m.m[0][0] + y * m.m[1][0] + z * m.m[2][0] + w * m.m[3][0],
        x * m.m[0][1] + y * m.m[1][1] + z * m.m[2][1] + w * m.m[3][1],
        x * m.m[0][2] + y * m.m[1][2] + z * m.m[2][2] + w * m.m[3][2]
    };
}

TraceRay MakeRay(const Vec3& origin, const Vec3& direction)
{
    TraceRay ray;
    ray.Origin = origin;
    ray.Direction = direction;
    const float kHuge = 1.0e30f;
    ray.InvDirection = {
        std::abs(direction.x) > 1.0e-8f ? 1.0f / direction.x : std::copysign(kHuge, direction.x),
        std::abs(direction.y) > 1.0e-8f ? 1.0f / direction.y : std::copysign(kHuge, direction.y),
        std::abs(direction.z) > 1.0e-8f ? 1.0f / direction.z : std::copysign(kHuge, direction.z)
    };
    return ray;
}

void Expand(Vec3& mn, Vec3& mx, const Vec3& p)
{
    mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
    mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
}

bool RayBox(const TraceRay& ray, const Vec3& mn, const Vec3& mx, float tMax)
{
    float t0 = (mn.x - ray.Origin.x) * ray.InvDirection.x;
    float t1 = (mx.x - ray.Origin.x) * ray.InvDirection.x;
    if (t0 > t1) std::swap(t0, t1);
    float ty0 = (mn.y - ray.Origin.y) * ray.InvDirection.y;
    float ty1 = (mx.y - ray.Origin.y) * ray.InvDirection.y;
    if (ty0 > ty1) std::swap(ty0, ty1);
    t0 = std::max(t0, ty0);
    t1 = std::min(t1, ty1);
    float tz0 = (mn.z - ray.Origin.z) * ray.InvDirection.z;
    float tz1 = (mx.z - ray.Origin.z) * ray.InvDirection.z;
    if (tz0 > tz1) std::swap(tz0, tz1);
    t0 = std::max(t0, tz0);
    t1 = std::min(t1, tz1);
    return t1 >= std::max(t0, 0.0f) && t0 < tMax;
}

bool IntersectTriangle(const Triangle& tri, const TraceRay& ray, float tMax, float& tHit, Vec3& normal)
{
    const float eps = 1.0e-6f;
    Vec3 e1 = tri.B - tri.A;
    Vec3 e2 = tri.C - tri.A;
    Vec3 p = ray.Direction.Cross(e2);
    float det = e1.Dot(p);
    if (std::abs(det) < eps)
        return false;
    float invDet = 1.0f / det;
    Vec3 s = ray.Origin - tri.A;
    float u = s.Dot(p) * invDet;
    if (u < 0.0f || u > 1.0f)
        return false;
    Vec3 q = s.Cross(e1);
    float v = ray.Direction.Dot(q) * invDet;
    if (v < 0.0f || u + v > 1.0f)
        return false;
    float t = e2.Dot(q) * invDet;
    if (t < 1.0e-3f || t > tMax)
        return false;
    tHit = t;
    normal = tri.Normal;
    if (normal.Dot(ray.Direction) > 0.0f)
        normal = normal * -1.0f;
    return true;
}

int BuildBvh(std::vector<BvhNode>& nodes, std::vector<int>& order, const std::vector<Triangle>& tris, int begin, int end)
{
    int index = (int)nodes.size();
    nodes.push_back({});
    Vec3 mn{ 1.0e30f, 1.0e30f, 1.0e30f };
    Vec3 mx{ -1.0e30f, -1.0e30f, -1.0e30f };
    for (int i = begin; i < end; ++i)
    {
        const Triangle& tri = tris[order[i]];
        Expand(mn, mx, tri.A);
        Expand(mn, mx, tri.B);
        Expand(mn, mx, tri.C);
    }
    nodes[index].Min = mn;
    nodes[index].Max = mx;
    int count = end - begin;
    if (count <= 8)
    {
        nodes[index].Start = begin;
        nodes[index].Count = count;
        return index;
    }

    Vec3 extent = mx - mn;
    int axis = 0;
    if (extent.y > extent.x && extent.y >= extent.z) axis = 1;
    else if (extent.z > extent.x) axis = 2;
    int mid = begin + count / 2;
    std::nth_element(order.begin() + begin, order.begin() + mid, order.begin() + end,
        [&](int a, int b)
        {
            auto center = [&](int id)
            {
                const Triangle& tri = tris[id];
                Vec3 c = (tri.A + tri.B + tri.C) * (1.0f / 3.0f);
                return axis == 0 ? c.x : axis == 1 ? c.y : c.z;
            };
            return center(a) < center(b);
        });
    int left = BuildBvh(nodes, order, tris, begin, mid);
    int right = BuildBvh(nodes, order, tris, mid, end);
    nodes[index].Left = left;
    nodes[index].Right = right;
    return index;
}

struct Hit
{
    float T = 1.0e30f;
    Vec3 Normal;
    Vec3 Color;
};

bool ClosestHit(const std::vector<BvhNode>& nodes, const std::vector<int>& order,
    const std::vector<Triangle>& tris, const TraceRay& ray, float tMax, Hit& hit)
{
    if (nodes.empty())
        return false;
    int stack[64];
    int top = 0;
    stack[top++] = 0;
    bool found = false;
    while (top > 0)
    {
        const BvhNode& node = nodes[stack[--top]];
        if (!RayBox(ray, node.Min, node.Max, tMax))
            continue;
        if (node.Left < 0)
        {
            for (int i = 0; i < node.Count; ++i)
            {
                const Triangle& tri = tris[order[node.Start + i]];
                float t = 0.0f;
                Vec3 n;
                if (IntersectTriangle(tri, ray, tMax, t, n))
                {
                    tMax = t;
                    hit.T = t;
                    hit.Normal = n;
                    hit.Color = tri.Color;
                    found = true;
                }
            }
        }
        else if (top < 62)
        {
            stack[top++] = node.Left;
            stack[top++] = node.Right;
        }
    }
    return found;
}

bool AnyHit(const std::vector<BvhNode>& nodes, const std::vector<int>& order,
    const std::vector<Triangle>& tris, const TraceRay& ray, float tMax)
{
    if (nodes.empty())
        return false;
    int stack[64];
    int top = 0;
    stack[top++] = 0;
    while (top > 0)
    {
        const BvhNode& node = nodes[stack[--top]];
        if (!RayBox(ray, node.Min, node.Max, tMax))
            continue;
        if (node.Left < 0)
        {
            for (int i = 0; i < node.Count; ++i)
            {
                float t = 0.0f;
                Vec3 n;
                if (IntersectTriangle(tris[order[node.Start + i]], ray, tMax, t, n))
                    return true;
            }
        }
        else if (top < 62)
        {
            stack[top++] = node.Left;
            stack[top++] = node.Right;
        }
    }
    return false;
}

const char* kBlitHLSL = R"(
Texture2D g_Color : register(t0);
SamplerState g_Sampler : register(s0);
struct VSOut { float4 Position : SV_POSITION; float2 TexCoord : TEXCOORD0; };
VSOut VSMain(uint vertexID : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    o.Position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    o.TexCoord = float2(uv.x, 1.0 - uv.y);
    return o;
}
float4 PSMain(VSOut i) : SV_TARGET
{
    return g_Color.Sample(g_Sampler, i.TexCoord);
}
)";

const char* kBlitMSL = R"(
//!VERTEX
struct VSOut { float4 position [[position]]; float2 uv; };
vertex VSOut VSMain(uint vertexID [[vertex_id]])
{
    float2 uv = float2(float((vertexID << 1) & 2), float(vertexID & 2));
    VSOut o;
    o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv = float2(uv.x, 1.0 - uv.y);
    return o;
}
//!FRAGMENT
struct FSIn { float4 position [[position]]; float2 uv; };
fragment float4 PSMain(FSIn in [[stage_in]],
                       texture2d<float> colorTex [[texture(0)]],
                       sampler colorSampler [[sampler(0)]])
{
    return colorTex.sample(colorSampler, in.uv);
}
)";

const char* kBlitGLSL = R"(
//!VERTEX
#version 410 core
out vec2 vUV;
void main()
{
    vec2 uv = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    vUV = vec2(uv.x, 1.0 - uv.y);
}
//!FRAGMENT
#version 410 core
in vec2 vUV;
layout(binding = 0) uniform sampler2D uColor;
out vec4 oColor;
void main()
{
    oColor = texture(uColor, vUV);
}
)";

} // namespace

void KiwiEngineApp::RenderRayTracing(RHICommandContext* ctx, RHITextureView* sceneRTV, const Viewport& vp, const ScissorRect& sr)
{
    ctx->BeginEvent("Ray Tracing");
    m_PassTimer.Begin("Ray Tracing");

    ctx->SetRenderTargets(&sceneRTV, 1, nullptr);
    ctx->SetViewports(&vp, 1);
    ctx->SetScissorRects(&sr, 1);
    ClearColorValue clearColor = { 0.05f, 0.06f, 0.08f, 1.0f };
    ctx->ClearRenderTargetView(sceneRTV, clearColor);

    const uint32_t screenW = std::max(1u, (uint32_t)vp.Width);
    const uint32_t screenH = std::max(1u, (uint32_t)vp.Height);
    const float resolutionScale = std::min(1.0f, std::max(0.01f, m_RayTracingResolutionPercent / 100.0f));
    const int samplesPerPixel = std::max(1, m_RayTracingSamplesPerPixel);
    const uint32_t traceW = std::max(1u, (uint32_t)(screenW * resolutionScale));
    const uint32_t traceH = std::max(1u, (uint32_t)(screenH * resolutionScale));
    m_RayTraceWidth = traceW;
    m_RayTraceHeight = traceH;

    std::vector<Triangle> triangles;
    for (const RenderItem& item : m_RenderList)
    {
        MeshComponent* mesh = item.MeshComp;
        if (!mesh)
            continue;
        const auto& verts = mesh->MeshData.GetVertices();
        const auto& indices = mesh->MeshData.GetIndices();
        if (verts.empty() || indices.size() < 3)
            continue;
        Material* parent = m_MaterialLibrary.GetMaterial(mesh->Material.Parent);
        Vec4 color = mesh->Material.GetColor(parent, "_Color", { 0.8f, 0.8f, 0.8f, 1.0f });
        Mat4 world = mesh->GetWorldMatrix();
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
            if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
                continue;
            Triangle tri;
            tri.A = TransformPoint(world, verts[i0].Position);
            tri.B = TransformPoint(world, verts[i1].Position);
            tri.C = TransformPoint(world, verts[i2].Position);
            tri.Normal = (tri.B - tri.A).Cross(tri.C - tri.A);
            if (tri.Normal.Dot(tri.Normal) < 1.0e-12f)
                continue;
            tri.Normal = tri.Normal.Normalize();
            tri.Color = { color.x, color.y, color.z };
            triangles.push_back(tri);
        }
    }

    std::vector<int> order(triangles.size());
    std::vector<BvhNode> nodes;
    if (!triangles.empty())
    {
        for (int i = 0; i < (int)triangles.size(); ++i)
            order[i] = i;
        nodes.reserve(triangles.size() * 2);
        BuildBvh(nodes, order, triangles, 0, (int)triangles.size());
    }

    Mat4 viewProj = m_ViewMatrix * m_ProjectionMatrix;
    Mat4 invViewProj = viewProj.Inverse();
    const bool unlit = m_ViewMode == EViewMode::Unlit;
    std::vector<uint8_t> pixels((size_t)traceW * traceH * 4);

    for (uint32_t y = 0; y < traceH; ++y)
    {
        for (uint32_t x = 0; x < traceW; ++x)
        {
            Vec3 radiance = { 0.0f, 0.0f, 0.0f };
            for (int sample = 0; sample < samplesPerPixel; ++sample)
            {
                float jitterX = (sample + 0.5f) / (float)samplesPerPixel;
                float jitterY = Sample01(x * 1973u ^ y * 9277u ^ (uint32_t)sample * 26699u);
                float ndcX = (x + jitterX) / (float)traceW * 2.0f - 1.0f;
                float sampleNdcY = 1.0f - (y + jitterY) / (float)traceH * 2.0f;
                float clipW = 1.0f;
                Vec3 farPos = TransformDirection(invViewProj, ndcX, sampleNdcY, 1.0f, 1.0f, clipW);
                if (std::abs(clipW) > 1.0e-8f)
                    farPos = farPos * (1.0f / clipW);
                Vec3 dir = (farPos - m_CameraPosition).Normalize();
                TraceRay ray = MakeRay(m_CameraPosition, dir);

                Vec3 sampleRadiance = { 0.05f, 0.06f, 0.08f };
                Hit hit;
                if (ClosestHit(nodes, order, triangles, ray, 1.0e30f, hit))
                {
                    Vec3 pos = ray.Origin + ray.Direction * hit.T;
                    Vec3 n = hit.Normal;
                    if (unlit)
                    {
                        sampleRadiance = hit.Color;
                    }
                    else
                    {
                        sampleRadiance = hit.Color * 0.08f;
                        for (int li = 0; li < m_NumActiveLights; ++li)
                        {
                            const GPULightData& light = m_LightDataCache[li];
                            Vec3 L;
                            float reach = 1.0e4f;
                            float atten = 1.0f;
                            if (light.Type == 0)
                            {
                                L = Vec3(light.DirectionOrPos[0], light.DirectionOrPos[1], light.DirectionOrPos[2]).Normalize();
                            }
                            else
                            {
                                Vec3 toLight = Vec3(light.DirectionOrPos[0], light.DirectionOrPos[1], light.DirectionOrPos[2]) - pos;
                                float dist = toLight.Length();
                                if (dist > light.Radius)
                                    continue;
                                L = toLight * (1.0f / std::max(dist, 0.0001f));
                                reach = dist - 0.001f;
                                float nd = dist / std::max(light.Radius, 0.001f);
                                float nd2 = nd * nd;
                                float window = std::max(0.0f, 1.0f - nd2 * nd2);
                                atten = window * window / std::max(dist * dist, 0.0001f);
                            }
                            float ndotl = n.Dot(L);
                            if (ndotl <= 0.0f)
                                continue;
                            TraceRay shadow = MakeRay(pos + n * 0.002f, L);
                            if (AnyHit(nodes, order, triangles, shadow, reach))
                                continue;
                            Vec3 lc = { light.ColorIntensity[0], light.ColorIntensity[1], light.ColorIntensity[2] };
                            sampleRadiance = sampleRadiance + hit.Color * lc * (ndotl * atten);
                        }
                    }
                }
                radiance = radiance + sampleRadiance;
            }
            radiance = radiance * (1.0f / (float)samplesPerPixel);

            size_t pixel = ((size_t)y * traceW + x) * 4;
            pixels[pixel + 0] = (uint8_t)std::min(255.0f, std::max(0.0f, radiance.x * 255.0f));
            pixels[pixel + 1] = (uint8_t)std::min(255.0f, std::max(0.0f, radiance.y * 255.0f));
            pixels[pixel + 2] = (uint8_t)std::min(255.0f, std::max(0.0f, radiance.z * 255.0f));
            pixels[pixel + 3] = 255;
        }
    }

    RHIDevice* device = GetDevice();
    if (!m_RayTraceBlitVS && device)
    {
        auto api = device->GetApiType();
        const char* source = kBlitHLSL;
        if (api == RHI_API_TYPE::METAL)
            source = kBlitMSL;
        else if (api == RHI_API_TYPE::OPENGL || api == RHI_API_TYPE::VULKAN)
            source = kBlitGLSL;
        m_RayTraceBlitVS = device->CompileShader(EShaderType::Vertex, source, "VSMain", "vs_5_0");
        m_RayTraceBlitPS = device->CompileShader(EShaderType::Pixel, source, "PSMain", "ps_5_0");
        if (m_RayTraceBlitVS && m_RayTraceBlitPS)
        {
            GraphicsPipelineStateInitializer init;
            init.VertexShader = m_RayTraceBlitVS.get();
            init.PixelShader = m_RayTraceBlitPS.get();
            init.DepthEnabled = false;
            init.DepthWrite = false;
            init.RasterizerState = RasterizerStateDesc(ERasterizerFillMode::Solid, ECullMode::None);
            init.RenderTargetsEnabled = 1;
            init.RenderTargetFormats[0] = EFormat::R16G16B16A16_FLOAT;
            m_RayTraceBlitPSO = device->CreateGraphicsPipelineState(init);
        }
    }

    if (device)
    {
        TextureDesc desc;
        desc.Width = traceW;
        desc.Height = traceH;
        desc.Format = EFormat::R8G8B8A8_UNORM;
        desc.BindFlags = TEXTURE_BIND_SHADER_RESOURCE;
        desc.Usage = EResourceUsage::Default;
        desc.MipLevels = 1;
        desc.SampleCount = 1;
        desc.DebugName = "RayTraceColor";
        m_RayTraceColor = device->CreateTexture(desc, pixels.data());
        if (m_RayTraceColor)
            m_RayTraceColorSRV = device->CreateTextureView(m_RayTraceColor.get(), EDescriptorHeapType::CBV_SRV_UAV);
    }

    if (m_RayTraceBlitPSO && m_RayTraceColorSRV)
    {
        ctx->SetPipelineState(m_RayTraceBlitPSO.get());
        ctx->SetVertexShader(m_RayTraceBlitVS.get());
        ctx->SetPixelShader(m_RayTraceBlitPS.get());
        ctx->SetInputLayout(nullptr);
        ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);
        ctx->SetShaderResourceView(0, m_RayTraceColorSRV.get());
        ctx->SetSampler(0, m_PostProcessSampler.get());
        ctx->Draw(3, 0);
        ctx->SetShaderResourceView(0, nullptr);
    }

    ctx->SetRenderTargets(&sceneRTV, 1, GetDSV());
    ClearDepthStencilValue depthClear = { 1.0f, 0 };
    ctx->ClearDepthStencilView(GetDSV(), depthClear, 0x01);
    ctx->SetConstantBuffer(0, m_ViewUB.get());
    ctx->SetCullMode(ECullMode::Back);
    ctx->SetInputLayout(m_InputLayout.get());
    DrawGizmo(ctx);
    ctx->ClearCullModeOverride();

    m_PassTimer.End();
    ctx->EndEvent();
}
