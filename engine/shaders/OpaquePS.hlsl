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

struct LightData
{
    float3 Position;
    float Intensity;
    float3 Color;
    float Falloff;
    float3 Direction;
    float Radius;
    float SpotOuter;
    float SpotInner;
    uint Flags;
    uint Pad;
};

StructuredBuffer<LightData> Lights : register(t0);
StructuredBuffer<uint> TileOffsets : register(t1);
StructuredBuffer<uint> TileCounts : register(t2);
StructuredBuffer<uint> TileIndices : register(t3);
Texture2D AlbedoTex : register(t4);
SamplerState AlbedoTex_sampler : register(s4);

struct PSInput
{
    float4 Pos : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV : TEXCOORD2;
    float3 Color : TEXCOORD3;
};

float4 main(PSInput input) : SV_Target
{
    float3 N = normalize(input.Normal);
    float3 albedo = input.Color;
    float4 tex = AlbedoTex.Sample(AlbedoTex_sampler, input.UV);
    if (tex.a > 0.01)
        albedo *= tex.rgb;

    // Cluster lookup from screen position / depth.
    uint2 screen = uint2(input.Pos.xy);
    uint tileX = min(screen.x * ClusterSizeX / max(uint(ViewProj[0][0] != 0) * 1 + 1279, 1), ClusterSizeX - 1);
    // Use a stable fallback when resolution uniforms are not bound: full-screen tile 0.
    uint tileIndex = 0;
    if (ClusterSizeX > 0 && ClusterSizeY > 0 && ClusterSizeZ > 0)
    {
        tileX = min(uint(input.Pos.x) * ClusterSizeX / 1280u, ClusterSizeX - 1);
        uint tileY = min(uint(input.Pos.y) * ClusterSizeY / 720u, ClusterSizeY - 1);
        float depth = saturate(input.Pos.z);
        uint tileZ = min(uint(depth * ClusterSizeZ), ClusterSizeZ - 1);
        tileIndex = (tileZ * ClusterSizeY + tileY) * ClusterSizeX + tileX;
    }

    float3 lit = albedo * Ambient;
    uint count = TileCounts[tileIndex];
    uint offset = TileOffsets[tileIndex];
    if (LightCount > 0 && count == 0)
    {
        // Fallback: first N lights if cluster empty (safe for sun-only scenes).
        count = min(LightCount, 8u);
        offset = 0;
        for (uint i = 0; i < count; ++i)
        {
            LightData L = Lights[i];
            float3 Ldir;
            float atten = 1.0;
            if ((L.Flags & 1u) != 0)
            {
                Ldir = normalize(-L.Direction);
            }
            else
            {
                float3 toL = L.Position - input.WorldPos;
                float dist = length(toL);
                Ldir = toL / max(dist, 1e-4);
                atten = L.Intensity / (1.0 + L.Falloff * dist * dist);
            }
            float ndotl = saturate(dot(N, Ldir));
            lit += albedo * L.Color * (ndotl * atten);
        }
    }
    else
    {
        for (uint i = 0; i < count; ++i)
        {
            LightData L = Lights[TileIndices[offset + i]];
            float3 Ldir;
            float atten = L.Intensity;
            if ((L.Flags & 1u) != 0)
            {
                Ldir = normalize(-L.Direction);
            }
            else
            {
                float3 toL = L.Position - input.WorldPos;
                float dist = length(toL);
                Ldir = toL / max(dist, 1e-4);
                atten = L.Intensity / (1.0 + L.Falloff * dist * dist);
            }
            float ndotl = saturate(dot(N, Ldir));
            lit += albedo * L.Color * (ndotl * atten);
        }
    }

    return float4(lit, 1.0);
}
