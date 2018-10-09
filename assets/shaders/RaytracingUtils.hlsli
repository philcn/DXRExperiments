/**********************************************************************************************************************
# Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
# following conditions are met:
#  * Redistributions of code must retain the copyright notice, this list of conditions and the following disclaimer.
#  * Neither the name of NVIDIA CORPORATION nor the names of its contributors may be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT
# SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
# OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************************************************************/

#define M_PI 3.1415927
#define M_1_PI (1.0 / M_PI)

// Generates a seed for a random number generator from 2 inputs plus a backoff
uint initRand(uint val0, uint val1, uint backoff = 16)
{
    uint v0 = val0, v1 = val1, s0 = 0;

    [unroll]
    for (uint n = 0; n < backoff; n++)
    {
        s0 += 0x9e3779b9;
        v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
        v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
    }
    return v0;
}

// Takes our seed, updates it, and returns a pseudorandom float in [0..1]
float nextRand(inout uint s)
{
    s = (1664525u * s + 1013904223u);
    return float(s & 0x00FFFFFF) / float(0x01000000);
}

// Utility function to get a vector perpendicular to an input vector 
//    (from "Efficient Construction of Perpendicular Vectors Without Branching")
float3 getPerpendicularVector(float3 u)
{
    float3 a = abs(u);
    uint xm = ((a.x - a.y)<0 && (a.x - a.z)<0) ? 1 : 0;
    uint ym = (a.y - a.z)<0 ? (1 ^ xm) : 0;
    uint zm = 1 ^ (xm | ym);
    return cross(u, float3(xm, ym, zm));
}

// Get a cosine-weighted random vector centered around a specified normal direction.
float3 getCosHemisphereSample(inout uint randSeed, float3 hitNorm)
{
    // Get 2 random numbers to select our sample with
    float2 randVal = float2(nextRand(randSeed), nextRand(randSeed));

    float3 bitangent = getPerpendicularVector(hitNorm);
    float3 tangent = cross(bitangent, hitNorm);

    // Uniformly sample disk
    float r = sqrt(randVal.x);
    float phi = 2.0f * 3.14159265f * randVal.y;

    // Projecct up to hemisphere
    float x = r * cos(phi);
    float z = r * sin(phi);
    float y = sqrt(1.0 - randVal.x);
    return x * tangent + y * hitNorm.xyz + z * bitangent;

    // Simplified math:
    // return tangent * (r * cos(phi).x) + bitangent * (r * sin(phi)) + hitNorm.xyz * sqrt(1 - randVal.x);
}

// http://www.rorydriscoll.com/2009/01/07/better-sampling/
float3 getUniformHemisphereSample(inout uint randSeed, float3 hitNorm)
{
    // Get 2 random numbers to select our sample with
    float2 randVal = float2(nextRand(randSeed), nextRand(randSeed));

    float3 bitangent = getPerpendicularVector(hitNorm);
    float3 tangent = cross(bitangent, hitNorm);

    float cosTheta = randVal.x;
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
    float phi = 2.0f * 3.14159265f * randVal.y;

    float x = sinTheta * cos(phi);
    float z = sinTheta * sin(phi);
    float y = cosTheta;
    return x * tangent + y * hitNorm.xyz + z * bitangent;
}

// From OptiX helpers.h
float3 samplePhongLobe(inout uint randSeed, float3 mirrorDir, float exponent, inout float pdf, inout float brdf)
{
    const float pi = 3.14159265f;

    // Get 2 random numbers to select our sample with
    float2 randVal = float2(nextRand(randSeed), nextRand(randSeed));

    float3 bitangent = getPerpendicularVector(mirrorDir);
    float3 tangent = cross(bitangent, mirrorDir);

    float cosTheta = pow(randVal.x, 1.0 / (exponent + 1.0));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float phi = 2.0f * pi * randVal.y;

    float poweredCos = pow(cosTheta, exponent);
    pdf = (exponent + 1.0) / (2.0 * pi) * poweredCos;
    brdf = (exponent + 2.0) / (2.0 * pi) * poweredCos;  

    float x = sinTheta * cos(phi);
    float z = sinTheta * sin(phi);
    float y = cosTheta;
    return x * tangent + y * mirrorDir + z * bitangent;
}

// Fresnel reflectance - schlick approximation.
float3 FresnelReflectanceSchlick(in float3 I, in float3 N, in float3 f0)
{
    float cosi = saturate(dot(-I, N));
    return f0 + (1 - f0)*pow(1 - cosi, 5);
}

/**
*  Calculates refraction direction
*  r   : refraction vector
*  i   : incident vector
*  n   : surface normal
*  ior : index of refraction ( n2 / n1 )
*  returns false in case of total internal reflection, in that case r is
*          initialized to (0,0,0).
*/
bool refract(inout float3 r, float3 i, float3 n, float ior)
{
    float3 nn = n;
    float negNdotV = dot(i, nn);
    float eta;

    if (negNdotV > 0.0) {
        eta = ior;
        nn = -n;
        negNdotV = -negNdotV;
    } else {
        eta = 1.0 / ior;
    }

    const float k = 1.0 - eta * eta * (1.0 - negNdotV * negNdotV);

    if (k < 0.0) {
        // Initialize this value, so that r always leaves this function initialized.
        r = float3(0.0, 0.0, 0.0);
        return false;
    } else {
        r = normalize(eta*i - (eta * negNdotV + sqrt(k)) * nn);
        return true;
    }
}

// Load three 16 bit indices from a byte addressed buffer.
uint3 Load3x16BitIndices(uint offsetBytes, ByteAddressBuffer Indices)
{
    uint3 indices;

    // ByteAdressBuffer loads must be aligned at a 4 byte boundary.
    // Since we need to read three 16 bit indices: { 0, 1, 2 } 
    // aligned at a 4 byte boundary as: { 0 1 } { 2 0 } { 1 2 } { 0 1 } ...
    // we will load 8 bytes (~ 4 indices { a b | c d }) to handle two possible index triplet layouts,
    // based on first index's offsetBytes being aligned at the 4 byte boundary or not:
    //  Aligned:     { 0 1 | 2 - }
    //  Not aligned: { - 0 | 1 2 }
    const uint dwordAlignedOffset = offsetBytes & ~3;
    const uint2 four16BitIndices = Indices.Load2(dwordAlignedOffset);

    // Aligned: { 0 1 | 2 - } => retrieve first three 16bit indices
    if (dwordAlignedOffset == offsetBytes)
    {
        indices.x = four16BitIndices.x & 0xffff;
        indices.y = (four16BitIndices.x >> 16) & 0xffff;
        indices.z = four16BitIndices.y & 0xffff;
    }
    else // Not aligned: { - 0 | 1 2 } => retrieve last three 16bit indices
    {
        indices.x = (four16BitIndices.x >> 16) & 0xffff;
        indices.y = four16BitIndices.y & 0xffff;
        indices.z = (four16BitIndices.y >> 16) & 0xffff;
    }

    return indices;
}

// Load three 32 bit indices from a byte addressed buffer.
uint3 Load3x32BitIndices(uint offsetBytes, ByteAddressBuffer Indices)
{
    // ByteAdressBuffer loads must be aligned at a 4 byte boundary.
    const uint dwordAlignedOffset = offsetBytes & ~3;
    const uint3 three32BitIndices = Indices.Load3(dwordAlignedOffset);
    return three32BitIndices;
}

// Retrieve hit world position.
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

float2 wsVectorToLatLong(float3 dir)
{
    float3 p = normalize(dir);
    float u = (1.f + atan2(p.x, -p.z) * M_1_PI) * 0.5f;
    float v = acos(p.y) * M_1_PI;
    return float2(u, v);
}
