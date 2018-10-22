#include "RaytracingCommon.hlsli"

RWTexture2D<float4> gOutput : register(u0);

struct SimplePayload
{
    XMFLOAT4 colorAndDistance;
    UINT depth;
};

[shader("raygeneration")] 
void RayGen() 
{
    if (perFrameConstants.cameraParams.accumCount >= perFrameConstants.options.maxIterations) {
        return;
    }

    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);
    float2 d = (((launchIndex.xy + 0.5f) / dims.xy) * 2.f - 1.f);
 
    SimplePayload payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);
    payload.depth = 0;

    float2 jitter = perFrameConstants.cameraParams.jitters * 30.0;

    RayDesc ray;
    ray.Origin = perFrameConstants.cameraParams.worldEyePos.xyz + float3(jitter.x, jitter.y, 0.0f);
    ray.Direction = normalize(d.x * perFrameConstants.cameraParams.U + (-d.y) * perFrameConstants.cameraParams.V + perFrameConstants.cameraParams.W).xyz;
    ray.TMin = 0;
    ray.TMax = RAY_MAX_T;

    TraceRay(SceneBVH, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, ray, payload);

    float4 prevColor = gOutput[launchIndex];
    float4 curColor = float4(max(payload.colorAndDistance.rgb, 0.0), 1.0f);
    gOutput[launchIndex] = (perFrameConstants.cameraParams.accumCount * prevColor + curColor) / (perFrameConstants.cameraParams.accumCount + 1);
}

float3 shootSecondaryRay(float3 orig, float3 dir, float minT, uint currentDepth)
{
    if (currentDepth >= MAX_RADIANCE_RAY_DEPTH) {
        return float3(0.0, 0.0, 0.0);
    }

    RayDesc ray = { orig, minT, dir, RAY_MAX_T };

    SimplePayload payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);
    payload.depth = currentDepth + 1;

    TraceRay(SceneBVH, 0, 0xFF, 0, 0, 0, ray, payload);
    return payload.colorAndDistance.rgb;
}

float3 evaluateIndirectDiffuse(float3 position, float3 normal, inout uint randSeed, uint currentDepth)
{
    float3 color = 0.0;
    const int rayCount = 1;

    for (int i = 0; i < rayCount; ++i) {
        if (perFrameConstants.options.cosineHemisphereSampling) {
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
    if (perFrameConstants.options.showAmbientOcclusionOnly) {
        return evaluateAO(position, normal);
    }

    // Set up random seeed
    uint2 pixIdx = DispatchRaysIndex().xy;
    uint2 numPix = DispatchRaysDimensions().xy;
    uint randSeed = initRand(pixIdx.x + pixIdx.y * numPix.x, perFrameConstants.cameraParams.frameCount);

    // Calculate direct diffuse lighting
    float3 directContrib = 0.0;
    if (perFrameConstants.options.debug == 2) {
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
    if (currentDepth < 1 && !perFrameConstants.options.noIndirectDiffuse) {
        indirectContrib += evaluateIndirectDiffuse(position, normal, randSeed, currentDepth);
    }

    float3 diffuseComponent = (directContrib + indirectContrib) / M_PI;

    // Accumulate indirect specular
    float3 fresnel = 0.0;
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

            // Equivalent to: fresnel = FresnelReflectanceSchlick(-V, H, materialParams.specular.rgb);
            fresnel = FresnelReflectanceSchlick(WorldRayDirection(), normal, materialParams.specular.rgb);
        }
    }
 
    // Debug visualization
    if (currentDepth == 0) {
        if (perFrameConstants.options.showIndirectDiffuseOnly) {
            return materialParams.albedo.rgb * indirectContrib / M_PI;
        } else if (perFrameConstants.options.showIndirectSpecularOnly) {
            return materialParams.reflectivity * specularComponent * fresnel;
        } else if (perFrameConstants.options.showFresnelTerm) {
            return fresnel;
        } else if (perFrameConstants.options.showGBufferAlbedoOnly) {
            return materialParams.albedo.rgb;
        } else if (perFrameConstants.options.showDirectLightingOnly) {
            return materialParams.albedo.rgb * directContrib / M_PI;
        }
    }

    return materialParams.emissive.rgb * materialParams.emissive.a + materialParams.albedo.rgb * diffuseComponent + materialParams.reflectivity * specularComponent * fresnel;
}

[shader("closesthit")] 
void PrimaryClosestHit(inout SimplePayload payload, Attributes attrib) 
{
    float3 vertPosition, vertNormal;
    interpolateVertexAttributes(attrib.bary, vertPosition, vertNormal);

    float3 color = shade(HitWorldPosition(), normalize(vertNormal), payload.depth);
    payload.colorAndDistance = float4(color, RayTCurrent());
}

[shader("miss")]
void PrimaryMiss(inout SimplePayload payload)
{
    payload.colorAndDistance = float4(sampleEnvironment(), -1.0);
}

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
void ShadowMiss(inout ShadowPayload payload)
{
    payload.lightVisibility = 1.0;
}
