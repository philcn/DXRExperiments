#include "stdafx.h"
#include "DXRFrameworkApp.h"
#include "nv_helpers_dx12/DXRHelper.h"
#include "DirectXRaytracingHelper.h"
#include "ImGuiRendererDX.h"
#include "GameInput.h"

using namespace std;
using namespace DXRFramework;

namespace GameCore 
{ 
    extern HWND g_hWnd; 
}

DXRFrameworkApp::DXRFrameworkApp(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    mRaytracingEnabled(false)
{
    UpdateForSizeChange(width, height);
}

void DXRFrameworkApp::OnInit()
{
    m_deviceResources = std::make_unique<DX::DeviceResources>(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_UNKNOWN,
        FrameCount,
        D3D_FEATURE_LEVEL_12_0,
        // Sample shows handling of use cases with tearing support, which is OS dependent and has been supported since TH2.
        // Since the Fallback Layer requires Fall Creator's update (RS3), we don't need to handle non-tearing cases.
        DX::DeviceResources::c_RequireTearingSupport,
        m_adapterIDoverride
    );
    m_deviceResources->RegisterDeviceNotify(this);
    m_deviceResources->SetWindow(Win32Application::GetHwnd(), GetWidth(), GetHeight());
    m_deviceResources->InitializeDXGIAdapter();
    mNativeDxrSupported = IsDirectXRaytracingSupported(m_deviceResources->GetAdapter());
    ThrowIfFalse(EnableComputeRaytracingFallback(m_deviceResources->GetAdapter()));

    m_deviceResources->CreateDeviceResources();
    m_deviceResources->CreateWindowSizeDependentResources();

    GameInput::Initialize();

    // Initialize texture loader
    #if (_WIN32_WINNT >= 0x0A00 /*_WIN32_WINNT_WIN10*/)
        Microsoft::WRL::Wrappers::RoInitializeWrapper initialize(RO_INIT_MULTITHREADED);
        ThrowIfFailed(initialize, L"Cannot initialize WIC");
    #else
        #error Unsupported Windows version
    #endif

    // Setup camera states
    mCamera.reset(new Math::Camera());
    mCamera->SetAspectRatio(m_aspectRatio);
    mCamera->SetEyeAtUp(Math::Vector3(1.0, 1.2, 4.0), Math::Vector3(0.0, 0.5, 0.0), Math::Vector3(Math::kYUnitVector));
    mCamera->SetZRange(1.0f, 10000.0f);
    mCamController.reset(new GameCore::CameraController(*mCamera, mCamera->GetUpVec()));
    mCamController->EnableFirstPersonMouse(false);
 
    InitRaytracing();

    // Initialize UI renderer
    ui::RendererDX::Initialize(GameCore::g_hWnd, m_deviceResources->GetD3DDevice(), m_deviceResources->GetBackBufferFormat(), FrameCount, [&] () {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
        UINT heapOffset = mRtContext->allocateDescriptor(&cpuHandle);
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = mRtContext->getDescriptorGPUHandle(heapOffset);
        return std::make_pair(cpuHandle, gpuHandle);
    }); 
}

void DXRFrameworkApp::InitRaytracing()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto commandList = m_deviceResources->GetCommandList();

    mRtContext = RtContext::create(device, commandList, false /* force compute */);

    if (mRaytracingEnabled) {
        mRaytracingPipeline = ProgressiveRaytracingPipeline::create(mRtContext);

        // Create scene
        mRtScene = RtScene::create();
        {
            auto identity = DirectX::XMMatrixIdentity();

            // working directory is "vc2015"
            mRtScene->addModel(RtModel::create(mRtContext, "..\\assets\\models\\ground.fbx"), identity);
            mRtScene->addModel(RtModel::create(mRtContext, "..\\assets\\models\\2_susannes.fbx"), identity);
        }
        mRaytracingPipeline->setScene(mRtScene);

        // Configure raytracing pipeline
        {
            ProgressiveRaytracingPipeline::Material material1 = {};
            material1.params.albedo = XMFLOAT4(1.0f, 0.55f, 0.85f, 1.0f);
            material1.params.specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
            material1.params.roughness = 0.05f;
            material1.params.reflectivity = 0.75f;
            material1.params.type = 1;

            ProgressiveRaytracingPipeline::Material material2 = {};
            material2.params.albedo = XMFLOAT4(0.95f, 0.95f, 0.95f, 1.0f);
            material2.params.specular = XMFLOAT4(0.18f, 0.18f, 0.18f, 1.0f);
            material2.params.roughness = 0.005f;
            material2.params.reflectivity = 1.0f;
            material2.params.type = 1;

            mRaytracingPipeline->addMaterial(material1);
            mRaytracingPipeline->addMaterial(material2);
        }
        mRaytracingPipeline->setCamera(mCamera);
        mRaytracingPipeline->loadResources(m_deviceResources->GetCommandQueue(), FrameCount);
        mRaytracingPipeline->createOutputResource(m_deviceResources->GetBackBufferFormat(), GetWidth(), GetHeight());

        // Build acceleration structures
        commandList->Reset(m_deviceResources->GetCommandAllocator(), nullptr);
        mRaytracingPipeline->buildAccelerationStructures();
        m_deviceResources->ExecuteCommandList();
        m_deviceResources->WaitForGpu();
    }
}

void DXRFrameworkApp::OnUpdate()
{
    DXSample::OnUpdate();

    // Begin recording UI draw list
    ui::RendererDX::NewFrame();

    float elapsedTime = static_cast<float>(mTimer.GetTotalSeconds());
    float deltaTime = static_cast<float>(mTimer.GetElapsedSeconds());

    GameInput::Update(deltaTime);
    mCamController->Update(deltaTime);

    if (mRaytracingEnabled) {
        mRaytracingPipeline->update(elapsedTime, GetFrameCount(), m_deviceResources->GetPreviousFrameIndex(), m_deviceResources->GetCurrentFrameIndex(), GetWidth(), GetHeight());
    }
}

void DXRFrameworkApp::OnRender()
{
    if (!m_deviceResources->IsWindowVisible()) return;

    // Reset command list
    m_deviceResources->Prepare();
    auto commandList = m_deviceResources->GetCommandList();

    if (mRaytracingEnabled) {
        mRaytracingPipeline->render(commandList, m_deviceResources->GetCurrentFrameIndex(), GetWidth(), GetHeight());
        CopyRaytracingOutputToBackbuffer(D3D12_RESOURCE_STATE_RENDER_TARGET);
    } else {
        auto rtvHandle = m_deviceResources->GetRenderTargetView();
        const float clearColor[] = { 0.3f, 0.2f, 0.1f, 1.0f };
        commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

        // Insert rasterizeration code here
    }

    // Render UI
    {
        mRtContext->bindDescriptorHeap();

        auto rtvHandle = m_deviceResources->GetRenderTargetView();
        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        ui::RendererDX::Render(commandList);
    }

    // Execute command list and insert fence
    m_deviceResources->Present(D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void DXRFrameworkApp::OnKeyDown(UINT8 key)
{
    switch (key) {
    case 'F':
        mCamController->EnableFirstPersonMouse(!mCamController->IsFirstPersonMouseEnabled());
        break;
    }
}

void DXRFrameworkApp::OnDestroy()
{
    m_deviceResources->WaitForGpu();

    ui::RendererDX::Shutdown();
    GameInput::Shutdown();
}

void DXRFrameworkApp::OnSizeChanged(UINT width, UINT height, bool minimized)
{
    if (!m_deviceResources->WindowSizeChanged(width, height, minimized)) {
        return;
    }

    UpdateForSizeChange(width, height);

    mCamera->SetAspectRatio(m_aspectRatio);
    mRaytracingPipeline->createOutputResource(m_deviceResources->GetBackBufferFormat(), GetWidth(), GetHeight());
}

void DXRFrameworkApp::CopyRaytracingOutputToBackbuffer(D3D12_RESOURCE_STATES transitionToState /* = D3D12_RESOURCE_STATE_PRESENT */)
{
    auto commandList= m_deviceResources->GetCommandList();
    auto renderTarget = m_deviceResources->GetRenderTarget();
    auto outputResource = mRaytracingPipeline->getOutputResource();

    D3D12_RESOURCE_BARRIER preCopyBarriers[2];
    preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(outputResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);

    commandList->CopyResource(renderTarget, outputResource);

    D3D12_RESOURCE_BARRIER postCopyBarriers[2];
    postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_COPY_DEST, transitionToState);
    postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(outputResource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);
}

LRESULT DXRFrameworkApp::WindowProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return ui::RendererDX::WindowProcHandler(hwnd, msg, wParam, lParam);
}
