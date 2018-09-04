#ifndef SHADER_LIBRARY_HLSL
#define SHADER_LIBRARY_HLSL

#define HLSL
#include "RaytracingHlslCompat.h"

// Global root signature
RWTexture2D<float4> gOutput : register(u0);
RaytracingAccelerationStructure SceneBVH : register(t0);
cbuffer CameraConstants : register(b0)
{
    CameraParams cameraParams;
}

// Hit group local root signature
StructuredBuffer<Vertex> vertexBuffer : register(t1);

[shader("raygeneration")] 
void RayGen() 
{
    uint2 launchIndex = DispatchRaysIndex();
    float2 dims = float2(DispatchRaysDimensions().xy);
    float2 d = (((launchIndex.xy + 0.5f) / dims.xy) * 2.f - 1.f);

    int numAASamples = 4;
    float3 averageColor = float3(0.0f, 0.0f, 0.0f);

    const float2 jitters[4] = 
    {
        { -1.0, -1.0 },
        { -1.0,  1.0 },
        {  1.0,  1.0 },
        {  1.0, -1.0 }    
    };
 
    for (int i = 0; i < numAASamples; ++i) {
        HitInfo payload;
        payload.colorAndDistance = float4(0, 0, 0, 0);

        float2 jitter = jitters[i] / dims;

        RayDesc ray;
        ray.Origin = cameraParams.worldEyePos + float3(jitter.x, jitter.y, 0.0f);
        ray.Direction = normalize(d.x * cameraParams.U + (-d.y) * cameraParams.V + cameraParams.W);
        ray.TMin = 0;
        ray.TMax = 100000;

        TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

        averageColor += payload.colorAndDistance.rgb;
    }
    averageColor /= numAASamples;

    gOutput[launchIndex] = float4(pow(averageColor, 1.0f / 2.2f), 1.0f);
}

float3 shade(float3 position, float3 normal)
{
    const float3 lightDir = float3(1.0f, 0.5f, 0.25f);
    const float3 diffuseColor = float3(0.95f, 0.93f, 0.92f);

    float3 L = normalize(lightDir);
    float3 N = normalize(normal);
    float NoL = saturate(dot(N, L));

    float3 color = diffuseColor * NoL;

    return color;
}

[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) 
{
    uint vertId = 3 * PrimitiveIndex();
    float3 barycentrics = float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

    float3 hitPosition = vertexBuffer[vertId + 0].position * barycentrics.x +
                         vertexBuffer[vertId + 1].position * barycentrics.y +
                         vertexBuffer[vertId + 2].position * barycentrics.z;
 
    float3 hitNormal = vertexBuffer[vertId + 0].normal * barycentrics.x +
                       vertexBuffer[vertId + 1].normal * barycentrics.y +
                       vertexBuffer[vertId + 2].normal * barycentrics.z;

    float3 color = shade(hitPosition, hitNormal);

    payload.colorAndDistance = float4(color, RayTCurrent());
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
