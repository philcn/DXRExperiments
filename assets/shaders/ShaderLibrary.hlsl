#ifndef SHADER_LIBRARY_HLSL
#define SHADER_LIBRARY_HLSL

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

RWTexture2D<float4> gOutput : register(u0);
RaytracingAccelerationStructure SceneBVH : register(t0);
cbuffer PerFrameConstants : register(b0)
{
    CameraParams cameraParams;
    DebugOptions options;  
    DirectionalLightParams directionalLight;
    PointLightParams pointLight; 
}

SamplerState defaultSampler : register(s0);

////////////////////////////////////////////////////////////////////////////////
// Hit-group local root signature
////////////////////////////////////////////////////////////////////////////////

// StructuredBuffer indexing is not supported in compute path of Fallback Layer
// On the other hand, native DXR path requires buffers bound through root 
// descriptors to be either StructuredBuffer or raw buffer (ByteAddressBuffer).
// Therefore, must use StructuredBuffer in native DXR path.
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
TextureCube radianceTexture : register(t1, space2);

////////////////////////////////////////////////////////////////////////////////
// Ray-gen shader
////////////////////////////////////////////////////////////////////////////////

[shader("raygeneration")] 
void RayGen() 
{
    if (cameraParams.accumCount >= options.maxIterations) {
        return;
    }

    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);
    float2 d = (((launchIndex.xy + 0.5f) / dims.xy) * 2.f - 1.f);
 
    HitInfo payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);
    payload.depth = 0;

    float2 jitter = cameraParams.jitters * 2.0;

    RayDesc ray;
    ray.Origin = cameraParams.worldEyePos.xyz + float3(jitter.x, jitter.y, 0.0f);
    ray.Direction = normalize(d.x * cameraParams.U + (-d.y) * cameraParams.V + cameraParams.W).xyz;
    ray.TMin = 0;
    ray.TMax = RAY_MAX_T;

    TraceRay(SceneBVH, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, ray, payload);

    float4 prevColor = gOutput[launchIndex];
    float4 curColor = float4(pow(payload.colorAndDistance.rgb, 1.0f / 2.2f), 1.0f);
    gOutput[launchIndex] = (cameraParams.accumCount * prevColor + curColor) / (cameraParams.accumCount + 1);
}

////////////////////////////////////////////////////////////////////////////////
// Hit groups
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

float3 shootSecondaryRay(float3 orig, float3 dir, float minT, uint currentDepth)
{
    if (currentDepth >= MAX_RADIANCE_RAY_DEPTH) {
        return float3(0.0, 0.0, 0.0);
    }

    RayDesc ray = { orig, minT, dir, RAY_MAX_T };

    HitInfo payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);
    payload.depth = currentDepth + 1;

    TraceRay(SceneBVH, 0, 0xFF, 0, 0, 2, ray, payload);
    return payload.colorAndDistance.rgb;
}

float evaluateAO(float3 position, float3 normal)
{
    float visibility = 0.0f;
    const int aoRayCount = 4;

    uint2 pixIdx = DispatchRaysIndex().xy;
    uint2 numPix = DispatchRaysDimensions().xy;
    uint randSeed = initRand(pixIdx.x + pixIdx.y * numPix.x, cameraParams.frameCount);

    for (int i = 0; i < aoRayCount; ++i) {
        float3 sampleDir;
        float NoL;
        float pdf;
        if (options.cosineHemisphereSampling) {
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
    float3 L = normalize(-directionalLight.forwardDir.xyz);
    float NoL = saturate(dot(normal, L));

    float visible = shootShadowRay(position, L, RAY_EPSILON, RAY_MAX_T, currentDepth);

    return directionalLight.color.rgb * directionalLight.color.a * NoL * visible;
}

float3 evaluatePointLight(float3 position, float3 normal, uint currentDepth)
{
    float3 lightPath = pointLight.worldPos.xyz - position;
    float lightDistance = length(lightPath);
    float3 L = normalize(lightPath);
    float NoL = saturate(dot(normal, L));

    float visible = shootShadowRay(position, L, RAY_EPSILON, lightDistance, currentDepth);

    float falloff = 1.0 / (2 * M_PI * lightDistance * lightDistance);
    return pointLight.color.rgb * pointLight.color.a * NoL * visible * falloff;
}

float3 evaluateIndirectDiffuse(float3 position, float3 normal, inout uint randSeed, uint currentDepth)
{
    float3 color = 0.0;
    const int rayCount = 1;

    for (int i = 0; i < rayCount; ++i) {
        if (options.cosineHemisphereSampling) {
            float3 sampleDir = getCosHemisphereSample(randSeed, normal);
            // float NoL = saturate(dot(normal, sampleDir));
            // float pdf = NoL / M_PI;
            // color += shootSecondaryRay(position, sampleDir, RAY_EPSILON, currentDepth) * NoL / pdf; 
            color += shootSecondaryRay(position, sampleDir, RAY_EPSILON, currentDepth) * M_PI; // term canceled
        } else {
            float3 sampleDir = getUniformHemisphereSample(randSeed, normal);
            float NoL = saturate(dot(normal, sampleDir));
            float pdf = 1.0 / (2.0 * M_PI); 
            color += shootSecondaryRay(position, sampleDir, RAY_EPSILON, currentDepth) * NoL / pdf;
        }
    }

    return color / float(rayCount);
}

float3 shade(float3 position, float3 normal, uint currentDepth)
{
    if (options.showAmbientOcclusionOnly) {
        return evaluateAO(position, normal);
    }

    // Set up random seeed
    uint2 pixIdx = DispatchRaysIndex().xy;
    uint2 numPix = DispatchRaysDimensions().xy;
    uint randSeed = initRand(pixIdx.x + pixIdx.y * numPix.x, cameraParams.frameCount);

    // Calculate direct diffuse lighting
    float3 directContrib = 0.0;
    if (options.debug == 2) {
        const int numLights = 2;
        // Select light to evaluate in this iteration
        if (nextRand(randSeed) < 0.5) {
            directContrib += evaluateDirectionalLight(position, normal, currentDepth) * numLights;
        } else {
            directContrib += evaluatePointLight(position, normal, currentDepth) * numLights;
        }
    } else {
        directContrib += evaluateDirectionalLight(position, normal, currentDepth);
        directContrib += evaluatePointLight(position, normal, currentDepth);
    }

    // Calculate indirect diffuse
    float3 indirectContrib = 0.0;
    if (currentDepth < 1) {
        indirectContrib += evaluateIndirectDiffuse(position, normal, randSeed, currentDepth);
    }

    float3 diffuseComponent = (directContrib + indirectContrib) / M_PI;

    float fresnel = 0.0;

    // Accumulate indirect specular
    float3 specularComponent = 0.0;
    if (materialParams.type == 1 || materialParams.type == 2) {
        if (materialParams.reflectivity > 0.001) {
            float exponent = exp((1.0 - materialParams.roughness) * 7.0);
            float pdf;
            float brdf;
            float3 mirrorDir = reflect(WorldRayDirection(), normal);
            float3 sampleDir = samplePhongLobe(randSeed, mirrorDir, exponent, pdf, brdf);
            float3 reflectionColor = shootSecondaryRay(position, sampleDir, RAY_EPSILON, currentDepth);
            specularComponent += reflectionColor * brdf / pdf;

            // Equivalent to: fresnel = FresnelReflectanceSchlick(-V, H, materialParams.specular);
            fresnel = FresnelReflectanceSchlick(WorldRayDirection(), normal, materialParams.specular);
        }
    }
 
    // Debug visualization
    if (currentDepth == 0) {
        if (options.showIndirectDiffuseOnly) {
            return materialParams.albedo * indirectContrib / M_PI;
        } else if (options.showIndirectSpecularOnly) {
            return fresnel * materialParams.specular * specularComponent;
        } else if (options.showFresnelTerm) {
            return fresnel;
        }
    }

    return materialParams.emissive.rgb * materialParams.emissive.a + materialParams.albedo * diffuseComponent + fresnel * materialParams.reflectivity * specularComponent;
}

// Hit group 1

[shader("closesthit")] 
void PrimaryClosestHit(inout HitInfo payload, Attributes attrib) 
{
    float3 vertPosition, vertNormal;
    interpolateVertexAttributes(attrib.bary, vertPosition, vertNormal);

    float3 color = shade(HitWorldPosition(), normalize(vertNormal), payload.depth);
    payload.colorAndDistance = float4(color, RayTCurrent());
}

// Hit group 2

[shader("closesthit")]
void ShadowClosestHit(inout ShadowPayload payload, Attributes attrib)
{
    // no-op
}

[shader("anyhit")]
void ShadowAnyHit(inout ShadowPayload payload, Attributes attrib)
{
    // no-op
}

////////////////////////////////////////////////////////////////////////////////
// Miss shader
////////////////////////////////////////////////////////////////////////////////

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

[shader("miss")]
void SecondaryMiss(inout HitInfo payload : SV_RayPayload)
{
    // lat-long environment map
    float2 uv = wsVectorToLatLong(WorldRayDirection().xyz);
    float4 envSample = envMap.SampleLevel(defaultSampler, uv, 0.0);

    payload.colorAndDistance = float4(envSample.rgb * options.environmentStrength, -1.0);
}

#endif // SHADER_LIBRARY_HLSL
