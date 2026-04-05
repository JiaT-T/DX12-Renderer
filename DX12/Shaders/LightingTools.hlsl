struct Light
{
    float3 strength;                 //光源颜色（三光通用）
    float falloffStart;     //点光灯和聚光灯的开始衰减距离
    float3 direction;           //方向光和聚光灯的方向向量
    float falloffEnd;         //点光和聚光灯的衰减结束距离
    float3 position;                  //点光和聚光灯的坐标
    float spotPower;                //聚光灯因子中的幂参数
};

struct Material
{
    float4 diffuseAlbedo;                     //材质反照率
    float3 fresnelR0;          //RF(0)值，即材质的反射属性
    float roughness;                        //材质的粗糙度
};

//float ToonDiffuse(float kd)
//{
//    if (kd <= 0.1f)   
//        return 0.4f;
//    else if (kd <= 0.5f)
//        return 0.6f;
//    else 
//        return 1.0f;
//}

//float ToonSpec(float ks)
//{
//    if (ks <= 0.1f)
//        return 0.0f;
//    else if (ks <= 0.6f)
//        return 0.5f;
//    else
//        return 0.8f;
//}

//光线衰减方程
float CalcAttenuation(float d, float falloffEnd, float falloffStart)
{
    //d是离灯光的距离
    float att = saturate((falloffEnd - d) / (falloffEnd - falloffStart));
    return att;
}

//石里克近似方程（模拟菲涅尔效应）
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightDir)
{
    //根据石里克方程计算出入射光线被反射的百分比
    float3 reflectPercent = R0 + (1.0f - R0) * pow(1 - saturate(dot(normal, lightDir)), 5.0f);
    return reflectPercent;
}

//Blinn-Phong模型
float3 BlinnPhong(Material mat, float3 normal, float3 viewDir, float3 lightDir, float3 lightStrength)
{
    float               m = (1.0f - mat.roughness) * 256.0f;                                       //粗糙度因子里的m值
    float3        halfVec = normalize(lightDir + viewDir);                                                  //半角向量
    float ks = max(dot(normal, halfVec), 0.0f);
    float roughnessFactor = (m + 8.0f) * pow(ks, m) / 8.0f; //粗糙度因子
    
    // 修复：Schlick Fresnel 应该使用视角方向和半角向量的点积
    float3  fresnelFactor = mat.fresnelR0 + (1.0f - mat.fresnelR0) * pow(1.0f - saturate(dot(halfVec, viewDir)), 5.0f);
    float3     specAlbedo = fresnelFactor * roughnessFactor;                    //镜面反射反照率=菲尼尔因子*粗糙度因子
               specAlbedo = specAlbedo / (specAlbedo + 1.0f);                           //将镜面反射反照率缩放到[0，1]
    
    // 确保高光值不会导致负值或异常
    specAlbedo = saturate(specAlbedo);
    
    float3 diff_Spec = lightStrength * (mat.diffuseAlbedo.rgb + specAlbedo);     //漫反射+高光反射=入射光量*总的反照率
    return diff_Spec;                                                                            //返回漫反射+高光反射
}   

//计算平行光
float3 ComputeDirectionalLight(Light light, Material mat, float3 normal, float3 viewDir)
{
    float3      lightDir = -light.direction;
    float       NdotL = max(dot(normal, lightDir), 0.0f);
    float3 lightStrength = light.strength * NdotL;
    return BlinnPhong(mat, normal, viewDir, lightDir, lightStrength);
}

//计算点光
float3 ComputePointLight(Light light, Material mat, float3 pos, float3 normal, float3 viewDir)
{
    float3 lightDir = light.position - pos;
    float  distance = length(lightDir);
    
    if (distance > light.falloffEnd)
        return 0;
    
                lightDir = normalize(lightDir);
    float          NdotL = max(0, dot(normal, lightDir));
    float3 lightStrength = NdotL * light.strength;
    
    float    atten = CalcAttenuation(distance, light.falloffEnd, light.falloffStart);
    lightStrength *= atten;
    return BlinnPhong(mat, normal, viewDir, lightDir, lightStrength);
}

//计算聚光
float3 ComputeSpotLight(Light light, Material mat, float3 pos, float3 normal, float3 viewDir)
{
    float3 lightDir = light.position - pos;         //顶点指向聚光灯光源的光向量
    float  distance = length(lightDir);             //顶点和光源的距离（向量模长）
    
    if (distance > light.falloffEnd)
        return 0;
    
                lightDir = normalize(lightDir);
    float          NdotL = max(dot(lightDir, normal), 0);                  //点积不能小于0
    float3 lightStrength = NdotL * light.strength; //点光再单位面积上的辐照度（没考虑衰减）
    
    //调用衰减函数
    float    atten = CalcAttenuation(distance, light.falloffEnd, light.falloffStart);
    lightStrength *= atten;                                       //衰减后的单位面积辐照度
    
    //计算聚光灯衰减因子
    float spotFactor = pow(max(dot(-lightDir, light.direction), 0), light.spotPower);
    lightStrength   *= spotFactor;
    
    //计算聚光灯的漫反射和高光反射
    return BlinnPhong(mat, normal, viewDir, lightDir, lightStrength);
}

//多种光照的叠加
#define MAX_LIGHTS 16

float3 ComputeLighting(Light lights[MAX_LIGHTS], Material mat, float3 pos, float3 normal, float3 viewDir, float shadowFactor)
{
    float3 result = 0.0f;
    int i = 0;
    
#if(NUM_DIR_LIGHTS > 0)
    for(i = 0; i < NUM_DIR_LIGHTS; i++)
    {
        //多个平行光的光照叠加（有阴影）
        result += shadowFactor * ComputeDirectionalLight(lights[i], mat, normal, viewDir);
    }
#endif
 
#if(NUM_POINT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i++)
    {
        //多个点光源的光照叠加
        result += ComputePointLight(lights[i], mat, pos, normal, viewDir);
    }
#endif
    
#if(NUM_SPOT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; i++)
    {
        //多个聚光灯光源的光照叠加
        result += ComputeSpotLight(lights[i], mat, pos, normal, viewDir);
    }
#endif
  
    return result;
}