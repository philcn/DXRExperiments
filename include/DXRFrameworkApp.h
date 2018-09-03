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
#include "RaytracingHlslCompat.h"

#include "dxcapi.h"
#include "dxcapi.use.h"

#include "DXRFramework/RtContext.h"
#include "DXRFramework/RtProgram.h"
#include "DXRFramework/RtState.h"
#include "DXRFramework/RtBindings.h"

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

    ////////////////////////////////////////////////////////////////////////////////

    // Geometries
    ComPtr<ID3D12Resource> mVertexBuffer;
    std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> mInstances;

    // Acceleration structures
    ComPtr<ID3D12Resource> mBlasBuffer;
    ComPtr<ID3D12Resource> mTlasBuffer;
    WRAPPED_GPU_POINTER mTlasWrappedPointer;

    // Raytracing output resources
    ComPtr<ID3D12Resource> mOutputResource;
    D3D12_GPU_DESCRIPTOR_HANDLE mOutputResourceUAVGpuDescriptor;

    ////////////////////////////////////////////////////////////////////////////////

    DXRFramework::RtContext::SharedPtr mRtContext;
    DXRFramework::RtProgram::SharedPtr mRtProgram;
    DXRFramework::RtState::SharedPtr mRtState;
    DXRFramework::RtBindings::SharedPtr mRtBindings;

    ////////////////////////////////////////////////////////////////////////////////

    void CreateGeometries();

    AccelerationStructureBuffers CreateBottomLevelAS(std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vertexBuffers);
    AccelerationStructureBuffers CreateTopLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> &instances);
    void CreateAccelerationStructures();

    void DoRaytracing();

    void CreateRaytracingOutputBuffer();
    void CopyRaytracingOutputToBackbuffer();

    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();
    void ReleaseDeviceDependentResources();
    void ReleaseWindowSizeDependentResources();
};
