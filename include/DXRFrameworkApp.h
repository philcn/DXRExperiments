#pragma once

#include "DXSample.h"
#include "DirectXRaytracingHelper.h" // for AccelerationStructureBuffers
#include "DXRFramework/RtContext.h"
#include "DXRFramework/RtProgram.h"
#include "DXRFramework/RtState.h"
#include "DXRFramework/RtBindings.h"
#include "DXRFramework/RtScene.h"
#include <vector>

class DXRFrameworkApp : public DXSample
{
public:
    DXRFrameworkApp(UINT width, UINT height, std::wstring name);

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

    ComPtr<ID3D12Resource> mOutputResource;
    D3D12_GPU_DESCRIPTOR_HANDLE mOutputResourceUAVGpuDescriptor;

    UINT mAlignedPerFrameConstantBufferSize;
    ComPtr<ID3D12Resource> mPerFrameConstantBuffer;
    D3D12_GPU_DESCRIPTOR_HANDLE mPerFrameCBVGpuHandle;
    void *mMappedPerFrameConstantsData;

    ////////////////////////////////////////////////////////////////////////////////

    DXRFramework::RtContext::SharedPtr mRtContext;
    DXRFramework::RtProgram::SharedPtr mRtProgram;
    DXRFramework::RtState::SharedPtr mRtState;
    DXRFramework::RtBindings::SharedPtr mRtBindings;
    DXRFramework::RtScene::SharedPtr mRtScene;

    ////////////////////////////////////////////////////////////////////////////////

    void InitRaytracing();
    void BuildAccelerationStructures();

    void DoRaytracing();

    void CreateRaytracingOutputBuffer();
    void CopyRaytracingOutputToBackbuffer();

    void CreateConstantBuffers();
    void UpdatePerFrameConstants(float elapsedTime);
};
