#pragma once

#include "DXSample.h"
#include "DirectXRaytracingHelper.h" // for AccelerationStructureBuffers
#include "DXRFramework/RtContext.h"
#include "DXRFramework/RtProgram.h"
#include "DXRFramework/RtState.h"
#include "DXRFramework/RtBindings.h"
#include "DXRFramework/RtScene.h"
#include "RaytracingHlslCompat.h"
#include "Camera.h"
#include "CameraController.h"
#include <vector>
#include <random>

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
    bool mFrameAccumulationEnabled;
    bool mAnimationPaused;
    DebugOptions mShaderDebugOptions;

    ////////////////////////////////////////////////////////////////////////////////

    Math::Camera mCamera;
    std::shared_ptr<GameCore::CameraController> mCamController;
    Math::Matrix4 mLastCameraVPMatrix;
    UINT mAccumCount;

    std::mt19937 mRng;
    std::uniform_real_distribution<float> mRngDist;     

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

    bool HasCameraMoved();
    void ResetAccumulation();
};
