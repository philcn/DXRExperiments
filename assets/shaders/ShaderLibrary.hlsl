#ifndef SHADER_LIBRARY_HLSL
#define SHADER_LIBRARY_HLSL

#define HLSL
#include "RaytracingHlslCompat.h"

#include "RaytracingUtils.hlsli"

////////////////////////////////////////////////////////////////////////////////
// Global root signature
////////////////////////////////////////////////////////////////////////////////

RWTexture2D<float4> gOutput : register(u0);
RaytracingAccelerationStructure SceneBVH : register(t0);
cbuffer PerFrameConstants : register(b0)
{
    CameraParams cameraParams;
    DirectionalLightParams directionalLight;
    uint frameCount;
}

SamplerState defaultSampler : register(s0);

////////////////////////////////////////////////////////////////////////////////
// Hit-group local root signature
////////////////////////////////////////////////////////////////////////////////

// StructuredBuffer<Vertex> vertexBuffer : register(t0, space1); // doesn't work in Fallback Layer
Buffer<float3> vertexData : register(t0, space1);

////////////////////////////////////////////////////////////////////////////////
// Miss shader local root signature
////////////////////////////////////////////////////////////////////////////////

// just for testing 32-bit local root parameter
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
    int numAASamples = 1;
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
        payload.depth = 0;

        float2 jitter = jitters[i] * 2.0f / dims;

        RayDesc ray;
        ray.Origin = cameraParams.worldEyePos.xyz + float3(jitter.x, jitter.y, 0.0f);
        ray.Direction = normalize(d.x * cameraParams.U + (-d.y) * cameraParams.V + cameraParams.W).xyz;
        ray.TMin = 0;
        ray.TMax = 100000;

        TraceRay(SceneBVH, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, ray, payload);

        averageColor += payload.colorAndDistance.rgb;
    }
    averageColor /= numAASamples;

    gOutput[launchIndex] = float4(pow(averageColor, 1.0f / 2.2f), 1.0f);
}

////////////////////////////////////////////////////////////////////////////////
// Hit groups
////////////////////////////////////////////////////////////////////////////////

void interpolateVertexAttributes(float2 bary, out float3 vertPosition, out float3 vertNormal)
{
    uint vertId = 3 * PrimitiveIndex();
    float3 barycentrics = float3(1.f - bary.x - bary.y, bary.x, bary.y);

    // vertPosition = vertexBuffer[vertId + 0].position * barycentrics.x +
    //                vertexBuffer[vertId + 1].position * barycentrics.y +
    //                vertexBuffer[vertId + 2].position * barycentrics.z;

    const uint strideInFloat3s = 2;
    const uint positionOffsetInFloat3s = 0;
    const uint normalOffsetInFloat3s = 1;

    vertPosition = vertexData.Load((vertId + 0) * strideInFloat3s + positionOffsetInFloat3s) * barycentrics.x +
                   vertexData.Load((vertId + 1) * strideInFloat3s + positionOffsetInFloat3s) * barycentrics.y +
                   vertexData.Load((vertId + 2) * strideInFloat3s + positionOffsetInFloat3s) * barycentrics.z;
 
    vertNormal = vertexData.Load((vertId + 0) * strideInFloat3s + normalOffsetInFloat3s) * barycentrics.x +
                 vertexData.Load((vertId + 1) * strideInFloat3s + normalOffsetInFloat3s) * barycentrics.y +
                 vertexData.Load((vertId + 2) * strideInFloat3s + normalOffsetInFloat3s) * barycentrics.z;
}

float3 shootIndirectRay(float3 orig, float3 dir, float minT, uint currentDepth)
{
    RayDesc ray = { orig, minT, dir, 1.0e+38f };

    HitInfo payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);
    payload.depth = currentDepth + 1;

    TraceRay(SceneBVH, 0, 0xFF, 0, 0, 0, ray, payload);

    float3 reflectionColor = payload.colorAndDistance.rgb;

    float hitT = payload.colorAndDistance.a;
    float falloff = max(1, (hitT * hitT));

    return hitT == -1 ? reflectionColor : reflectionColor * saturate(2.5 / falloff);
}

float shootShadowRay(float3 orig, float3 dir, float minT, float maxT)
{
    RayDesc ray = { orig, minT, dir, maxT };

    ShadowPayload payload = { 0.0 };
    TraceRay(SceneBVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 1, 0, 1, ray, payload);
    return payload.lightVisibility;
}

float evaluateAO(float3 position, float3 normal)
{
    float visibility = 1.0f;
    const int aoRayCount = 4;

    uint2 pixIdx = DispatchRaysIndex();
    uint2 numPix = DispatchRaysDimensions();
    uint randSeed = initRand(pixIdx.x + pixIdx.y * numPix.x, frameCount);

    for (int i = 0; i < aoRayCount; ++i) {
        float3 sampleDir = getCosHemisphereSample(randSeed, normal);
        visibility += shootShadowRay(position, sampleDir, 0.001, 10.0);
    }

    return visibility / float(aoRayCount);
}

float3 shade(float3 position, float3 normal, uint currentDepth)
{
    const float3 albedo = float3(0.85f, 0.85f, 0.85f);

    float3 L = normalize(-directionalLight.forwardDir.xyz);
    float3 N = normalize(normal);
    float NoL = dot(N, L);

    float3 color = 0.2;

    if (NoL > 0.0) {
        float visible = 1.0;
        if (currentDepth < 2) {
            visible = shootShadowRay(position, L, 0.001, 10.0);
        }
        color += albedo * directionalLight.color.rgb * NoL * visible;
    }

    if (currentDepth < 1) {
        float3 reflectDir = reflect(WorldRayDirection(), N);
        float3 reflectionColor = shootIndirectRay(position, reflectDir, 0.001, currentDepth);
        color += reflectionColor * 0.3;

        color *= evaluateAO(position, normal);
    }

    return color;
}

// Hit group 1

[shader("closesthit")] 
void PrimaryClosestHit(inout HitInfo payload, Attributes attrib) 
{
    float3 vertPosition, vertNormal;
    interpolateVertexAttributes(attrib.bary, vertPosition, vertNormal);

    float3 hitPosition = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    float3 color = shade(hitPosition, vertNormal, payload.depth);
    payload.colorAndDistance = float4(color, RayTCurrent());
}

// Hit group 2

[shader("anyhit")]
void ShadowAnyHit(inout ShadowPayload payload, Attributes attrib)
{
    // no-op
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
void PrimaryMiss(inout HitInfo payload : SV_RayPayload)
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);

    // cubemap, doesn't work now
    float4 radianceSample = radianceTexture.SampleLevel(defaultSampler, normalize(WorldRayDirection().xyz), 0.0);

    // lat-long environment map
    float2 uv = wsVectorToLatLong(WorldRayDirection().xyz);
    float4 envSample = envMap.SampleLevel(defaultSampler, uv, 0.0);

    payload.colorAndDistance = float4(envSample.rgb, -1.0);
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload : SV_RayPayload)
{
    payload.lightVisibility = 1.0;
}

#endif // SHADER_LIBRARY_HLSL
