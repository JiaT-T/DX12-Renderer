cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldInvTrans;
};

cbuffer cbPass : register(b2)
{
    float4x4 gViewProj;
};

struct VertexIn
{
    float3 PosLS : POSITION;
    float3 Normal : NORMAL;
    float4 TangentU : TANGENT;
    float2 TexC : TEXCOORD;
};

Texture2D gEnvMap : register(t4);
SamplerState samLinearWarp : register(s2);

struct VertexOut
{
    float4 PosHS : SV_POSITION;
    float3 DirWS : TEXCOORD0;
};

float2 DirectionToEquirectUV(float3 dir)
{
    dir = normalize(dir);

    const float invTwoPi = 0.15915494309f;
    const float invPi = 0.31830988618f;

    float u = atan2(dir.z, dir.x) * invTwoPi + 0.5f;
    float v = acos(clamp(dir.y, -1.0f, 1.0f)) * invPi;
    return float2(frac(u), saturate(v));
}

float3 ToneMapACES(float3 color)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

VertexOut VS(VertexIn v)
{
    VertexOut o;

    float3 dir = normalize(v.PosLS);
    o.PosHS = mul(float4(dir, 0.0f), gViewProj);
    o.PosHS.z = o.PosHS.w;
    o.DirWS = dir;

    return o;
}

float4 PS(VertexOut i) : SV_TARGET
{
    float3 color = gEnvMap.SampleLevel(samLinearWarp, DirectionToEquirectUV(i.DirWS), 0.0f).rgb;
    color = ToneMapACES(color);
    color = pow(color, 1.0f / 2.2f);
    return float4(color, 1.0f);
}
