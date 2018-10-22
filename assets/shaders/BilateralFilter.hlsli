#ifndef BILATERAL_FILTER_HLSLI
#define BILATERAL_FILTER_HLSLI

#if PASS == 0
static const int2 kDirection = int2(1, 0);
inline uint flattenIndex(uint2 id) { return id.x; }
#else
static const int2 kDirection = int2(0, 1);
inline uint flattenIndex(uint2 id) { return id.y; }
#endif

// 11 x 11 separated kernel weights.
// Custom disk-like // vs. Gaussian
#define KERNEL_TAPS 6
static const float kGaussianKernel[KERNEL_TAPS + 1] = {
    1.00, // 0.13425804976814
    1.00, // 0.12815541114232
    0.90, // 0.11143948794984;
    0.75, // 0.08822292796029;
    0.60, // 0.06352050813141;
    0.50, // 0.04153263993208;
    0.00  // 0.00000000000000;  // Weight applied to outside-radius values
};

float4 sampleOffset(Texture2D tex, uint2 sampleLocation, int2 offset)
{
    return tex[sampleLocation + offset];
}

float colorDistance(float3 c0, float3 c1)
{
    return (abs(c0.r - c1.r) + abs(c0.g - c1.g) + abs(c0.b - c1.b)) * 10.0;
}

float calcColorWeight(float4 color0, float4 color1)
{
    return 1.0 - clamp(colorDistance(color0.rgb, color1.rgb), 0.0, 1.0);
}

#define PREFETCH_TEXTURES 1

#if PREFETCH_TEXTURES
#define MAX_EXTENT 20
#define CACHE_SIZE (GROUP_WIDTH + MAX_EXTENT * 2)

groupshared float4 sInputSamples[CACHE_SIZE];
groupshared float4 sJointSamples[CACHE_SIZE];
groupshared float sPrecalculatedGaussianWeights[MAX_EXTENT * 2 + 1];

void fillSharedMemory(Texture2D inputTex, Texture2D jointTex, uint2 threadID, uint2 dispatchID)
{
    uint tid = flattenIndex(threadID.xy);

    sInputSamples[MAX_EXTENT + tid] = inputTex[dispatchID.xy];
    sJointSamples[MAX_EXTENT + tid] = jointTex[dispatchID.xy];    uint readOffset = tid < GROUP_WIDTH / 2 ? (-GROUP_WIDTH / 2) : (GROUP_WIDTH / 2);    uint writeOffset = readOffset + MAX_EXTENT;    sInputSamples[clamp(tid + writeOffset, 0, CACHE_SIZE)] = inputTex[dispatchID.xy + readOffset * kDirection];    sJointSamples[clamp(tid + writeOffset, 0, CACHE_SIZE)] = jointTex[dispatchID.xy + readOffset * kDirection];}

float4 cachedTextureFetch(uint2 threadID, int2 offset)
{
    return sInputSamples[MAX_EXTENT + flattenIndex(int2(threadID) + offset)];
}

float4 cachedJointTextureFetch(uint2 threadID, int2 offset)
{
    return sJointSamples[MAX_EXTENT + flattenIndex(int2(threadID) + offset)];
}
#endif

float4 filterKernel(int kernelMaxSize, float kernelRadius, uint2 pixelCenter, Texture2D inputTex, Texture2D jointTex, uint2 threadID, uint2 dispatchID)
{    
#if PREFETCH_TEXTURES
    fillSharedMemory(inputTex, jointTex, threadID, dispatchID);
#endif
    
    if (flattenIndex(threadID.xy) == 0) {
        for (int i = -MAX_EXTENT; i <= MAX_EXTENT; ++i) {
            int idx = clamp(int(float(abs(i) * (KERNEL_TAPS - 1)) / (0.001 + abs(kernelRadius * 0.8))), 0, KERNEL_TAPS);
            float weight = idx < 2 ? 1.0 : (idx < 3 ? 0.9 : (idx < 4 ? 0.75 : (idx < 5 ? 0.6 : (idx < 6 ? 0.5 : 0.0))));

            // const float kGaussianWeights[7] = { 1.0, 1.0, 0.9, 0.75, 0.6, 0.5, 0.0 };
            // float weight = kGaussianWeights[idx];

            sPrecalculatedGaussianWeights[i + MAX_EXTENT] = weight;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    float4 color = 0.0;
    float weight = 0.0;

    float4 centerColor = sampleOffset(inputTex, pixelCenter, int2(0, 0));
    float4 centerColorJoint = sampleOffset(jointTex, pixelCenter, int2(0, 0));

    for (int i = -kernelMaxSize; i <= kernelMaxSize; ++i) {
#if PREFETCH_TEXTURES
        float4 sampleColor = cachedTextureFetch(threadID, kDirection * i);
        float4 sampleColorJoint = cachedJointTextureFetch(threadID, kDirection * i);
#else
        float4 sampleColor = sampleOffset(inputTex, pixelCenter, kDirection * i);
        float4 sampleColorJoint = sampleOffset(jointTex, pixelCenter, kDirection * i);
#endif

        float gaussianWeight = sPrecalculatedGaussianWeights[i + MAX_EXTENT];
        float colorWeight = calcColorWeight(sampleColorJoint, centerColorJoint);
        float bilateralWeight = gaussianWeight * colorWeight;
        color += sampleColor * bilateralWeight;
        weight += bilateralWeight;
    }

    return color / weight;
}

#endif // BILATERAL_FILTER_HLSLI
