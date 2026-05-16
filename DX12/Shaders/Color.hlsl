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
    float3 PosLS : POSITION;
    float3 Normal : NORMAL;
    float4 TangentU : TANGENT;
    float2 TexC : TEXCOORD;
};

Texture2D gBaseColorMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gRoughnessMap : register(t2);
Texture2D gMetallicMap : register(t3);
Texture2D gEnvMap : register(t4);
Texture2D gShadowMap : register(t5);

SamplerState samPointWarp         : register(s0);
SamplerState samPointClamp        : register(s1);
SamplerState samLinearWarp        : register(s2);
SamplerState samLinearClamp       : register(s3);
SamplerState samAnisotropicWarp   : register(s4);
SamplerState samAnisotropicClamp  : register(s5);
SamplerState samAnisotropicMirror : register(s6);
SamplerComparisonState samShadow  : register(s7);

struct VertexOut
{
    float4 PosHS : SV_POSITION;
    float3 PosWS : POSITION;
    float3 NormalWS : NORMAL;
    float4 TangentWS : TANGENT;
    float2 TexC : TEXCOORD;
    float4 ShadowPosH : TEXCOORD1;
};

float3 SampleNormalWS(float3 baseNormalWS, float4 tangentWS, float2 texC)
{
    float3 tangentNormal = gNormalMap.Sample(samAnisotropicWarp, texC).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= gNormalScale;
    tangentNormal.y *= (gNormalMapFlipY > 0.5f) ? -1.0f : 1.0f;
    tangentNormal = normalize(tangentNormal);

    float3 normalWS = normalize(baseNormalWS);
    float3 tangentDirWS = normalize(tangentWS.xyz - dot(tangentWS.xyz, normalWS) * normalWS);
    float3 bitangentWS = tangentWS.w * normalize(cross(normalWS, tangentDirWS));
    float3x3 tbn = float3x3(tangentDirWS, bitangentWS, normalWS);
    return normalize(mul(tangentNormal, tbn));
}

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

float2 DirectionToEquirectUV(float3 dir)
{
    dir = normalize(dir);

    const float invTwoPi = 0.15915494309f;
    const float invPi = 0.31830988618f;

    float u = atan2(dir.z, dir.x) * invTwoPi + 0.5f;
    float v = acos(clamp(dir.y, -1.0f, 1.0f)) * invPi;
    return float2(frac(u), saturate(v));
}

float3 SampleEnvironment(float3 dir, float mipLevel)
{
    return gEnvMap.SampleLevel(samLinearWarp, DirectionToEquirectUV(dir), mipLevel).rgb;
}

VertexOut VS(VertexIn v)
{
    VertexOut o;

    float4 posW = mul(float4(v.PosLS, 1.0f), gWorld);
    o.PosWS = posW.xyz;
    o.NormalWS = mul(v.Normal, (float3x3) gWorldInvTrans);
    o.TangentWS.xyz = mul(v.TangentU.xyz, (float3x3) gWorld);
    o.TangentWS.w = v.TangentU.w;
    o.PosHS = mul(posW, gViewProj);
    o.ShadowPosH = mul(posW, gShadowTransform);
    o.TexC = v.TexC;

    return o;
}

float CalcShadowFactor(float4 shadowPosH)
{
    shadowPosH.xyz /= shadowPosH.w;

    if (shadowPosH.x < 0.0f || shadowPosH.x > 1.0f ||
        shadowPosH.y < 0.0f || shadowPosH.y > 1.0f ||
        shadowPosH.z < 0.0f || shadowPosH.z > 1.0f)
    {
        return 1.0f;
    }

    uint width;
    uint height;
    uint mipCount;
    gShadowMap.GetDimensions(0, width, height, mipCount);

    const float dx = 1.0f / (float) width;
    const float depth = shadowPosH.z - 0.0025f;
    float percentLit = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            percentLit += gShadowMap.SampleCmpLevelZero(
                samShadow,
                shadowPosH.xy + float2(x, y) * dx,
                depth);
        }
    }

    return percentLit / 9.0f;
}

float4 PS(VertexOut i) : SV_TARGET
{
    float4 baseColor = gBaseColorMap.Sample(samAnisotropicWarp, i.TexC) * gBaseColorFactor;
    clip(baseColor.a - gAlphaCutoff);

    float3 viewDirWS = normalize(gCameraPosW - i.PosWS);
    float3 normalWS = SampleNormalWS(i.NormalWS, i.TangentWS, i.TexC);

    float roughness = saturate(gRoughnessMap.Sample(samLinearClamp, i.TexC).r * gRoughnessFactor);
    roughness = clamp(roughness, 0.05f, 1.0f);
    float metallic = saturate(gMetallicMap.Sample(samLinearClamp, i.TexC).r * gMetallicFactor);

    Material mat;
    mat.baseColor = baseColor.rgb;
    mat.fresnelR0 = gFresnelR0;
    mat.roughness = roughness;
    mat.metallic = metallic;

    float shadowFactor = CalcShadowFactor(i.ShadowPosH);
    float3 directLight = ComputeLighting(gLights, mat, i.PosWS, normalWS, viewDirWS, shadowFactor);

    float NdotV = saturate(dot(normalWS, viewDirWS));
    float3 F0 = lerp(gFresnelR0, baseColor.rgb, metallic);
    float3 kS = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kD = (1.0f - kS) * (1.0f - metallic);

    float envMip = max(gEnvMapMipCount - 1.0f, 0.0f);
    float3 diffuseEnv = SampleEnvironment(normalWS, envMip);
    float3 reflectDir = reflect(-viewDirWS, normalWS);
    float3 prefilteredEnv = SampleEnvironment(reflectDir, roughness * envMip);
    float3 specularIBL = prefilteredEnv * EnvBRDFApprox(F0, roughness, NdotV);
    float3 diffuseIBL = diffuseEnv * baseColor.rgb * kD;

    float3 ambientFallback = gAmbientLight.rgb * baseColor.rgb * kD;
    float3 finalColor = directLight + ambientFallback + gIblStrength * (diffuseIBL + specularIBL);
    finalColor = ToneMapACES(finalColor);
    finalColor = pow(finalColor, 1.0f / 2.2f);

    return float4(finalColor, baseColor.a);
}
