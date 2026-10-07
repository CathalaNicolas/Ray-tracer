#include "GfxView.hpp"

#include "GfxCluster.hpp"
#include "GfxDevice.hpp"
#include "GfxQuality.hpp"
#include "EngineSettings.hpp"
#include "Mesh.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

#if defined(RAYTRACER_DILIGENT)

#include "RefCntAutoPtr.hpp"
#include "DeviceContext.h"
#include "PipelineState.h"
#include "RenderDevice.h"
#include "Shader.h"
#include "ShaderResourceBinding.h"
#include "Buffer.h"
#include "Texture.h"
#include "SwapChain.h"
#include "MapHelper.hpp"

using namespace Diligent;

namespace
{

struct Vertex
{
    float pos[3];
    float nrm[3];
    float uv[2];
};

struct Instance
{
    float pos[3];
    float scale[3];
    float color[3];
    float quat[4];
};

struct FrameCB
{
    float viewProj[16];
    float cameraPos[3];
    float pad0;
    float ambient[3];
    float pad1;
    std::uint32_t lightCount;
    std::uint32_t clusterSizeX;
    std::uint32_t clusterSizeY;
    std::uint32_t clusterSizeZ;
};

void mulMat4(float out[16], const float a[16], const float b[16])
{
    float r[16];
    for (int c = 0; c < 4; ++c)
    {
        for (int row = 0; row < 4; ++row)
        {
            r[c * 4 + row] = a[0 * 4 + row] * b[c * 4 + 0] + a[1 * 4 + row] * b[c * 4 + 1]
                + a[2 * 4 + row] * b[c * 4 + 2] + a[3 * 4 + row] * b[c * 4 + 3];
        }
    }
    std::memcpy(out, r, sizeof(r));
}

void perspectiveRH(float out[16], float fovyRad, float aspect, float znear, float zfar)
{
    const float f = 1.f / std::tan(fovyRad * 0.5f);
    std::memset(out, 0, sizeof(float) * 16);
    out[0] = f / aspect;
    out[5] = f;
    out[10] = zfar / (znear - zfar);
    out[11] = -1.f;
    out[14] = (zfar * znear) / (znear - zfar);
}

void lookAtRH(float out[16], const float eye[3], const float center[3], const float up[3])
{
    float f[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
    float len = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    f[0] /= len;
    f[1] /= len;
    f[2] /= len;
    float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    len = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    s[0] /= len;
    s[1] /= len;
    s[2] /= len;
    float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    std::memset(out, 0, sizeof(float) * 16);
    out[0] = s[0];
    out[4] = s[1];
    out[8] = s[2];
    out[1] = u[0];
    out[5] = u[1];
    out[9] = u[2];
    out[2] = -f[0];
    out[6] = -f[1];
    out[10] = -f[2];
    out[15] = 1.f;
    out[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    out[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    out[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
}

std::string loadShaderFile(const char *name)
{
    const std::string path = std::string(RAYTRACER_SHADER_DIR) + "/" + name;
    std::ifstream in(path);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const char *kFallbackVS = R"(
cbuffer FrameCB : register(b0)
{
    float4x4 ViewProj;
    float3 CameraPos;
    float Pad0;
    float3 Ambient;
    float Pad1;
    uint LightCount;
    uint ClusterSizeX;
    uint ClusterSizeY;
    uint ClusterSizeZ;
};
struct VSInput {
    float3 Pos : ATTRIB0;
    float3 Normal : ATTRIB1;
    float2 UV : ATTRIB2;
    float3 InstancePos : ATTRIB3;
    float3 InstanceScale : ATTRIB4;
    float3 InstanceColor : ATTRIB5;
    float4 InstanceQuat : ATTRIB6;
};
struct PSInput {
    float4 Pos : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV : TEXCOORD2;
    float3 Color : TEXCOORD3;
};
float3 quatRotate(float4 q, float3 v) {
    float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}
void main(in VSInput input, out PSInput output) {
    float3 local = input.Pos * input.InstanceScale;
    float3 world = quatRotate(input.InstanceQuat, local) + input.InstancePos;
    float3 relative = world - CameraPos;
    output.Pos = mul(float4(relative, 1.0), ViewProj);
    output.WorldPos = world;
    output.Normal = normalize(quatRotate(input.InstanceQuat, input.Normal));
    output.UV = input.UV;
    output.Color = input.InstanceColor;
}
)";

const char *kFallbackPS = R"(
cbuffer FrameCB : register(b0)
{
    float4x4 ViewProj;
    float3 CameraPos;
    float Pad0;
    float3 Ambient;
    float Pad1;
    uint LightCount;
    uint ClusterSizeX;
    uint ClusterSizeY;
    uint ClusterSizeZ;
};
struct LightData {
    float3 Position; float Intensity;
    float3 Color; float Falloff;
    float3 Direction; float Radius;
    float SpotOuter; float SpotInner; uint Flags; uint Pad;
};
StructuredBuffer<LightData> Lights : register(t0);
StructuredBuffer<uint> TileOffsets : register(t1);
StructuredBuffer<uint> TileCounts : register(t2);
StructuredBuffer<uint> TileIndices : register(t3);
struct PSInput {
    float4 Pos : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV : TEXCOORD2;
    float3 Color : TEXCOORD3;
};
float4 main(PSInput input) : SV_Target {
    float3 N = normalize(input.Normal);
    float3 albedo = input.Color;
    float3 lit = albedo * Ambient;
    uint count = min(LightCount, 16u);
    for (uint i = 0; i < count; ++i) {
        LightData L = Lights[i];
        float3 Ldir; float atten = L.Intensity;
        if ((L.Flags & 1u) != 0) Ldir = normalize(-L.Direction);
        else {
            float3 toL = L.Position - input.WorldPos;
            float dist = length(toL);
            Ldir = toL / max(dist, 1e-4);
            atten = L.Intensity / (1.0 + L.Falloff * dist * dist);
        }
        lit += albedo * L.Color * (saturate(dot(N, Ldir)) * atten);
    }
    return float4(lit, 1.0);
}
)";

} // namespace

struct GfxView::Impl
{
    IRenderDevice *device = nullptr;
    IDeviceContext *context = nullptr;
    ISwapChain *swapChain = nullptr;
    RefCntAutoPtr<IPipelineState> pso;
    RefCntAutoPtr<IShaderResourceBinding> srb;
    RefCntAutoPtr<IBuffer> frameCb;
    RefCntAutoPtr<IBuffer> vertexBuffer;
    RefCntAutoPtr<IBuffer> indexBuffer;
    RefCntAutoPtr<IBuffer> instanceBuffer;
    RefCntAutoPtr<IBuffer> lightBuffer;
    RefCntAutoPtr<IBuffer> tileOffsetBuffer;
    RefCntAutoPtr<IBuffer> tileCountBuffer;
    RefCntAutoPtr<IBuffer> tileIndexBuffer;
    RefCntAutoPtr<ITexture> albedoTex;
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 0;
    GfxClusterGrid clusters;
    GfxQuality quality;
};

GfxView::GfxView() = default;
GfxView::~GfxView()
{
    shutdown();
}

bool GfxView::init(GfxDevice &device, std::string &error)
{
    shutdown();
    if (!device.ready())
    {
        error = "GfxDevice not ready";
        return false;
    }
    impl_ = std::make_unique<Impl>();
    impl_->device = static_cast<IRenderDevice *>(device.nativeDevice());
    impl_->context = static_cast<IDeviceContext *>(device.nativeContext());
    impl_->swapChain = static_cast<ISwapChain *>(device.nativeSwapChain());
    impl_->quality = GfxQuality::fromSettings(engineSettings());

    std::string vsSource = loadShaderFile("OpaqueVS.hlsl");
    std::string psSource = loadShaderFile("OpaquePS.hlsl");
    if (vsSource.empty())
        vsSource = kFallbackVS;
    if (psSource.empty())
        psSource = kFallbackPS;

    ShaderCreateInfo sci;
    sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    sci.Desc.UseCombinedTextureSamplers = true;

    RefCntAutoPtr<IShader> vs;
    {
        sci.Desc.ShaderType = SHADER_TYPE_VERTEX;
        sci.Desc.Name = "OpaqueVS";
        sci.EntryPoint = "main";
        sci.Source = vsSource.c_str();
        impl_->device->CreateShader(sci, &vs);
    }
    RefCntAutoPtr<IShader> ps;
    {
        sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
        sci.Desc.Name = "OpaquePS";
        sci.EntryPoint = "main";
        sci.Source = psSource.c_str();
        impl_->device->CreateShader(sci, &ps);
    }
    if (!vs || !ps)
    {
        error = "failed to create opaque shaders";
        shutdown();
        return false;
    }

    LayoutElement layout[] = {
        {0, 0, 3, VT_FLOAT32, False},
        {1, 0, 3, VT_FLOAT32, False},
        {2, 0, 2, VT_FLOAT32, False},
        {3, 1, 3, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        {4, 1, 3, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        {5, 1, 3, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        {6, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
    };

    GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name = "OpaquePSO";
    psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    psoCI.GraphicsPipeline.NumRenderTargets = 1;
    psoCI.GraphicsPipeline.RTVFormats[0] = impl_->swapChain->GetDesc().ColorBufferFormat;
    psoCI.GraphicsPipeline.DSVFormat = impl_->swapChain->GetDesc().DepthBufferFormat;
    psoCI.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    psoCI.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
    psoCI.pVS = vs;
    psoCI.pPS = ps;
    psoCI.GraphicsPipeline.InputLayout.LayoutElements = layout;
    psoCI.GraphicsPipeline.InputLayout.NumElements = static_cast<Uint32>(sizeof(layout) / sizeof(layout[0]));

    ShaderResourceVariableDesc vars[] = {
        {SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "FrameCB", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
        {SHADER_TYPE_PIXEL, "Lights", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL, "TileOffsets", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL, "TileCounts", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL, "TileIndices", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL, "AlbedoTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
    };
    psoCI.PSODesc.ResourceLayout.Variables = vars;
    psoCI.PSODesc.ResourceLayout.NumVariables = static_cast<Uint32>(sizeof(vars) / sizeof(vars[0]));

    SamplerDesc sampLinear;
    sampLinear.MinFilter = FILTER_TYPE_LINEAR;
    sampLinear.MagFilter = FILTER_TYPE_LINEAR;
    sampLinear.MipFilter = FILTER_TYPE_LINEAR;
    sampLinear.AddressU = TEXTURE_ADDRESS_WRAP;
    sampLinear.AddressV = TEXTURE_ADDRESS_WRAP;
    sampLinear.AddressW = TEXTURE_ADDRESS_WRAP;
    ImmutableSamplerDesc immutSamplers[] = {
        {SHADER_TYPE_PIXEL, "AlbedoTex", sampLinear},
    };
    psoCI.PSODesc.ResourceLayout.ImmutableSamplers = immutSamplers;
    psoCI.PSODesc.ResourceLayout.NumImmutableSamplers =
        static_cast<Uint32>(sizeof(immutSamplers) / sizeof(immutSamplers[0]));

    impl_->device->CreateGraphicsPipelineState(psoCI, &impl_->pso);
    if (!impl_->pso)
    {
        error = "failed to create opaque PSO";
        shutdown();
        return false;
    }

    BufferDesc cbDesc;
    cbDesc.Name = "FrameCB";
    cbDesc.Size = sizeof(FrameCB);
    cbDesc.Usage = USAGE_DYNAMIC;
    cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
    cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
    impl_->device->CreateBuffer(cbDesc, nullptr, &impl_->frameCb);
    if (auto *v = impl_->pso->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FrameCB"))
        v->Set(impl_->frameCb);
    if (auto *p = impl_->pso->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FrameCB"))
        p->Set(impl_->frameCb);

    // 1x1 white albedo until cooked BC textures are bound per material.
    {
        TextureDesc td;
        td.Name = "AlbedoWhite";
        td.Type = RESOURCE_DIM_TEX_2D;
        td.Width = 1;
        td.Height = 1;
        td.Format = TEX_FORMAT_RGBA8_UNORM;
        td.BindFlags = BIND_SHADER_RESOURCE;
        td.Usage = USAGE_IMMUTABLE;
        const std::uint8_t white[4] = {255, 255, 255, 255};
        TextureSubResData sub;
        sub.pData = white;
        sub.Stride = 4;
        TextureData data;
        data.pSubResources = &sub;
        data.NumSubresources = 1;
        impl_->device->CreateTexture(td, &data, &impl_->albedoTex);
    }

    // InitStaticResources=true copies static CB bindings into the SRB.
    impl_->pso->CreateShaderResourceBinding(&impl_->srb, true);
    if (impl_->srb && impl_->albedoTex)
    {
        if (auto *var = impl_->srb->GetVariableByName(SHADER_TYPE_PIXEL, "AlbedoTex"))
            var->Set(impl_->albedoTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    }

    return true;
}

void GfxView::shutdown()
{
    impl_.reset();
    meshInstances_ = 0;
    uniqueGeometries_ = 0;
    lightCount_ = 0;
}

void GfxView::upload(const Scene &scene, const Camera &camera)
{
    if (impl_ == nullptr || impl_->device == nullptr)
        return;

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Instance> instances;
    std::unordered_map<const MeshGeometry *, std::pair<std::uint32_t, std::uint32_t>> geomRanges;

    for (const auto &object : scene.objects())
    {
        const Mesh *mesh = dynamic_cast<const Mesh *>(object.get());
        if (mesh == nullptr)
            continue;
        const MeshGeometry *geometry = mesh->geometry().get();
        if (geometry == nullptr || geometry->triangles.empty())
            continue;

        auto found = geomRanges.find(geometry);
        if (found == geomRanges.end())
        {
            const std::uint32_t baseVertex = static_cast<std::uint32_t>(vertices.size());
            const std::uint32_t indexStart = static_cast<std::uint32_t>(indices.size());
            for (const MeshTri &tri : geometry->triangles)
            {
                for (int c = 0; c < 3; ++c)
                {
                    Vertex v{};
                    v.pos[0] = static_cast<float>(tri.position[c].x);
                    v.pos[1] = static_cast<float>(tri.position[c].y);
                    v.pos[2] = static_cast<float>(tri.position[c].z);
                    v.nrm[0] = static_cast<float>(tri.normal[c].x);
                    v.nrm[1] = static_cast<float>(tri.normal[c].y);
                    v.nrm[2] = static_cast<float>(tri.normal[c].z);
                    v.uv[0] = tri.u[c];
                    v.uv[1] = tri.v[c];
                    indices.push_back(static_cast<std::uint32_t>(vertices.size()));
                    vertices.push_back(v);
                }
            }
            geomRanges.emplace(geometry,
                std::make_pair(indexStart, static_cast<std::uint32_t>(indices.size() - indexStart)));
            (void)baseVertex;
        }

        Instance inst{};
        const Vec3 p = mesh->position();
        inst.pos[0] = static_cast<float>(p.x);
        inst.pos[1] = static_cast<float>(p.y);
        inst.pos[2] = static_cast<float>(p.z);
        const double s = mesh->scale();
        inst.scale[0] = inst.scale[1] = inst.scale[2] = static_cast<float>(s);
        const Vec3 albedo = mesh->material().albedo;
        inst.color[0] = static_cast<float>(albedo.x);
        inst.color[1] = static_cast<float>(albedo.y);
        inst.color[2] = static_cast<float>(albedo.z);
        // Identity quat for now (Euler bake can land later).
        inst.quat[0] = 0;
        inst.quat[1] = 0;
        inst.quat[2] = 0;
        inst.quat[3] = 1;
        instances.push_back(inst);
    }

    uniqueGeometries_ = static_cast<int>(geomRanges.size());
    meshInstances_ = static_cast<int>(instances.size());
    impl_->instanceCount = static_cast<std::uint32_t>(instances.size());
    impl_->indexCount = static_cast<std::uint32_t>(indices.size());

    auto recreateBuffer = [&](RefCntAutoPtr<IBuffer> &buf, const void *data, std::uint64_t size, BIND_FLAGS bind,
                              const char *name, BUFFER_MODE mode = BUFFER_MODE_UNDEFINED, std::uint32_t stride = 0) {
        buf.Release();
        if (size == 0)
            return;
        BufferDesc desc;
        desc.Name = name;
        desc.Size = size;
        desc.Usage = USAGE_DEFAULT;
        desc.BindFlags = bind;
        desc.Mode = mode;
        desc.ElementByteStride = stride;
        BufferData bd;
        bd.pData = data;
        bd.DataSize = size;
        impl_->device->CreateBuffer(desc, &bd, &buf);
    };

    recreateBuffer(impl_->vertexBuffer, vertices.data(), vertices.size() * sizeof(Vertex), BIND_VERTEX_BUFFER, "MeshVB");
    recreateBuffer(impl_->indexBuffer, indices.data(), indices.size() * sizeof(std::uint32_t), BIND_INDEX_BUFFER, "MeshIB");
    recreateBuffer(impl_->instanceBuffer, instances.data(), instances.size() * sizeof(Instance), BIND_VERTEX_BUFFER,
        "MeshInstanceVB");

    gfxBuildLightClusters(camera, 1280, 720, scene.lights(), impl_->clusters);
    lightCount_ = static_cast<int>(impl_->clusters.lights.size());
    // Structured buffers for clustered forward lighting.
    std::vector<GfxClusterLight> lightScratch = impl_->clusters.lights;
    if (lightScratch.empty())
        lightScratch.push_back({});
    std::vector<std::uint32_t> indexScratch = impl_->clusters.indices;
    if (indexScratch.empty())
        indexScratch.push_back(0);
    recreateBuffer(impl_->lightBuffer, lightScratch.data(), lightScratch.size() * sizeof(GfxClusterLight),
        BIND_SHADER_RESOURCE, "Lights", BUFFER_MODE_STRUCTURED, sizeof(GfxClusterLight));
    recreateBuffer(impl_->tileOffsetBuffer, impl_->clusters.tileOffsets.data(),
        impl_->clusters.tileOffsets.size() * sizeof(std::uint32_t), BIND_SHADER_RESOURCE, "TileOffsets",
        BUFFER_MODE_STRUCTURED, sizeof(std::uint32_t));
    recreateBuffer(impl_->tileCountBuffer, impl_->clusters.tileCounts.data(),
        impl_->clusters.tileCounts.size() * sizeof(std::uint32_t), BIND_SHADER_RESOURCE, "TileCounts",
        BUFFER_MODE_STRUCTURED, sizeof(std::uint32_t));
    recreateBuffer(impl_->tileIndexBuffer, indexScratch.data(), indexScratch.size() * sizeof(std::uint32_t),
        BIND_SHADER_RESOURCE, "TileIndices", BUFFER_MODE_STRUCTURED, sizeof(std::uint32_t));

    if (impl_->srb)
    {
        auto bindSrv = [&](const char *name, IBuffer *buffer) {
            if (buffer == nullptr)
                return;
            if (auto *var = impl_->srb->GetVariableByName(SHADER_TYPE_PIXEL, name))
                var->Set(buffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        };
        bindSrv("Lights", impl_->lightBuffer);
        bindSrv("TileOffsets", impl_->tileOffsetBuffer);
        bindSrv("TileCounts", impl_->tileCountBuffer);
        bindSrv("TileIndices", impl_->tileIndexBuffer);
        if (impl_->albedoTex)
        {
            if (auto *var = impl_->srb->GetVariableByName(SHADER_TYPE_PIXEL, "AlbedoTex"))
                var->Set(impl_->albedoTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        }
    }
    (void)camera;
}

void GfxView::draw(const Scene &scene, const Camera &camera)
{
    if (impl_ == nullptr || impl_->pso == nullptr || impl_->instanceCount == 0 || impl_->indexCount == 0)
        return;

    const Vec3 eye = camera.origin();
    const Vec3 forward = camera.viewDirection();
    const Vec3 center = eye + forward;
    const Vec3 up = camera.upAxis();
    float eyeA[3] = {static_cast<float>(eye.x), static_cast<float>(eye.y), static_cast<float>(eye.z)};
    float centerA[3] = {static_cast<float>(center.x), static_cast<float>(center.y), static_cast<float>(center.z)};
    float upA[3] = {static_cast<float>(up.x), static_cast<float>(up.y), static_cast<float>(up.z)};
    // Camera-relative view: eye at origin.
    float eyeZero[3] = {0, 0, 0};
    float centerRel[3] = {centerA[0] - eyeA[0], centerA[1] - eyeA[1], centerA[2] - eyeA[2]};
    float view[16];
    float proj[16];
    float viewProj[16];
    lookAtRH(view, eyeZero, centerRel, upA);
    perspectiveRH(proj, 60.f * 3.14159265f / 180.f, 16.f / 9.f, 0.05f, impl_->quality.viewDistance);
    mulMat4(viewProj, proj, view);

    {
        MapHelper<FrameCB> cb(impl_->context, impl_->frameCb, MAP_WRITE, MAP_FLAG_DISCARD);
        std::memcpy(cb->viewProj, viewProj, sizeof(viewProj));
        cb->cameraPos[0] = eyeA[0];
        cb->cameraPos[1] = eyeA[1];
        cb->cameraPos[2] = eyeA[2];
        cb->ambient[0] = 0.08f;
        cb->ambient[1] = 0.08f;
        cb->ambient[2] = 0.10f;
        cb->lightCount = static_cast<std::uint32_t>(impl_->clusters.lights.size());
        cb->clusterSizeX = GfxClusterGrid::kSizeX;
        cb->clusterSizeY = GfxClusterGrid::kSizeY;
        cb->clusterSizeZ = GfxClusterGrid::kSizeZ;
    }

    IBuffer *vbs[] = {impl_->vertexBuffer, impl_->instanceBuffer};
    Uint64 offsets[] = {0, 0};
    impl_->context->SetVertexBuffers(0, 2, vbs, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
        SET_VERTEX_BUFFERS_FLAG_RESET);
    impl_->context->SetIndexBuffer(impl_->indexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    impl_->context->SetPipelineState(impl_->pso);
    impl_->context->CommitShaderResources(impl_->srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DrawIndexedAttribs draw;
    draw.IndexType = VT_UINT32;
    draw.NumIndices = impl_->indexCount;
    draw.NumInstances = impl_->instanceCount;
    draw.Flags = DRAW_FLAG_VERIFY_ALL;
    impl_->context->DrawIndexed(draw);
    (void)scene;
}

#else

GfxView::GfxView() = default;
GfxView::~GfxView() = default;
bool GfxView::init(GfxDevice &, std::string &error)
{
    error = "Diligent not enabled";
    return false;
}
void GfxView::shutdown() {}
void GfxView::upload(const Scene &, const Camera &) {}
void GfxView::draw(const Scene &, const Camera &) {}

#endif
