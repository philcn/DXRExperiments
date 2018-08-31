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

#endif // RAYTRACINGHLSLCOMPAT_H