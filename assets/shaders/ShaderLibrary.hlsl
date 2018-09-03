#ifndef SHADER_LIBRARY_HLSL
#define SHADER_LIBRARY_HLSL

#define HLSL
#include "RaytracingHlslCompat.h"

RWTexture2D<float4> gOutput : register(u0);
RaytracingAccelerationStructure SceneBVH : register(t0);
cbuffer CameraConstants : register(b0)
{
    CameraParams cameraParams;
}

[shader("raygeneration")] 
void RayGen() 
{
    HitInfo payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);

    uint2 launchIndex = DispatchRaysIndex();
    float2 dims = float2(DispatchRaysDimensions().xy);
    float2 d = (((launchIndex.xy + 0.5f) / dims.xy) * 2.f - 1.f);

    RayDesc ray;
    ray.Origin = cameraParams.worldEyePos;
    ray.Direction = normalize(d.x * cameraParams.U + (-d.y) * cameraParams.V + cameraParams.W);
    ray.TMin = 0;
    ray.TMax = 100000;

    TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    gOutput[launchIndex] = float4(payload.colorAndDistance.rgb, 1.0f);
}

[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) 
{
    float3 barycentrics = float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

    const float3 A = float3(1, 1, 1);
    const float3 B = float3(1, 0, 1);
    const float3 C = float3(0, 1, 1);

    float3 hitColor = A * barycentrics.x + B * barycentrics.y + C * barycentrics.z;

    payload.colorAndDistance = float4(hitColor, RayTCurrent());
}

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload)
{
    uint2 launchIndex = DispatchRaysIndex();
    float2 dims = float2(DispatchRaysDimensions().xy);

    float ramp = launchIndex.y / dims.y;
    payload.colorAndDistance = float4(1.0f, 0.2f, 0.7f - 0.3f * ramp, -1.0f);
}

#endif // SHADER_LIBRARY_HLSL
