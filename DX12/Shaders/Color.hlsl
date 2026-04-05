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
    float4 gDiffuseAlbedo;                 //材质反照率
    float3 gFresnelR0;      //RF(0)值，即材质的反射属性
    float gRoughness;                    //材质的粗糙度
};

cbuffer cbPass : register(b2)
{
    float4x4 gViewProj;
    float3 gCameraPosW;
    float gTotalTime;
    float3 gAmbientLight;
    Light gLights[MAX_LIGHTS];
};

struct VertexIn
{
    float3  PosLS : POSITION;
    float3 Normal : NORMAL;
    float2   TexC : TEXCOORD;
};

Texture2D gDiffuseMap : register(t0);
SamplerState samPointWarp         : register(s0);
SamplerState samPointClamp        : register(s1);
SamplerState samLinearWarp        : register(s2);
SamplerState samLinearClamp       : register(s3);
SamplerState samAnisotropicWarp   : register(s4);
SamplerState samAnisotropicClamp  : register(s5);
SamplerState samAnisotropicMirror : register(s6);


struct VertexOut
{
    float4    PosHS : SV_POSITION; //齐次裁剪空间
    float3    PosWS : POSITION;
    float3 NormalWS : NORMAL;
    float2     TexC : TEXCOORD;
};

VertexOut VS(VertexIn v)
{
    VertexOut o;
    
    float4 PosW = mul(float4(v.PosLS, 1.0f), gWorld);
        o.PosWS = PosW.xyz;
     o.NormalWS = mul(v.Normal, (float3x3) gWorldInvTrans);
        o.PosHS = mul(PosW, gViewProj);
         o.TexC = v.TexC;
    
    return o;
}

float4 PS(VertexOut i) : SV_TARGET
{
    float3 normalWS = normalize(i.NormalWS);
    float3 viewDirWS = normalize(gCameraPosW - i.PosWS);
    
    float4 diffuseAlbedo = gDiffuseMap.Sample(samAnisotropicWarp, i.TexC) * gDiffuseAlbedo;
    
    Material mat = { diffuseAlbedo, gFresnelR0, gRoughness };
    float shadowFactor = 1.0f; //暂时使用1.0，不对计算产生影响

    //直接光
    float3 directLight = ComputeLighting(gLights, mat, i.PosWS, normalWS, viewDirWS, shadowFactor);
    //间接光
    float3 ambient = gAmbientLight * diffuseAlbedo.xyz;
    
    float3 finalColor = directLight + ambient;
    float alpha = diffuseAlbedo.a;
    // 确保颜色值在有效范围内
    finalColor = saturate(finalColor);
    
    return float4(finalColor, alpha);
}