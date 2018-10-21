#include "RaytracingCommon.hlsli"

RWTexture2D<float4> gDirectLightingOutput : register(u0);
RWTexture2D<float4> gIndirectSpecularOutput : register(u1);

struct ShadingAOV
{
    float3 albedo;
    float roughness;
    float3 directLighting;
    float3 indirectSpecular;
};

struct RealtimePayload
{
    float3 color;
    float distance;
    ShadingAOV aov;
    uint depth;
};

[shader("raygeneration")] 
void RayGen() 
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);
    float2 d = (((launchIndex.xy + 0.5f) / dims.xy) * 2.f - 1.f);
 
    RealtimePayload payload;
    payload.color = float3(0, 0, 0);
    payload.distance = 0.0;
    payload.depth = 0;

    float2 jitter = perFrameConstants.cameraParams.jitters * 10.0;

    RayDesc ray;
    ray.Origin = perFrameConstants.cameraParams.worldEyePos.xyz + float3(jitter.x, jitter.y, 0.0f);
    ray.Direction = normalize(d.x * perFrameConstants.cameraParams.U + (-d.y) * perFrameConstants.cameraParams.V + perFrameConstants.cameraParams.W).xyz;
    ray.TMin = 0;
    ray.TMax = RAY_MAX_T;

    TraceRay(SceneBVH, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, ray, payload);

    gDirectLightingOutput[launchIndex] = float4(max(payload.aov.directLighting, 0.0), 1.0f);
    gIndirectSpecularOutput[launchIndex] = float4(max(payload.aov.indirectSpecular, 0.0), 1.0f);
}

float3 shootSecondaryRay(float3 orig, float3 dir, float minT, uint currentDepth)
{
    if (currentDepth >= MAX_RADIANCE_RAY_DEPTH) {
        return float3(0.0, 0.0, 0.0);
    }

    RayDesc ray = { orig, minT, dir, RAY_MAX_T };

    RealtimePayload payload;
    payload.color = float3(0, 0, 0);
    payload.distance = 0.0;
    payload.depth = currentDepth + 1;

    TraceRay(SceneBVH, 0, 0xFF, 0, 0, 2, ray, payload);
    return payload.color;
}

float3 shadeAOV(float3 position, float3 normal, uint currentDepth, out ShadingAOV aov)
{
    // Set up random seeed
    uint2 pixIdx = DispatchRaysIndex().xy;
    uint2 numPix = DispatchRaysDimensions().xy;
    uint randSeed = initRand(pixIdx.x + pixIdx.y * numPix.x, perFrameConstants.cameraParams.frameCount);

    // Calculate direct diffuse lighting
    float3 directContrib = 0.0;
    directContrib += evaluateDirectionalLight(position, normal, currentDepth);
    directContrib += evaluatePointLight(position, normal, currentDepth);

    // Accumulate indirect specular
    float fresnel = 0.0;
    float3 specularComponent = 0.0;
    if (materialParams.type == 1 || materialParams.type == 2) {
        if (materialParams.reflectivity > 0.001) {
            float exponent = exp((1.0 - materialParams.roughness) * 12.0);
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

    if (currentDepth == 0) {
        aov.albedo = materialParams.albedo;
        aov.roughness = materialParams.roughness;
        aov.directLighting = materialParams.albedo * directContrib / M_PI;
        aov.indirectSpecular = materialParams.reflectivity * specularComponent * fresnel;
    }

    return materialParams.albedo * directContrib / M_PI + materialParams.reflectivity * specularComponent * fresnel;
}

// Hit group 1
[shader("closesthit")] 
void PrimaryClosestHit(inout RealtimePayload payload, Attributes attrib) 
{
    float3 vertPosition, vertNormal;
    interpolateVertexAttributes(attrib.bary, vertPosition, vertNormal);

    ShadingAOV shadingResult;
    float3 color = shadeAOV(HitWorldPosition(), normalize(vertNormal), payload.depth, shadingResult);

    payload.color = color;
    payload.distance = RayTCurrent();
    payload.aov = shadingResult;
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

[shader("miss")]
void PrimaryMiss(inout RealtimePayload payload : SV_RayPayload)
{
    payload.color = sampleEnvironment();
    payload.distance = -1.0;
    payload.aov.directLighting = payload.color;
    payload.aov.indirectSpecular = 0.0;
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload : SV_RayPayload)
{
    payload.lightVisibility = 1.0;
}

[shader("miss")]
void SecondaryMiss(inout RealtimePayload payload : SV_RayPayload)
{
    payload.color = sampleEnvironment();
    payload.distance = -1.0;
    payload.aov.indirectSpecular = 0.0;
}
