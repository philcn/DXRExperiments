#ifndef DENOISE_COMMON_HLSLI
#define DENOISE_COMMON_HLSLI

#define GROUP_WIDTH 64
#include "BilateralFilter.hlsli"

#if PASS == 0
#define GROUP_SIZE_X GROUP_WIDTH
#define GROUP_SIZE_Y 1
#else
#define GROUP_SIZE_X 1
#define GROUP_SIZE_Y GROUP_WIDTH
#endif

Texture2D<float4> gDirectLighting : register(t0);
Texture2D<float4> gInput : register(t1);
RWTexture2D<float4> gOutput : register(u0);

cbuffer Params : register(b0)
{
    float gExposure;
    float gGamma;
    uint gTonemap;
    uint gGammaCorrect;
    int gMaxKernelSize;
    uint gDebugVisualize;
}

float calcLuminance(float3 color)
{
    return dot(color.xyz, float3(0.299f, 0.587f, 0.114f));
}

float3 reinhardToneMap(float3 color)
{
    float luminance = calcLuminance(color);
    float reinhard = luminance / (luminance + 1);
    return color * (reinhard / luminance);
}

float3 linearToSRGB(float3 color)
{
    return pow(color, 1.0 / gGamma);
}

[numthreads(GROUP_SIZE_X, GROUP_SIZE_Y, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID, uint3 threadID : SV_GroupThreadID, uint3 groupID : SV_GroupID)
{
    float3 color;
    if (gDebugVisualize == 2) {
        color = gInput[dispatchID.xy].rgb;
    } else {
        color = filterKernel(gMaxKernelSize, float(gMaxKernelSize), dispatchID.xy, gInput, gDirectLighting, threadID.xy, dispatchID.xy).rgb;
    }

#if PASS == 1
    if (gDebugVisualize == 0) {
        color += gDirectLighting[dispatchID.xy].rgb;
    } else if (gDebugVisualize == 1) {
        // no-op
    } else if (gDebugVisualize == 2) {
        // no-op
    } else if (gDebugVisualize == 3) {
        color = gDirectLighting[dispatchID.xy].rgb;
    }

    color *= gExposure;
    if (gTonemap) {
        color = max(reinhardToneMap(color), 0.0);
    }
    if (gGammaCorrect) {
        color = saturate(linearToSRGB(color));
    }
#endif

    gOutput[dispatchID.xy] = float4(color, 1.0);
}

#endif // DENOISE_COMMON_HLSLI
