#pragma once

#include "DXSample.h"
#include "DXRFramework/RtBindings.h"
#include "DXRFramework/RtContext.h"
#include "DXRFramework/RtProgram.h"
#include "DXRFramework/RtRenderer.h"
#include "DXRFramework/RtScene.h"
#include "DXRFramework/RtState.h"
#include "Camera.h"
#include "CameraController.h"

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
    virtual LRESULT WindowProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    virtual IDXGISwapChain* GetSwapchain() { return m_deviceResources->GetSwapChain(); }

private:
    static const UINT FrameCount = 3;

    bool mNativeDxrSupported;
    bool mRaytracingEnabled;

    std::shared_ptr<Math::Camera> mCamera;
    std::shared_ptr<GameCore::CameraController> mCamController;

    DXRFramework::RtContext::SharedPtr mRtContext;
    DXRFramework::RtRenderer::SharedPtr mRtRenderer;
    DXRFramework::RtScene::SharedPtr mRtScene;

    DXRFramework::RtProgram::SharedPtr mRtProgram;
    DXRFramework::RtBindings::SharedPtr mRtBindings;
    DXRFramework::RtState::SharedPtr mRtState;

    void InitRaytracing();
    void CopyRaytracingOutputToBackbuffer(D3D12_RESOURCE_STATES transitionToState = D3D12_RESOURCE_STATE_PRESENT);
};
