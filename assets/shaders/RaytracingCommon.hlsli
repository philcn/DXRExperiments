#ifndef RAYTRACING_COMMON_HLSLI
#define RAYTRACING_COMMON_HLSLI

#define HLSL
#include "RaytracingHlslCompat.h"
#include "RaytracingUtils.hlsli"

#define RAY_MAX_T 1.0e+38f
#define RAY_EPSILON 0.0001

#define MAX_RADIANCE_RAY_DEPTH 1
#define MAX_SHADOW_RAY_DEPTH 2

////////////////////////////////////////////////////////////////////////////////
// Global root signature
////////////////////////////////////////////////////////////////////////////////

RaytracingAccelerationStructure SceneBVH : register(t0);
ConstantBuffer<PerFrameConstants> perFrameConstants : register(b0);

SamplerState defaultSampler : register(s0);

////////////////////////////////////////////////////////////////////////////////
// Hit-group local root signature
////////////////////////////////////////////////////////////////////////////////

// StructuredBuffer indexing is not supported in compute path of Fallback Layer,
// must use typed buffer or raw buffer in compute path.
#define USE_STRUCTURED_VERTEX_BUFFER 0
#if USE_STRUCTURED_VERTEX_BUFFER
StructuredBuffer<Vertex> vertexBuffer : register(t0, space1);
#else
Buffer<float3> vertexBuffer : register(t0, space1);
#endif

ByteAddressBuffer indexBuffer : register(t1, space1);

cbuffer MaterialConstants : register(b0, space1)
{
    MaterialParams materialParams;
}
////////////////////////////////////////////////////////////////////////////////
// Miss shader local root signature
////////////////////////////////////////////////////////////////////////////////

Texture2D envMap : register(t0, space2);
TextureCube envCubemap : register(t1, space2);

////////////////////////////////////////////////////////////////////////////////
// Common routines
////////////////////////////////////////////////////////////////////////////////

void interpolateVertexAttributes(float2 bary, out float3 vertPosition, out float3 vertNormal)
{
    float3 barycentrics = float3(1.f - bary.x - bary.y, bary.x, bary.y);

    uint baseIndex = PrimitiveIndex() * 3;
    int address = baseIndex * 4;
    const uint3 indices = Load3x32BitIndices(address, indexBuffer);

    const uint strideInFloat3s = 2;
    const uint positionOffsetInFloat3s = 0;
    const uint normalOffsetInFloat3s = 1;

#if USE_STRUCTURED_VERTEX_BUFFER
    vertPosition = vertexBuffer[indices[0]].position * barycentrics.x +
        vertexBuffer[indices[1]].position * barycentrics.y +
        vertexBuffer[indices[2]].position * barycentrics.z;

    vertNormal = vertexBuffer[indices[0]].normal * barycentrics.x +
        vertexBuffer[indices[1]].normal * barycentrics.y +
        vertexBuffer[indices[2]].normal * barycentrics.z;
#else
    vertPosition = vertexBuffer[indices[0] * strideInFloat3s + positionOffsetInFloat3s] * barycentrics.x +
        vertexBuffer[indices[1] * strideInFloat3s + positionOffsetInFloat3s] * barycentrics.y +
        vertexBuffer[indices[2] * strideInFloat3s + positionOffsetInFloat3s] * barycentrics.z;

    vertNormal = vertexBuffer[indices[0] * strideInFloat3s + normalOffsetInFloat3s] * barycentrics.x +
        vertexBuffer[indices[1] * strideInFloat3s + normalOffsetInFloat3s] * barycentrics.y +
        vertexBuffer[indices[2] * strideInFloat3s + normalOffsetInFloat3s] * barycentrics.z;
#endif
}

float shootShadowRay(float3 orig, float3 dir, float minT, float maxT, uint currentDepth)
{
    if (currentDepth >= MAX_SHADOW_RAY_DEPTH) {
        return 1.0;
    }

    RayDesc ray = { orig, minT, dir, maxT };

    ShadowPayload payload = { 0.0 };

    TraceRay(SceneBVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 1, 0, 1, ray, payload);
    return payload.lightVisibility;
}

float evaluateAO(float3 position, float3 normal)
{
    float visibility = 0.0f;
    const int aoRayCount = 4;

    uint2 pixIdx = DispatchRaysIndex().xy;
    uint2 numPix = DispatchRaysDimensions().xy;
    uint randSeed = initRand(pixIdx.x + pixIdx.y * numPix.x, perFrameConstants.cameraParams.frameCount);

    for (int i = 0; i < aoRayCount; ++i) {
        float3 sampleDir;
        float NoL;
        float pdf;
        if (perFrameConstants.options.cosineHemisphereSampling) {
            sampleDir = getCosHemisphereSample(randSeed, normal);
            NoL = saturate(dot(normal, sampleDir));
            pdf = NoL / M_PI;
        } else {
            sampleDir = getUniformHemisphereSample(randSeed, normal);
            NoL = saturate(dot(normal, sampleDir));
            pdf = 1.0 / (2.0 * M_PI);
        }
        visibility += shootShadowRay(position, sampleDir, RAY_EPSILON, 10.0, 1) * NoL / pdf;
    }

    return visibility / float(aoRayCount);
}

float3 evaluateDirectionalLight(float3 position, float3 normal, uint currentDepth)
{
    float3 L = normalize(-perFrameConstants.directionalLight.forwardDir.xyz);
    float NoL = saturate(dot(normal, L));

    float visible = shootShadowRay(position, L, RAY_EPSILON, RAY_MAX_T, currentDepth);

    return perFrameConstants.directionalLight.color.rgb * perFrameConstants.directionalLight.color.a * NoL * visible;
}

float3 evaluatePointLight(float3 position, float3 normal, uint currentDepth)
{
    float3 lightPath = perFrameConstants.pointLight.worldPos.xyz - position;
    float lightDistance = length(lightPath);
    float3 L = normalize(lightPath);
    float NoL = saturate(dot(normal, L));

    float visible = shootShadowRay(position, L, RAY_EPSILON, lightDistance - RAY_EPSILON, currentDepth);

    float falloff = 1.0 / (2 * M_PI * lightDistance * lightDistance);
    return perFrameConstants.pointLight.color.rgb * perFrameConstants.pointLight.color.a * NoL * visible * falloff;
}

float3 sampleEnvironment()
{
    // cubemap
    float4 envSample = envCubemap.SampleLevel(defaultSampler, WorldRayDirection().xyz, 0.0);

    // lat-long environment map
    // float2 uv = wsVectorToLatLong(WorldRayDirection().xyz);
    // float4 envSample = envMap.SampleLevel(defaultSampler, uv, 0.0);

    return envSample.rgb * perFrameConstants.options.environmentStrength;
}

#endif // RAYTRACING_COMMON_HLSLI
