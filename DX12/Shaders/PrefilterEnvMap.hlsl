static const float PI = 3.14159265359f;
static const uint SAMPLE_COUNT = 1024u;

cbuffer cbPrefilter : register(b3)
{
    float gRoughness;
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

float RadicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

float2 Hammersley(uint i, uint sampleCount)
{
    return float2(float(i) / float(sampleCount), RadicalInverseVdC(i));
}

float3 ImportanceSampleGGX(float2 xi, float roughness, float3 n)
{
    float a = roughness * roughness;

    float phi = 2.0f * PI * xi.x;
    float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));

    float3 h;
    h.x = cos(phi) * sinTheta;
    h.y = sin(phi) * sinTheta;
    h.z = cosTheta;

    float3 up = abs(n.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, n));
    float3 bitangent = cross(n, tangent);
    return normalize(tangent * h.x + bitangent * h.y + n * h.z);
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

float3 PrefilterEnvironment(float3 reflectionDir, float roughness)
{
    float3 result = gEnvMap.SampleLevel(samLinearWarp, DirectionToEquirectUV(reflectionDir), 0.0f).rgb;

    if (roughness > 1e-4f)
    {
        const float3 n = normalize(reflectionDir);
        const float3 v = n;
        float3 prefilteredColor = float3(0.0f, 0.0f, 0.0f);
        float totalWeight = 0.0f;

        [loop]
        for (uint i = 0u; i < SAMPLE_COUNT; ++i)
        {
            float2 xi = Hammersley(i, SAMPLE_COUNT);
            float3 h = ImportanceSampleGGX(xi, roughness, n);
            float3 l = normalize(2.0f * dot(v, h) * h - v);

            float nDotL = saturate(dot(n, l));
            if (nDotL > 0.0f)
            {
                prefilteredColor += gEnvMap.SampleLevel(samLinearWarp, DirectionToEquirectUV(l), 0.0f).rgb * nDotL;
                totalWeight += nDotL;
            }
        }

        result = prefilteredColor / max(totalWeight, 1e-5f);
    }

    return result;
}

float4 PS(VertexOut i) : SV_TARGET
{
    uint faceIndex = (uint)round(gFaceIndex);
    float3 reflectionDir = CubeFaceUvToDirection(i.TexC, faceIndex);
    return float4(PrefilterEnvironment(reflectionDir, saturate(gRoughness)), 1.0f);
}
