static const float PI = 3.14159265359f;

struct Light
{
    float3 strength;
    float falloffStart;
    float3 direction;
    float falloffEnd;
    float3 position;
    float spotPower;
};

struct Material
{
    float3 baseColor;
    float3 fresnelR0;
    float roughness;
    float metallic;
};

float CalcAttenuation(float distanceToLight, float falloffEnd, float falloffStart)
{
    return saturate((falloffEnd - distanceToLight) / max(falloffEnd - falloffStart, 0.001f));
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 oneMinusRoughness = float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness);
    return F0 + (max(oneMinusRoughness, F0) - F0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * denom * denom, 1e-4f);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotV / max(NdotV * (1.0f - k) + k, 1e-4f);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float ggxV = GeometrySchlickGGX(NdotV, roughness);
    float ggxL = GeometrySchlickGGX(NdotL, roughness);
    return ggxV * ggxL;
}

float3 EvaluateCookTorrance(Material mat, float3 normalWS, float3 viewDirWS, float3 lightDirWS, float3 radiance)
{
    float3 halfVec = normalize(viewDirWS + lightDirWS);

    float NdotV = saturate(dot(normalWS, viewDirWS));
    float NdotL = saturate(dot(normalWS, lightDirWS));
    float NdotH = saturate(dot(normalWS, halfVec));
    float VdotH = saturate(dot(viewDirWS, halfVec));

    if (NdotV <= 0.0f || NdotL <= 0.0f)
    {
        return 0.0f;
    }

    float3 F0 = lerp(mat.fresnelR0, mat.baseColor, mat.metallic);
    float3 F = FresnelSchlick(VdotH, F0);
    float D = DistributionGGX(NdotH, mat.roughness);
    float G = GeometrySmith(NdotV, NdotL, mat.roughness);

    float3 numerator = D * G * F;
    float denominator = max(4.0f * NdotV * NdotL, 1e-4f);
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - mat.metallic);
    float3 diffuse = kD * mat.baseColor / PI;

    return (diffuse + specular) * radiance * NdotL;
}

float3 ComputeDirectionalLight(Light light, Material mat, float3 normalWS, float3 viewDirWS)
{
    float3 lightDir = normalize(-light.direction);
    float3 radiance = light.strength;
    return EvaluateCookTorrance(mat, normalWS, viewDirWS, lightDir, radiance);
}

float3 ComputePointLight(Light light, Material mat, float3 posWS, float3 normalWS, float3 viewDirWS)
{
    float3 lightDir = light.position - posWS;
    float distanceToLight = length(lightDir);
    if (distanceToLight > light.falloffEnd)
    {
        return 0.0f;
    }

    lightDir /= max(distanceToLight, 1e-4f);
    float attenuation = CalcAttenuation(distanceToLight, light.falloffEnd, light.falloffStart);
    float3 radiance = light.strength * attenuation;
    return EvaluateCookTorrance(mat, normalWS, viewDirWS, lightDir, radiance);
}

float3 ComputeSpotLight(Light light, Material mat, float3 posWS, float3 normalWS, float3 viewDirWS)
{
    float3 lightDir = light.position - posWS;
    float distanceToLight = length(lightDir);
    if (distanceToLight > light.falloffEnd)
    {
        return 0.0f;
    }

    lightDir /= max(distanceToLight, 1e-4f);
    float attenuation = CalcAttenuation(distanceToLight, light.falloffEnd, light.falloffStart);
    float spotFactor = pow(saturate(dot(-lightDir, normalize(light.direction))), light.spotPower);
    float3 radiance = light.strength * attenuation * spotFactor;
    return EvaluateCookTorrance(mat, normalWS, viewDirWS, lightDir, radiance);
}

#define MAX_LIGHTS 16

float3 ComputeLighting(Light lights[MAX_LIGHTS], Material mat, float3 posWS, float3 normalWS, float3 viewDirWS, float shadowFactor)
{
    float3 result = 0.0f;
    int i = 0;

#if (NUM_DIR_LIGHTS > 0)
    for (i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        const float dirShadow = (i == 0) ? shadowFactor : 1.0f;
        result += dirShadow * ComputeDirectionalLight(lights[i], mat, normalWS, viewDirWS);
    }
#endif

#if (NUM_POINT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; ++i)
    {
        result += ComputePointLight(lights[i], mat, posWS, normalWS, viewDirWS);
    }
#endif

#if (NUM_SPOT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
    {
        result += ComputeSpotLight(lights[i], mat, posWS, normalWS, viewDirWS);
    }
#endif

    return result;
}
