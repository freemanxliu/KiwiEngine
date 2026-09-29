#include "KiwiEngineApp.h"
#include "Renderer/DeferredShadingRenderer.h"

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

float Sample01(uint32_t N)
{
    N ^= N >> 16;
    N *= 0x7feb352du;
    N ^= N >> 15;
    N *= 0x846ca68bu;
    N ^= N >> 16;
    return (N & 0x00ffffffu) / float(0x01000000u);
}

Vec3 TransformPoint(const Mat4& World, const Vec3& P)
{
    return {
        P.x * World.m[0][0] + P.y * World.m[1][0] + P.z * World.m[2][0] + World.m[3][0],
        P.x * World.m[0][1] + P.y * World.m[1][1] + P.z * World.m[2][1] + World.m[3][1],
        P.x * World.m[0][2] + P.y * World.m[1][2] + P.z * World.m[2][2] + World.m[3][2]
    };
}

Vec3 TransformDirection(const Mat4& M, float X, float Y, float Z, float W, float& OutW)
{
    OutW = X * M.m[0][3] + Y * M.m[1][3] + Z * M.m[2][3] + W * M.m[3][3];
    return {
        X * M.m[0][0] + Y * M.m[1][0] + Z * M.m[2][0] + W * M.m[3][0],
        X * M.m[0][1] + Y * M.m[1][1] + Z * M.m[2][1] + W * M.m[3][1],
        X * M.m[0][2] + Y * M.m[1][2] + Z * M.m[2][2] + W * M.m[3][2]
    };
}

TraceRay MakeRay(const Vec3& Origin, const Vec3& Direction)
{
    TraceRay Ray;
    Ray.Origin = Origin;
    Ray.Direction = Direction;
    const float KHuge = 1.0e30f;
    Ray.InvDirection = {
        std::abs(Direction.x) > 1.0e-8f ? 1.0f / Direction.x : std::copysign(KHuge, Direction.x),
        std::abs(Direction.y) > 1.0e-8f ? 1.0f / Direction.y : std::copysign(KHuge, Direction.y),
        std::abs(Direction.z) > 1.0e-8f ? 1.0f / Direction.z : std::copysign(KHuge, Direction.z)
    };
    return Ray;
}

void Expand(Vec3& Mn, Vec3& Mx, const Vec3& P)
{
    Mn.x = std::min(Mn.x, P.x); Mn.y = std::min(Mn.y, P.y); Mn.z = std::min(Mn.z, P.z);
    Mx.x = std::max(Mx.x, P.x); Mx.y = std::max(Mx.y, P.y); Mx.z = std::max(Mx.z, P.z);
}

bool RayBox(const TraceRay& Ray, const Vec3& Mn, const Vec3& Mx, float TMax)
{
    float T0 = (Mn.x - Ray.Origin.x) * Ray.InvDirection.x;
    float T1 = (Mx.x - Ray.Origin.x) * Ray.InvDirection.x;
    if (T0 > T1) std::swap(T0, T1);
    float Ty0 = (Mn.y - Ray.Origin.y) * Ray.InvDirection.y;
    float Ty1 = (Mx.y - Ray.Origin.y) * Ray.InvDirection.y;
    if (Ty0 > Ty1) std::swap(Ty0, Ty1);
    T0 = std::max(T0, Ty0);
    T1 = std::min(T1, Ty1);
    float Tz0 = (Mn.z - Ray.Origin.z) * Ray.InvDirection.z;
    float Tz1 = (Mx.z - Ray.Origin.z) * Ray.InvDirection.z;
    if (Tz0 > Tz1) std::swap(Tz0, Tz1);
    T0 = std::max(T0, Tz0);
    T1 = std::min(T1, Tz1);
    return T1 >= std::max(T0, 0.0f) && T0 < TMax;
}

bool IntersectTriangle(const Triangle& Tri, const TraceRay& Ray, float TMax, float& THit, Vec3& Normal)
{
    const float Eps = 1.0e-6f;
    Vec3 E1 = Tri.B - Tri.A;
    Vec3 E2 = Tri.C - Tri.A;
    Vec3 P = Ray.Direction.Cross(E2);
    float Det = E1.Dot(P);
    if (std::abs(Det) < Eps)
        return false;
    float InvDet = 1.0f / Det;
    Vec3 S = Ray.Origin - Tri.A;
    float U = S.Dot(P) * InvDet;
    if (U < 0.0f || U > 1.0f)
        return false;
    Vec3 Q = S.Cross(E1);
    float V = Ray.Direction.Dot(Q) * InvDet;
    if (V < 0.0f || U + V > 1.0f)
        return false;
    float T = E2.Dot(Q) * InvDet;
    if (T < 1.0e-3f || T > TMax)
        return false;
    THit = T;
    Normal = Tri.Normal;
    if (Normal.Dot(Ray.Direction) > 0.0f)
        Normal = Normal * -1.0f;
    return true;
}

int BuildBvh(std::vector<BvhNode>& Nodes, std::vector<int>& Order, const std::vector<Triangle>& Tris, int Begin, int End)
{
    int Index = (int)Nodes.size();
    Nodes.push_back({});
    Vec3 Mn{ 1.0e30f, 1.0e30f, 1.0e30f };
    Vec3 Mx{ -1.0e30f, -1.0e30f, -1.0e30f };
    for (int I = Begin; I < End; ++I)
    {
        const Triangle& Tri = Tris[Order[I]];
        Expand(Mn, Mx, Tri.A);
        Expand(Mn, Mx, Tri.B);
        Expand(Mn, Mx, Tri.C);
    }
    Nodes[Index].Min = Mn;
    Nodes[Index].Max = Mx;
    int Count = End - Begin;
    if (Count <= 8)
    {
        Nodes[Index].Start = Begin;
        Nodes[Index].Count = Count;
        return Index;
    }

    Vec3 Extent = Mx - Mn;
    int Axis = 0;
    if (Extent.y > Extent.x && Extent.y >= Extent.z) Axis = 1;
    else if (Extent.z > Extent.x) Axis = 2;
    int Mid = Begin + Count / 2;
    std::nth_element(Order.begin() + Begin, Order.begin() + Mid, Order.begin() + End,
        [&](int A, int B)
        {
            auto Center = [&](int Id)
            {
                const Triangle& Tri = Tris[Id];
                Vec3 C = (Tri.A + Tri.B + Tri.C) * (1.0f / 3.0f);
                return Axis == 0 ? C.x : Axis == 1 ? C.y : C.z;
            };
            return Center(A) < Center(B);
        });
    int Left = BuildBvh(Nodes, Order, Tris, Begin, Mid);
    int Right = BuildBvh(Nodes, Order, Tris, Mid, End);
    Nodes[Index].Left = Left;
    Nodes[Index].Right = Right;
    return Index;
}

struct Hit
{
    float T = 1.0e30f;
    Vec3 Normal;
    Vec3 Color;
};

bool ClosestHit(const std::vector<BvhNode>& Nodes, const std::vector<int>& Order,
    const std::vector<Triangle>& Tris, const TraceRay& Ray, float TMax, Hit& Hit)
{
    if (Nodes.empty())
        return false;
    int Stack[64];
    int Top = 0;
    Stack[Top++] = 0;
    bool Found = false;
    while (Top > 0)
    {
        const BvhNode& Node = Nodes[Stack[--Top]];
        if (!RayBox(Ray, Node.Min, Node.Max, TMax))
            continue;
        if (Node.Left < 0)
        {
            for (int I = 0; I < Node.Count; ++I)
            {
                const Triangle& Tri = Tris[Order[Node.Start + I]];
                float T = 0.0f;
                Vec3 N;
                if (IntersectTriangle(Tri, Ray, TMax, T, N))
                {
                    TMax = T;
                    Hit.T = T;
                    Hit.Normal = N;
                    Hit.Color = Tri.Color;
                    Found = true;
                }
            }
        }
        else if (Top < 62)
        {
            Stack[Top++] = Node.Left;
            Stack[Top++] = Node.Right;
        }
    }
    return Found;
}

bool AnyHit(const std::vector<BvhNode>& Nodes, const std::vector<int>& Order,
    const std::vector<Triangle>& Tris, const TraceRay& Ray, float TMax)
{
    if (Nodes.empty())
        return false;
    int Stack[64];
    int Top = 0;
    Stack[Top++] = 0;
    while (Top > 0)
    {
        const BvhNode& Node = Nodes[Stack[--Top]];
        if (!RayBox(Ray, Node.Min, Node.Max, TMax))
            continue;
        if (Node.Left < 0)
        {
            for (int I = 0; I < Node.Count; ++I)
            {
                float T = 0.0f;
                Vec3 N;
                if (IntersectTriangle(Tris[Order[Node.Start + I]], Ray, TMax, T, N))
                    return true;
            }
        }
        else if (Top < 62)
        {
            Stack[Top++] = Node.Left;
            Stack[Top++] = Node.Right;
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

void Kiwi::DeferredShadingSceneRenderer::RenderRayTracing(RHICommandContext* Ctx, RHITextureView* SceneRTV, const Viewport& Vp, const ScissorRect& Sr)
{

    Ctx->BeginEvent("Ray Tracing");
    App.PassTimer.Begin("Ray Tracing");

    Ctx->SetRenderTargets(&SceneRTV, 1, nullptr);
    Ctx->SetViewports(&Vp, 1);
    Ctx->SetScissorRects(&Sr, 1);
    ClearColorValue ClearColor = { 0.05f, 0.06f, 0.08f, 1.0f };
    Ctx->ClearRenderTargetView(SceneRTV, ClearColor);

    const uint32_t ScreenW = std::max(1u, (uint32_t)Vp.Width);
    const uint32_t ScreenH = std::max(1u, (uint32_t)Vp.Height);
    const float ResolutionScale = std::min(1.0f, std::max(0.01f, App.RenderParams.RayTracingResolutionPercent / 100.0f));
    const int SamplesPerPixel = std::max(1, App.RenderParams.RayTracingSamplesPerPixel);
    const uint32_t TraceW = std::max(1u, (uint32_t)(ScreenW * ResolutionScale));
    const uint32_t TraceH = std::max(1u, (uint32_t)(ScreenH * ResolutionScale));
    App.RayTraceWidth = TraceW;
    App.RayTraceHeight = TraceH;

    std::vector<Triangle> Triangles;
    for (const RenderItem& Item : View.VisibleItems)
    {
        const PrimitiveSceneProxy& Proxy = Item.Primitive->Proxy;
        if (!Proxy.MeshData)
            continue;
        const auto& Verts = Proxy.MeshData->GetVertices();
        const auto& Indices = Proxy.MeshData->GetIndices();
        if (Verts.empty() || Indices.size() < 3)
            continue;
        const Vec4& Color = Proxy.Color;
        const Mat4& World = Proxy.LocalToWorld;
        for (size_t I = 0; I + 2 < Indices.size(); I += 3)
        {
            uint32_t I0 = Indices[I], I1 = Indices[I + 1], I2 = Indices[I + 2];
            if (I0 >= Verts.size() || I1 >= Verts.size() || I2 >= Verts.size())
                continue;
            Triangle Tri;
            Tri.A = TransformPoint(World, Verts[I0].Position);
            Tri.B = TransformPoint(World, Verts[I1].Position);
            Tri.C = TransformPoint(World, Verts[I2].Position);
            Tri.Normal = (Tri.B - Tri.A).Cross(Tri.C - Tri.A);
            if (Tri.Normal.Dot(Tri.Normal) < 1.0e-12f)
                continue;
            Tri.Normal = Tri.Normal.Normalize();
            Tri.Color = { Color.x, Color.y, Color.z };
            Triangles.push_back(Tri);
        }
    }

    std::vector<int> Order(Triangles.size());
    std::vector<BvhNode> Nodes;
    if (!Triangles.empty())
    {
        for (int I = 0; I < (int)Triangles.size(); ++I)
            Order[I] = I;
        Nodes.reserve(Triangles.size() * 2);
        BuildBvh(Nodes, Order, Triangles, 0, (int)Triangles.size());
    }

    const Mat4& InvViewProj = View.Matrices.GetInvViewProjectionMatrix();
    const Vec3& ViewOrigin = View.Matrices.GetViewOrigin();
    const bool Unlit = View.ViewMode == EViewMode::Unlit;
    std::vector<uint8_t> Pixels((size_t)TraceW * TraceH * 4);

    for (uint32_t Y = 0; Y < TraceH; ++Y)
    {
        for (uint32_t X = 0; X < TraceW; ++X)
        {
            Vec3 Radiance = { 0.0f, 0.0f, 0.0f };
            for (int Sample = 0; Sample < SamplesPerPixel; ++Sample)
            {
                float JitterX = (Sample + 0.5f) / (float)SamplesPerPixel;
                float JitterY = Sample01(X * 1973u ^ Y * 9277u ^ (uint32_t)Sample * 26699u);
                float NdcX = (X + JitterX) / (float)TraceW * 2.0f - 1.0f;
                float SampleNdcY = 1.0f - (Y + JitterY) / (float)TraceH * 2.0f;
                float ClipW = 1.0f;
                Vec3 FarPos = TransformDirection(InvViewProj, NdcX, SampleNdcY, 1.0f, 1.0f, ClipW);
                if (std::abs(ClipW) > 1.0e-8f)
                    FarPos = FarPos * (1.0f / ClipW);
                Vec3 Dir = (FarPos - ViewOrigin).Normalize();
                TraceRay PrimaryRay = MakeRay(ViewOrigin, Dir);

                Vec3 SampleRadiance = { 0.05f, 0.06f, 0.08f };
                Hit PrimaryHit;
                if (ClosestHit(Nodes, Order, Triangles, PrimaryRay, 1.0e30f, PrimaryHit))
                {
                    Vec3 Pos = PrimaryRay.Origin + PrimaryRay.Direction * PrimaryHit.T;
                    Vec3 N = PrimaryHit.Normal;
                    if (Unlit)
                    {
                        SampleRadiance = PrimaryHit.Color;
                    }
                    else
                    {
                        SampleRadiance = PrimaryHit.Color * 0.08f;
                        for (int Li = 0; Li < App.RenderScene.GetNumLights(); ++Li)
                        {
                            const GPULightData& Light = App.RenderScene.GetLightData()[Li];
                            Vec3 L;
                            float Reach = 1.0e4f;
                            float Atten = 1.0f;
                            if (Light.Type == 0)
                            {
                                L = Vec3(Light.DirectionOrPos[0], Light.DirectionOrPos[1], Light.DirectionOrPos[2]).Normalize();
                            }
                            else
                            {
                                Vec3 ToLight = Vec3(Light.DirectionOrPos[0], Light.DirectionOrPos[1], Light.DirectionOrPos[2]) - Pos;
                                float Dist = ToLight.Length();
                                if (Dist > Light.Radius)
                                    continue;
                                L = ToLight * (1.0f / std::max(Dist, 0.0001f));
                                Reach = Dist - 0.001f;
                                float Nd = Dist / std::max(Light.Radius, 0.001f);
                                float Nd2 = Nd * Nd;
                                float Window = std::max(0.0f, 1.0f - Nd2 * Nd2);
                                Atten = Window * Window / std::max(Dist * Dist, 0.0001f);
                            }
                            float Ndotl = N.Dot(L);
                            if (Ndotl <= 0.0f)
                                continue;
                            TraceRay Shadow = MakeRay(Pos + N * 0.002f, L);
                            if (AnyHit(Nodes, Order, Triangles, Shadow, Reach))
                                continue;
                            Vec3 Lc = { Light.ColorIntensity[0], Light.ColorIntensity[1], Light.ColorIntensity[2] };
                            SampleRadiance = SampleRadiance + PrimaryHit.Color * Lc * (Ndotl * Atten);
                        }
                    }
                }
                Radiance = Radiance + SampleRadiance;
            }
            Radiance = Radiance * (1.0f / (float)SamplesPerPixel);

            size_t Pixel = ((size_t)Y * TraceW + X) * 4;
            Pixels[Pixel + 0] = (uint8_t)std::min(255.0f, std::max(0.0f, Radiance.x * 255.0f));
            Pixels[Pixel + 1] = (uint8_t)std::min(255.0f, std::max(0.0f, Radiance.y * 255.0f));
            Pixels[Pixel + 2] = (uint8_t)std::min(255.0f, std::max(0.0f, Radiance.z * 255.0f));
            Pixels[Pixel + 3] = 255;
        }
    }

    RHIDevice* Device = App.GetDevice();
    if (!App.RayTraceBlitVS && Device)
    {
        auto Api = Device->GetApiType();
        const char* Source = kBlitHLSL;
        if (Api == RHI_API_TYPE::METAL)
            Source = kBlitMSL;
        else if (Api == RHI_API_TYPE::OPENGL || Api == RHI_API_TYPE::VULKAN)
            Source = kBlitGLSL;
        App.RayTraceBlitVS = Device->CompileShader(EShaderType::Vertex, Source, "VSMain", "vs_5_0");
        App.RayTraceBlitPS = Device->CompileShader(EShaderType::Pixel, Source, "PSMain", "ps_5_0");
        if (App.RayTraceBlitVS && App.RayTraceBlitPS)
        {
            GraphicsPipelineStateInitializer Init;
            Init.VertexShader = App.RayTraceBlitVS.get();
            Init.PixelShader = App.RayTraceBlitPS.get();
            Init.DepthEnabled = false;
            Init.DepthWrite = false;
            Init.RasterizerState = RasterizerStateDesc(ERasterizerFillMode::Solid, ECullMode::None);
            Init.RenderTargetsEnabled = 1;
            Init.RenderTargetFormats[0] = EFormat::R16G16B16A16_FLOAT;
            App.RayTraceBlitPSO = Device->CreateGraphicsPipelineState(Init);
        }
    }

    if (Device)
    {
        TextureDesc Desc;
        Desc.Width = TraceW;
        Desc.Height = TraceH;
        Desc.Format = EFormat::R8G8B8A8_UNORM;
        Desc.BindFlags = TEXTURE_BIND_SHADER_RESOURCE;
        Desc.Usage = EResourceUsage::Default;
        Desc.MipLevels = 1;
        Desc.SampleCount = 1;
        Desc.DebugName = "RayTraceColor";
        // The previous frame's blit may not have been replayed yet.
        RHICommandList* CmdList = App.GetContext();
        CmdList->DeferredRelease(App.RayTraceColorSRV);
        CmdList->DeferredRelease(App.RayTraceColor);
        App.RayTraceColor = Device->CreateTexture(Desc, Pixels.data());
        if (App.RayTraceColor)
            App.RayTraceColorSRV = Device->CreateTextureView(App.RayTraceColor.get(), EDescriptorHeapType::CBV_SRV_UAV);
    }

    if (App.RayTraceBlitPSO && App.RayTraceColorSRV)
    {
        Ctx->SetPipelineState(App.RayTraceBlitPSO.get());
        Ctx->SetVertexShader(App.RayTraceBlitVS.get());
        Ctx->SetPixelShader(App.RayTraceBlitPS.get());
        Ctx->SetInputLayout(nullptr);
        Ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);
        Ctx->SetShaderResourceView(0, App.RayTraceColorSRV.get());
        Ctx->SetSampler(0, App.PostProcessSampler.get());
        Ctx->Draw(3, 0);
        Ctx->SetShaderResourceView(0, nullptr);
    }

    Ctx->SetRenderTargets(&SceneRTV, 1, App.GetDSV());
    ClearDepthStencilValue DepthClear = { 1.0f, 0 };
    Ctx->ClearDepthStencilView(App.GetDSV(), DepthClear, 0x01);
    Ctx->SetConstantBuffer(0, View.ViewUniformBufferRef.GetReference());
    Ctx->SetCullMode(ECullMode::Back);
    Ctx->SetInputLayout(App.InputLayout.get());
    App.DrawGizmo(Ctx);
    Ctx->ClearCullModeOverride();

    App.PassTimer.End();
    Ctx->EndEvent();
}
