#include "Common.hlsl"

// Raytracing output texture, accessed as a UAV
RWTexture2D< float4 > gOutput : register(u0);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);

cbuffer CameraParams : register(b0)
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewI;
    float4x4 projectionI;
}

[shader("raygeneration")] 
void RayGen()
{
    // Initialize the ray payload
    HitInfo payload;
    payload.colorAndDistance = float4(0.1, 0.6, 0.2, 1);

    // Get the location within the dispatched 2D grid of work items
    // (often maps to pixels, so this could represent a pixel coordinate).
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);
    // /5 means translaing the start point from the top left corner to the center of the pixel
    // /dims means normalizing the coordinates to [0, 1], aim to remove 
    // the dependency on the resolution of the output texture  
    float2 d = (((launchIndex.xy + 0.5f)) / dims) * 2.f - 1.f;
    float aspectRatio = dims.x / dims.y;
    
    RayDesc ray;
    ray.Origin = mul(viewI, float4(0, 0, 0, 1));
    float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
    ray.Direction = mul(viewI, float4(target.xyz, 0));
    ray.TMin = 0;
    ray.TMax = 100000;
    
    TraceRay(SceneBVH,                  //Acceleration structure
           RAY_FLAG_NONE,                            //Ray flags
           0xFF,                         //InstanceInclusionMask
           0,                   //RayContributionToHitGroupIndex
           0, //MultiplierForGeometryContributionToHitGroupIndex
           0,                                  //MissShaderIndex
           ray,                                            //Ray
           payload);                                   //Payload 
           

    gOutput[launchIndex] = float4(payload.colorAndDistance.rgb, 1.f);
}
