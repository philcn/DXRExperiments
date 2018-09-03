#pragma once

#include "DXSample.h"
#include "DirectXRaytracingHelper.h" // for AccelerationStructureBuffers
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
    void CreateAccelerationStructures();
    AccelerationStructureBuffers CreateBottomLevelAS(std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vertexBuffers);
    AccelerationStructureBuffers CreateTopLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> &instances);

    void DoRaytracing();

    void CreateRaytracingOutputBuffer();
    void CopyRaytracingOutputToBackbuffer();

    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();
    void ReleaseDeviceDependentResources();
    void ReleaseWindowSizeDependentResources();
};
