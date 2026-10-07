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

struct VSInput
{
    float3 Pos : ATTRIB0;
    float3 Normal : ATTRIB1;
    float2 UV : ATTRIB2;
    float3 InstancePos : ATTRIB3;
    float3 InstanceScale : ATTRIB4;
    float3 InstanceColor : ATTRIB5;
    float4 InstanceQuat : ATTRIB6;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV : TEXCOORD2;
    float3 Color : TEXCOORD3;
};

float3 quatRotate(float4 q, float3 v)
{
    float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

void main(in VSInput input, out PSInput output)
{
    float3 local = input.Pos * input.InstanceScale;
    float3 world = quatRotate(input.InstanceQuat, local) + input.InstancePos;
    // Camera-relative float precision.
    float3 relative = world - CameraPos;
    output.Pos = mul(float4(relative, 1.0), ViewProj);
    output.WorldPos = world;
    output.Normal = normalize(quatRotate(input.InstanceQuat, input.Normal));
    output.UV = input.UV;
    output.Color = input.InstanceColor;
}
