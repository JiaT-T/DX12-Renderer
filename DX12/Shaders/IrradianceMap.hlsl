static const float PI = 3.14159265359f;

cbuffer cbIrradiance : register(b3)
{
    float gUnused;
    float gFaceIndex;
};

Texture2D gEnvMap : register(t4);
SamplerState samLinearWarp : register(s2);

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VS(uint vertexId : SV_VertexID)
{
    VertexOut o;
    float2 texC = float2((vertexId << 1) & 2, vertexId & 2);
    o.TexC = texC;
    o.PosH = float4(texC * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

float2 DirectionToEquirectUV(float3 dir)
{
    dir = normalize(dir);

    const float invTwoPi = 0.15915494309f;
    const float invPi = 0.31830988618f;

    float u = atan2(dir.z, dir.x) * invTwoPi + 0.5f;
    float v = acos(clamp(dir.y, -1.0f, 1.0f)) * invPi;
    return float2(frac(u), saturate(v));
}

float3 CubeFaceUvToDirection(float2 texC, uint faceIndex)
{
    float2 uv = texC * 2.0f - 1.0f;

    if (faceIndex == 0u) return normalize(float3(1.0f, -uv.y, -uv.x));
    if (faceIndex == 1u) return normalize(float3(-1.0f, -uv.y, uv.x));
    if (faceIndex == 2u) return normalize(float3(uv.x, 1.0f, uv.y));
    if (faceIndex == 3u) return normalize(float3(uv.x, -1.0f, -uv.y));
    if (faceIndex == 4u) return normalize(float3(uv.x, -uv.y, 1.0f));
    return normalize(float3(-uv.x, -uv.y, -1.0f));
}

float3 IntegrateIrradiance(float3 normal)
{
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);

    float3 up = abs(normal.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 right = normalize(cross(up, normal));
    up = normalize(cross(normal, right));

    const float sampleDelta = 0.025f;
    float sampleCount = 0.0f;

    [loop]
    for (float phi = 0.0f; phi < 2.0f * PI; phi += sampleDelta)
    {
        [loop]
        for (float theta = 0.0f; theta < 0.5f * PI; theta += sampleDelta)
        {
            float3 tangentSample = float3(
                sin(theta) * cos(phi),
                sin(theta) * sin(phi),
                cos(theta));
            float3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal;
            float3 radiance = gEnvMap.SampleLevel(samLinearWarp, DirectionToEquirectUV(sampleVec), 0.0f).rgb;

            irradiance += radiance * cos(theta) * sin(theta);
            sampleCount += 1.0f;
        }
    }

    return PI * irradiance / max(sampleCount, 1.0f);
}

float4 PS(VertexOut i) : SV_TARGET
{
    uint faceIndex = (uint)round(gFaceIndex);
    float3 normal = CubeFaceUvToDirection(i.TexC, faceIndex);
    return float4(IntegrateIrradiance(normal), 1.0f);
}
