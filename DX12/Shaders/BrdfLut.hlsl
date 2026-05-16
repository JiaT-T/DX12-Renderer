static const float PI = 3.14159265359f;
static const uint SAMPLE_COUNT = 1024u;

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

float3 ImportanceSampleGGX(float2 xi, float roughness)
{
    float a = roughness * roughness;

    float phi = 2.0f * PI * xi.x;
    float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));

    float3 h;
    h.x = cos(phi) * sinTheta;
    h.y = sin(phi) * sinTheta;
    h.z = cosTheta;
    return normalize(h);
}

float GeometrySchlickGGX(float nDotV, float roughness)
{
    float a = roughness;
    float k = (a * a) * 0.5f;
    return nDotV / max(nDotV * (1.0f - k) + k, 1e-5f);
}

float GeometrySmith(float3 n, float3 v, float3 l, float roughness)
{
    float nDotV = saturate(dot(n, v));
    float nDotL = saturate(dot(n, l));
    return GeometrySchlickGGX(nDotV, roughness) * GeometrySchlickGGX(nDotL, roughness);
}

float2 IntegrateBRDF(float nDotV, float roughness)
{
    float3 v;
    v.x = sqrt(saturate(1.0f - nDotV * nDotV));
    v.y = 0.0f;
    v.z = nDotV;

    float a = 0.0f;
    float b = 0.0f;
    const float3 n = float3(0.0f, 0.0f, 1.0f);

    [loop]
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 xi = Hammersley(i, SAMPLE_COUNT);
        float3 h = ImportanceSampleGGX(xi, roughness);
        float3 l = normalize(2.0f * dot(v, h) * h - v);

        float nDotL = saturate(l.z);
        float nDotH = saturate(h.z);
        float vDotH = saturate(dot(v, h));

        if (nDotL > 0.0f)
        {
            float g = GeometrySmith(n, v, l, roughness);
            float gVis = (g * vDotH) / max(nDotH * nDotV, 1e-5f);
            float fc = pow(1.0f - vDotH, 5.0f);

            a += (1.0f - fc) * gVis;
            b += fc * gVis;
        }
    }

    return float2(a, b) / float(SAMPLE_COUNT);
}

float4 PS(VertexOut i) : SV_TARGET
{
    float nDotV = saturate(i.TexC.x);
    float roughness = saturate(i.TexC.y);
    return float4(IntegrateBRDF(nDotV, roughness), 0.0f, 1.0f);
}
