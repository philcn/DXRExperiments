#ifndef SHADER_LIBRARY_HLSL
#define SHADER_LIBRARY_HLSL

#define HLSL
#include "RaytracingHlslCompat.h"

////////////////////////////////////////////////////////////////////////////////
// Global root signature
////////////////////////////////////////////////////////////////////////////////

RWTexture2D<float4> gOutput : register(u0);
RaytracingAccelerationStructure SceneBVH : register(t0);
cbuffer CameraConstants : register(b0)
{
    CameraParams cameraParams;
}

SamplerState defaultSampler : register(s0);

////////////////////////////////////////////////////////////////////////////////
// Hit-group local root signature
////////////////////////////////////////////////////////////////////////////////

// StructuredBuffer<Vertex> vertexBuffer : register(t0, space1);
Buffer<float3> vertexData : register(t0, space1);

////////////////////////////////////////////////////////////////////////////////
// Miss shader local root signature
////////////////////////////////////////////////////////////////////////////////

cbuffer LocalData : register(b0, space2)
{
    int localData;
}

Texture2D envMap : register(t0, space2);
TextureCube radianceTexture : register(t1, space2);

////////////////////////////////////////////////////////////////////////////////
// Ray-gen shader
////////////////////////////////////////////////////////////////////////////////

[shader("raygeneration")] 
void RayGen() 
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);
    float2 d = (((launchIndex.xy + 0.5f) / dims.xy) * 2.f - 1.f);

    // +---+---+---+---+
    // |   | * |   |   | -0.125, -0.375
    // +---+---+---+---+
    // |   |   |   | * |  0.375, -0.125
    // +---+---+---+---+
    // | * |   |   |   | -0.375,  0.125
    // +---+---+---+---+
    // |   |   | * |   |  0.125,  0.375
    // +---+---+---+---+
    int numAASamples = 4;
    const float2 jitters[4] = 
    {
        { -0.125, -0.375 },
        {  0.375, -0.125 },
        { -0.375,  0.125 },
        {  0.125,  0.375 }    
    };
 
    float3 averageColor = float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < numAASamples; ++i) {
        HitInfo payload;
        payload.colorAndDistance = float4(0, 0, 0, 0);

        float2 jitter = jitters[i] * 2.0f / dims;

        RayDesc ray;
        ray.Origin = cameraParams.worldEyePos.xyz + float3(jitter.x, jitter.y, 0.0f);
        ray.Direction = normalize(d.x * cameraParams.U + (-d.y) * cameraParams.V + cameraParams.W).xyz;
        ray.TMin = 0;
        ray.TMax = 100000;

        TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

        averageColor += payload.colorAndDistance.rgb;
    }
    averageColor /= numAASamples;

    gOutput[launchIndex] = float4(pow(averageColor, 1.0f / 2.2f), 1.0f);
}

////////////////////////////////////////////////////////////////////////////////
// Hit shader
////////////////////////////////////////////////////////////////////////////////

void interpolateHitPointData(float2 bary, out float3 hitPosition, out float3 hitNormal)
{
    uint vertId = 3 * PrimitiveIndex();
    float3 barycentrics = float3(1.f - bary.x - bary.y, bary.x, bary.y);

    /*
        hitPosition = vertexBuffer[vertId + 0].position * barycentrics.x +
                      vertexBuffer[vertId + 1].position * barycentrics.y +
                      vertexBuffer[vertId + 2].position * barycentrics.z;
    */

    const uint strideInFloat3s = 2;
    const uint positionOffsetInFloat3s = 0;
    const uint normalOffsetInFloat3s = 1;

    hitPosition = vertexData.Load((vertId + 0) * strideInFloat3s + positionOffsetInFloat3s) * barycentrics.x +
                  vertexData.Load((vertId + 1) * strideInFloat3s + positionOffsetInFloat3s) * barycentrics.y +
                  vertexData.Load((vertId + 2) * strideInFloat3s + positionOffsetInFloat3s) * barycentrics.z;
 
    hitNormal = vertexData.Load((vertId + 0) * strideInFloat3s + normalOffsetInFloat3s) * barycentrics.x +
                vertexData.Load((vertId + 1) * strideInFloat3s + normalOffsetInFloat3s) * barycentrics.y +
                vertexData.Load((vertId + 2) * strideInFloat3s + normalOffsetInFloat3s) * barycentrics.z;
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
    float3 hitPosition, hitNormal;
    interpolateHitPointData(attrib.bary, hitPosition, hitNormal);

    float3 color = shade(hitPosition, hitNormal);

    payload.colorAndDistance = float4(color, RayTCurrent());
}

////////////////////////////////////////////////////////////////////////////////
// Miss shader
////////////////////////////////////////////////////////////////////////////////

#define M_PI 3.1415927
#define M_1_PI (1.0 / M_PI)

float2 wsVectorToLatLong(float3 dir)
{
    float3 p = normalize(dir);
    float u = (1.f + atan2(p.x, -p.z) * M_1_PI) * 0.5f;
    float v = acos(p.y) * M_1_PI;
    return float2(u, v);
}

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload)
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);

    // cubemap
//    float4 radianceSample = radianceTexture.SampleLevel(defaultSampler, launchIndex / dims, 0.0); // Texture2D
    float4 radianceSample = radianceTexture.SampleLevel(defaultSampler, normalize(WorldRayDirection().xyz), 0.0); // TextureCube

    // lat-long map
    float2 uv = wsVectorToLatLong(WorldRayDirection().xyz);
    float4 envSample = envMap.SampleLevel(defaultSampler, uv, 0.0);

    payload.colorAndDistance = float4(envSample.rgb, -1.0);
}

#endif // SHADER_LIBRARY_HLSL
