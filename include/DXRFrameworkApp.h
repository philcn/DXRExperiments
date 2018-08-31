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

#pragma once

#include "DXSample.h"
#include "DirectXRaytracingHelper.h"
#include "StepTimer.h"
#include "RaytracingHlslCompat.h"

#include "dxcapi.h"
#include "dxcapi.use.h"
#include "nv_helpers_dx12/TopLevelASGenerator.h"
#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"

#include <vector>

class DXRFrameworkApp : public DXSample
{
public:
    DXRFrameworkApp(UINT width, UINT height, std::wstring name);

    // IDeviceNotify
    virtual void OnDeviceLost() override;
    virtual void OnDeviceRestored() override;

    // Messages
    virtual void OnInit();
    virtual void OnKeyDown(UINT8 key);
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnSizeChanged(UINT width, UINT height, bool minimized);
    virtual void OnDestroy();
    virtual IDXGISwapChain* GetSwapchain() { return m_deviceResources->GetSwapChain(); }

private:
    static const UINT FrameCount = 3;

    bool mUseDXRDriver;
    bool mRaytracingEnabled;

    // Raytracing Fallback Layer (FL) attributes
    ComPtr<ID3D12RaytracingFallbackDevice> mFallbackDevice;
    ComPtr<ID3D12RaytracingFallbackCommandList> mFallbackCommandList;

    // Descriptors
    ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
    UINT mDescriptorsAllocated;
    UINT mDescriptorSize;

    // Geometries
    ComPtr<ID3D12Resource> mVertexBuffer;
    std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> mInstances;

    // Acceleration structures
    ComPtr<ID3D12Resource> mBlasBuffer;
    ComPtr<ID3D12Resource> mTlasBuffer;
    WRAPPED_GPU_POINTER mTlasWrappedPointer;

    // Pipeline
    ComPtr<ID3D12RootSignature> mGlobalRootSignature;
    ComPtr<ID3D12RootSignature> mRayGenSignature;
    ComPtr<ID3D12RootSignature> mHitSignature;
    ComPtr<ID3D12RootSignature> mMissSignature;
    ComPtr<ID3D12RaytracingFallbackStateObject> mFallbackStateObject;

    // Raytracing output resources
    ComPtr<ID3D12Resource> mOutputResource;
    D3D12_GPU_DESCRIPTOR_HANDLE mOutputResourceUAVGpuDescriptor;

    // Shader table
    nv_helpers_dx12::ShaderBindingTableGenerator mShaderTableGenerator;
    ComPtr<ID3D12Resource> mShaderTable;

    // Application state
    StepTimer mTimer;

    void InitializeScene();

    void CreateRaytracingInterfaces();
    void CreateDescriptorHeap();
    void CreateGeometries();

    AccelerationStructureBuffers CreateBottomLevelAS(std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vertexBuffers);
    AccelerationStructureBuffers CreateTopLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> &instances);
    void CreateAccelerationStructures();

    void CreateGlobalRootSignature();
    ComPtr<ID3D12RootSignature> CreateRayGenSignature();
    ComPtr<ID3D12RootSignature> CreateMissSignature();
    ComPtr<ID3D12RootSignature> CreateHitSignature();

    void CreateRaytracingPipeline();
    void CreateShaderBindingTable();

    void CreateRaytracingOutputBuffer();

    void DoRaytracing();
    void CopyRaytracingOutputToBackbuffer();

    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();
    void ReleaseDeviceDependentResources();
    void ReleaseWindowSizeDependentResources();

    void CalculateFrameStats();
    void SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig);
    UINT AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse = UINT_MAX);
    WRAPPED_GPU_POINTER CreateFallbackWrappedPointer(ID3D12Resource* resource, UINT bufferNumElements);
    void EnableDXRExperimentalFeatures(IDXGIAdapter1* adapter);
};
