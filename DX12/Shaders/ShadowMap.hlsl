cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldInvTrans;
};

cbuffer cbPass : register(b2)
{
    float4x4 gViewProj;
    float4x4 gLightViewProj;
    float4x4 gShadowTransform;
};

struct VertexIn
{
    float3 PosLS : POSITION;
    float3 Normal : NORMAL;
    float4 TangentU : TANGENT;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosHS : SV_POSITION;
};

VertexOut VS(VertexIn v)
{
    VertexOut o;
    float4 posW = mul(float4(v.PosLS, 1.0f), gWorld);
    o.PosHS = mul(posW, gLightViewProj);
    return o;
}
