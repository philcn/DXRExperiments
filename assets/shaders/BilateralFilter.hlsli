#define KERNEL_TAPS 6

// 11 x 11 separated kernel weights.
// Custom disk-like // vs. Gaussian
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
    return (abs(c0.r - c1.r) + abs(c0.g - c1.g) + abs(c0.b - c1.b)) * 3.0;
}

float calcColorWeight(float4 color0, float4 color1)
{
    return 1.0 - clamp(colorDistance(color0.rgb, color1.rgb), 0.0, 1.0);
}

float4 filterKernel(int passId, int kernelMaxSize, float kernelRadius, uint2 pixelCenter, Texture2D inputTex, Texture2D jointTex)
{    
    const int2 kDirection = passId == 0 ? int2(0, 1) : int2(1, 0);

    float4 color = 0.0;
    float weight = 0.0;

    float4 centerColor = sampleOffset(inputTex, pixelCenter, int2(0, 0));
    float4 centerColorJoint = sampleOffset(jointTex, pixelCenter, int2(0, 0));

    [unroll(40)]
    for (int i = -kernelMaxSize; i <= kernelMaxSize; ++i) {
        float4 sampleColor = sampleOffset(inputTex, pixelCenter, kDirection * i);
        // float4 sampleColorJoint = sampleOffset(jointTex, pixelCenter, kDirection * i);

        // float gaussianWeight = kGaussianKernel[clamp(int(float(abs(i) * (KERNEL_TAPS - 1)) / (0.001 + abs(kernelRadius * 0.8))), 0, KERNEL_TAPS)];            
        float gaussianWeight = 1.0;
        // float colorWeight = calcColorWeight(sampleColorJoint, centerColorJoint);
        float bilateralWeight = gaussianWeight;// * colorWeight;
        
        color += sampleColor * bilateralWeight;
        weight += bilateralWeight;
    }

    return color / weight;
}
