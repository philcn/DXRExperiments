//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#ifndef RAYTRACINGHLSLCOMPAT_H
#define RAYTRACINGHLSLCOMPAT_H

// Workaround for NV driver not supporting null local root signatures. 
// Use an empty local root signature where a shader does not require it.
#define USE_NON_NULL_LOCAL_ROOT_SIG 1

#ifdef HLSL
#include "HlslCompat.h"
#else
using namespace DirectX;

// Shader will use byte encoding to access indices.
typedef UINT16 Index;
#endif

// Hit information, aka ray payload
// This sample only carries a shading color and hit distance.
// Note that the payload should be kept as small as possible,
// and that its size must be declared in the corresponding
// D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.
struct HitInfo
{
    XMFLOAT4 colorAndDistance;
    UINT depth;
};

struct ShadowPayload
{
    float lightVisibility;
};

// Attributes output by the raytracing when hitting a surface,
// here the barycentric coordinates
struct Attributes
{
    XMFLOAT2 bary;
};

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
};

struct CameraParams
{
    XMFLOAT4 worldEyePos;
    XMFLOAT4 U;
    XMFLOAT4 V;
    XMFLOAT4 W;
    XMFLOAT2 jitters;
    UINT frameCount;
    UINT accumCount;
};

struct DirectionalLightParams
{
    XMFLOAT4 forwardDir;
    XMFLOAT4 color;
};

struct PointLightParams
{
    XMFLOAT4 worldPos;
    XMFLOAT4 color;
};

struct DebugOptions
{
    UINT maxIterations;
    UINT cosineHemisphereSampling;
    UINT showIndirectDiffuseOnly;
    UINT showIndirectSpecularOnly;
    UINT showAmbientOcclusionOnly;
    UINT showGBufferAlbedoOnly;
    UINT showDirectLightingOnly;
    UINT showReflectionDenoiseGuide;
    UINT showFresnelTerm;
    float environmentStrength;
    UINT debug;
};

struct PerFrameConstants
{
    CameraParams cameraParams;
    DirectionalLightParams directionalLight;
    PointLightParams pointLight;
    DebugOptions options;
};

struct MaterialParams
{
    XMFLOAT4 albedo;
    XMFLOAT4 specular;
    XMFLOAT4 emissive;
    float reflectivity;
    float roughness;
    float IoR;
    UINT type; // 0: diffuse, 1: glossy, 2: specular (glass)
};

#endif // RAYTRACINGHLSLCOMPAT_H