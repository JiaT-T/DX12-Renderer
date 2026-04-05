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

Texture2D gDiffuseMap : register(t0);
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
    float4 gDiffuseAlbedo; //材质反照率
    float3 gFresnelR0; //RF(0)值，即材质的反射属性
    float gRoughness; //材质的粗糙度
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
    float3 VertexWS : POSITION; // 公告板中心点的世界坐标
    float2   SizeWS : SIZE;                // 公告板长宽
};

// 几何着色器的输出结构体
struct GeoOut
{
    float4     PosH : SV_POSITION;
    float3    PosWS : POSITION;
    float3 NormalWS : NORMAL;
    float2      Tex : TEXCOORD;
};

struct VertexOut
{
    float3 CenterPosWS: POSITION; // 公告板中心点的世界坐标
    float2 SizeWS : SIZE;                   // 公告板长宽
};

VertexOut VS(VertexIn v)
{
    VertexOut o;
    o.CenterPosWS = v.VertexWS;
    o.SizeWS = v.SizeWS;
    
    return o;
}

[maxvertexcount(4)]                                     //最多输出4个顶点
void GS(point VertexOut gin[1], //输入图元是一个“点”，所以gin数组元素数量为1
        inout TriangleStream<GeoOut> triStream)          //输出流是三角带
{
    float3 up = float3(0.0f, 1.0f, 0.0f);
    float3 look = gCameraPosW - gin[0].CenterPosWS;
    look.y = 0.0f;
    if (length(look) < 0.001f)
    {
        // 如果相机刚好和树重合，给一个默认的朝向
        look = float3(0.0f, 0.0f, 1.0f);
    }
    else
    {
        look = normalize(look);
    }
    float3 right = cross(look, up);
    
    //
    // 计算三角形带的顶点（四边形）
    //
    float halfWidth = 0.5f * gin[0].SizeWS.x;
    float halfHeight = 0.5f * gin[0].SizeWS.y;
    
    float4 v[4];
    v[0] = float4(gin[0].CenterPosWS + right * halfWidth - up * halfHeight, 1.0f);
    v[1] = float4(gin[0].CenterPosWS + right * halfWidth + up * halfHeight, 1.0f);
    v[2] = float4(gin[0].CenterPosWS - right * halfWidth - up * halfHeight, 1.0f);
    v[3] = float4(gin[0].CenterPosWS - right * halfWidth + up * halfHeight, 1.0f);
    
    // 计算UV坐标
    float2 texC[4] =
    {
        float2(0.0f, 1.0f), //v[1]的UV
        float2(0.0f, 0.0f), //v[2]的UV
        float2(1.0f, 1.0f), //v[3]的UV
        float2(1.0f, 0.0f)  //v[4]的UV   
    };
    
    // 输出数据
    GeoOut gOut;
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        gOut.NormalWS = look;
        gOut.PosH = mul(v[i], gViewProj);
        gOut.PosWS = v[i].xyz;
        gOut.Tex = texC[i];
        
        //将输出数据合并至输出流
        triStream.Append(gOut);
    }
}

float4 PS(GeoOut i) : SV_Target
{
    float4 diffuseAlbedo = gDiffuseMap.Sample(samAnisotropicWarp, i.Tex);
    
    clip(diffuseAlbedo.a - 0.1f);
    
    float3 normalWS = normalize(i.NormalWS);
    float3 toEyeWS = gCameraPosW - i.PosWS;
    float disToEye = length(toEyeWS);
    float3 viewDirWS = toEyeWS / disToEye;
    
    Material mat = { diffuseAlbedo, gFresnelR0, gRoughness };
    float shadowFactor = 1.0f;
    float3 ambient = gAmbientLight * diffuseAlbedo.rgb;
    float3 directLight = ComputeLighting(gLights, mat, i.PosWS, normalWS, viewDirWS, shadowFactor);

    float3 finalCol = ambient + directLight;
    
    return float4(finalCol, diffuseAlbedo.a);
}