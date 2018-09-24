#ifndef SHADER_LIBRARY_HLSL
#define SHADER_LIBRARY_HLSL

#define HLSL
#include "RaytracingHlslCompat.h"

#include "RaytracingUtils.hlsli"

#define M_PI 3.1415927
#define M_1_PI (1.0 / M_PI)

#define kRayMaxT 1.0e+38f
#define kRayBias 0.0001

////////////////////////////////////////////////////////////////////////////////
// Global root signature
////////////////////////////////////////////////////////////////////////////////

RWTexture2D<float4> gOutput : register(u0);
RaytracingAccelerationStructure SceneBVH : register(t0);
cbuffer PerFrameConstants : register(b0)
{
    CameraParams cameraParams;
    DirectionalLightParams directionalLight;
    PointLightParams pointLight;
    DebugOptions options;
    uint frameCount;
    uint accumCount;
}

SamplerState defaultSampler : register(s0);

////////////////////////////////////////////////////////////////////////////////
// Hit-group local root signature
////////////////////////////////////////////////////////////////////////////////

// StructuredBuffer<Vertex> vertexBuffer : register(t0, space1); // doesn't work in Fallback Layer
Buffer<float3> vertexData : register(t0, space1);
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
    if (accumCount >= options.maxIterations) {
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
    ray.TMax = kRayMaxT;

    TraceRay(SceneBVH, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, ray, payload);

    float4 prevColor = gOutput[launchIndex];
    float4 curColor = float4(pow(payload.colorAndDistance.rgb, 1.0f / 2.2f), 1.0f);
    gOutput[launchIndex] = (accumCount * prevColor + curColor) / (accumCount + 1);
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

float shootShadowRay(float3 orig, float3 dir, float minT, float maxT)
{
    RayDesc ray = { orig, minT, dir, maxT };

    ShadowPayload payload = { 0.0 };

    TraceRay(SceneBVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 1, 0, 1, ray, payload);
    return payload.lightVisibility;
}

float3 shootReflectionRay(float3 orig, float3 dir, float minT, uint currentDepth)
{
    RayDesc ray = { orig, minT, dir, kRayMaxT };

    HitInfo payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);
    payload.depth = currentDepth + 1;

    TraceRay(SceneBVH, 0, 0xFF, 0, 0, 0, ray, payload);

    float3 reflectionColor = payload.colorAndDistance.rgb;

    float hitT = payload.colorAndDistance.a;
    float falloff = max(1, (hitT * hitT));

    return hitT == -1 ? reflectionColor : reflectionColor * saturate(2.5 / falloff);
}

float3 shootSecondaryRay(float3 orig, float3 dir, float minT)
{
    RayDesc ray = { orig, minT, dir, 1.0e+38f };

    HitInfo payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);
    payload.depth = 1;

    TraceRay(SceneBVH, 0, 0xFF, 0, 0, 2, ray, payload);
    return payload.colorAndDistance.rgb;
}

float evaluateAO(float3 position, float3 normal)
{
    float visibility = 0.0f;
    const int aoRayCount = 4;

    uint2 pixIdx = DispatchRaysIndex().xy;
    uint2 numPix = DispatchRaysDimensions().xy;
    uint randSeed = initRand(pixIdx.x + pixIdx.y * numPix.x, frameCount);

    for (int i = 0; i < aoRayCount; ++i) {
        if (options.cosineHemisphereSampling) {
            float3 sampleDir = getCosHemisphereSample(randSeed, normal);
            float NoL = saturate(dot(normal, sampleDir));
            float pdf = NoL / M_PI;
            visibility += shootShadowRay(position, sampleDir, kRayBias, 10.0) * NoL / pdf;
        } else {
            float3 sampleDir = getUniformHemisphereSample(randSeed, normal);
            float NoL = saturate(dot(normal, sampleDir));
            float pdf = 1.0 / (2.0 * M_PI);
            visibility += shootShadowRay(position, sampleDir, kRayBias, 10.0) * NoL / pdf;
        }
    }

    return visibility / float(aoRayCount);
}

float3 evaluateDirectionalLight(float3 position, float3 normal, uint currentDepth)
{
    float3 L = normalize(-directionalLight.forwardDir.xyz);
    float NoL = saturate(dot(normal, L));

    float visible = 1.0;
    if (currentDepth < 2) {
        visible = shootShadowRay(position, L, kRayBias, kRayMaxT);
    }

    return directionalLight.color.rgb * directionalLight.color.a * NoL * visible;
}

float3 evaluatePointLight(float3 position, float3 normal, uint currentDepth)
{
    float3 lightPath = pointLight.worldPos.xyz - position;
    float lightDistance = length(lightPath);
    float3 L = normalize(lightPath);
    float NoL = saturate(dot(normal, L));

    float visible = 1.0;
    if (currentDepth < 2) {
        visible = shootShadowRay(position, L, kRayBias, lightDistance);
    }

    float falloff = 1.0 / (2 * M_PI * lightDistance * lightDistance);
    return pointLight.color.rgb * pointLight.color.a * NoL * visible * falloff;
}

float3 evaluateIndirectDiffuse(float3 position, float3 normal, inout uint randSeed)
{
    float3 color = 0.0;
    const int rayCount = 2;

    for (int i = 0; i < rayCount; ++i) {
        if (options.cosineHemisphereSampling) {
            float3 sampleDir = getCosHemisphereSample(randSeed, normal);
            // float NoL = saturate(dot(normal, sampleDir));
            // float pdf = NoL / M_PI;
            // color += shootSecondaryRay(position, sampleDir, kRayBias) * NoL / pdf; 
            color += shootSecondaryRay(position, sampleDir, kRayBias) * M_PI; // term canceled
        } else {
            float3 sampleDir = getUniformHemisphereSample(randSeed, normal);
            float NoL = saturate(dot(normal, sampleDir));
            float pdf = 1.0 / (2.0 * M_PI); 
            color += shootSecondaryRay(position, sampleDir, kRayBias) * NoL / pdf;
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
    uint randSeed = initRand(pixIdx.x + pixIdx.y * numPix.x, frameCount);

    // Calculate direct diffuse lighting
    float3 directContrib = 0.0;
    if (options.reduceSamplesPerIteration) {
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
        indirectContrib += evaluateIndirectDiffuse(position, normal, randSeed);
    }

    float3 diffuseComponent = (directContrib + indirectContrib) / M_PI;

    // Accumulate indirect specular
    float3 specularComponent = 0.0;
    if (materialParams.type == 1 || materialParams.type == 2) {
        if (currentDepth < 1) {
            float exponent = exp((1.0 - materialParams.roughness) * 10.0);
            float pdf;
            float brdf;
            float3 mirrorDir = reflect(WorldRayDirection(), normal);
            float3 sampleDir = samplePhongLobe(randSeed, mirrorDir, exponent, pdf, brdf);
            float3 reflectionColor = shootReflectionRay(position, sampleDir, kRayBias, currentDepth);
            specularComponent += reflectionColor * materialParams.reflectivity * brdf / pdf;
        }
    }
 
    // Debug display
    if (currentDepth == 0 && options.showIndirectDiffuseOnly) {
        return materialParams.albedo * indirectContrib / M_PI;
    } else if (currentDepth == 0 && options.showIndirectSpecularOnly) {
        return materialParams.specular * specularComponent;
    }

    return materialParams.albedo * diffuseComponent + materialParams.specular * specularComponent;
}

// Hit group 1

[shader("closesthit")] 
void PrimaryClosestHit(inout HitInfo payload, Attributes attrib) 
{
    float3 vertPosition, vertNormal;
    interpolateVertexAttributes(attrib.bary, vertPosition, vertNormal);

    float3 hitPosition = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    float3 color = shade(hitPosition, normalize(vertNormal), payload.depth);
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

    payload.colorAndDistance = float4(envSample.rgb * 4.0, -1.0);
}

#endif // SHADER_LIBRARY_HLSL
