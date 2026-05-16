#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 2
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

#include "LightingTools.hlsl"

Texture2D gBaseColorMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gRoughnessMap : register(t2);
Texture2D gMetallicMap : register(t3);
TextureCube gEnvMap : register(t4);

SamplerState samPointWarp : register(s0);
SamplerState samPointClamp : register(s1);
SamplerState samLinearWarp : register(s2);
SamplerState samLinearClamp : register(s3);
SamplerState samAnisotropicWarp : register(s4);
SamplerState samAnisotropicClamp : register(s5);
SamplerState samAnisotropicMirror : register(s6);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldInvTrans;
};

cbuffer cbMaterial : register(b1)
{
    float4 gBaseColorFactor;
    float3 gFresnelR0;
    float gRoughnessFactor;
    float gMetallicFactor;
    float gNormalScale;
    float gNormalMapFlipY;
    float gAlphaCutoff;
};

cbuffer cbPass : register(b2)
{
    float4x4 gViewProj;
    float4x4 gLightViewProj;
    float4x4 gShadowTransform;
    float3 gCameraPosW;
    float gTotalTime;
    float4 gAmbientLight;
    Light gLights[MAX_LIGHTS];
    float gEnvMapMipCount;
    float gIblStrength;
    float2 gPassPadding;
};

struct VertexIn
{
    float3 VertexWS : POSITION;
    float2 SizeWS : SIZE;
};

struct GeoOut
{
    float4 PosH : SV_POSITION;
    float3 PosWS : POSITION;
    float3 NormalWS : NORMAL;
    float2 Tex : TEXCOORD;
};

struct VertexOut
{
    float3 CenterPosWS : POSITION;
    float2 SizeWS : SIZE;
};

float3 EnvBRDFApprox(float3 specularColor, float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    float2 AB = float2(-1.04f, 1.04f) * a004 + r.zw;
    return specularColor * AB.x + AB.y;
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
    o.CenterPosWS = v.VertexWS;
    o.SizeWS = v.SizeWS;
    return o;
}

[maxvertexcount(4)]
void GS(point VertexOut gin[1], inout TriangleStream<GeoOut> triStream)
{
    float3 up = float3(0.0f, 1.0f, 0.0f);
    float3 look = gCameraPosW - gin[0].CenterPosWS;
    look.y = 0.0f;
    if (length(look) < 0.001f)
    {
        look = float3(0.0f, 0.0f, 1.0f);
    }
    else
    {
        look = normalize(look);
    }

    float3 right = cross(look, up);
    float halfWidth = 0.5f * gin[0].SizeWS.x;
    float halfHeight = 0.5f * gin[0].SizeWS.y;

    float4 v[4];
    v[0] = float4(gin[0].CenterPosWS + right * halfWidth - up * halfHeight, 1.0f);
    v[1] = float4(gin[0].CenterPosWS + right * halfWidth + up * halfHeight, 1.0f);
    v[2] = float4(gin[0].CenterPosWS - right * halfWidth - up * halfHeight, 1.0f);
    v[3] = float4(gin[0].CenterPosWS - right * halfWidth + up * halfHeight, 1.0f);

    float2 texC[4] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, 0.0f)
    };

    GeoOut gOut;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        gOut.NormalWS = look;
        gOut.PosH = mul(v[i], gViewProj);
        gOut.PosWS = v[i].xyz;
        gOut.Tex = texC[i];
        triStream.Append(gOut);
    }
}

float4 PS(GeoOut i) : SV_Target
{
    float4 baseColor = gBaseColorMap.Sample(samAnisotropicWarp, i.Tex) * gBaseColorFactor;
    clip(baseColor.a - gAlphaCutoff);

    float roughness = clamp(gRoughnessMap.Sample(samLinearClamp, i.Tex).r * gRoughnessFactor, 0.05f, 1.0f);
    float metallic = saturate(gMetallicMap.Sample(samLinearClamp, i.Tex).r * gMetallicFactor);
    float3 viewDirWS = normalize(gCameraPosW - i.PosWS);
    float3 normalWS = normalize(i.NormalWS);

    Material mat;
    mat.baseColor = baseColor.rgb;
    mat.fresnelR0 = gFresnelR0;
    mat.roughness = roughness;
    mat.metallic = metallic;

    float3 directLight = ComputeLighting(gLights, mat, i.PosWS, normalWS, viewDirWS, 1.0f);

    float NdotV = saturate(dot(normalWS, viewDirWS));
    float3 F0 = lerp(gFresnelR0, baseColor.rgb, metallic);
    float3 kS = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    float envMip = max(gEnvMapMipCount - 1.0f, 0.0f);
    float3 diffuseEnv = gEnvMap.SampleLevel(samLinearClamp, normalWS, envMip).rgb;
    float3 reflectDir = reflect(-viewDirWS, normalWS);
    float3 prefilteredEnv = gEnvMap.SampleLevel(samLinearClamp, reflectDir, roughness * envMip).rgb;
    float3 specularIBL = prefilteredEnv * EnvBRDFApprox(F0, roughness, NdotV);

    float3 finalColor = directLight + gAmbientLight.rgb * baseColor.rgb * kD + gIblStrength * (diffuseEnv * baseColor.rgb * kD + specularIBL);
    finalColor = ToneMapACES(finalColor);
    finalColor = pow(finalColor, 1.0f / 2.2f);

    return float4(finalColor, baseColor.a);
}
