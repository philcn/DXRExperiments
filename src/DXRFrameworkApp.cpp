#include "stdafx.h"
#include "DXRFrameworkApp.h"
#include "CompiledShaders/ShaderLibrary.hlsl.h"

using namespace std;
using namespace DXRFramework;

DXRFrameworkApp::DXRFrameworkApp(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    mRaytracingEnabled(true)
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
    m_deviceResources->SetWindow(Win32Application::GetHwnd(), m_width, m_height);
    m_deviceResources->InitializeDXGIAdapter();
    mUseDXRDriver = EnableDXRExperimentalFeatures(m_deviceResources->GetAdapter());

    m_deviceResources->CreateDeviceResources();
    m_deviceResources->CreateWindowSizeDependentResources();

    InitRaytracing();
    BuildAccelerationStructures();

    CreateRaytracingOutputBuffer();
    // UpdateCameraMatrices();
}

void DXRFrameworkApp::InitRaytracing()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto commandList = m_deviceResources->GetCommandList();
    mRtContext = RtContext::create(device, commandList);

    RtProgram::Desc programDesc;
    programDesc.addShaderLibrary(g_pShaderLibrary, ARRAYSIZE(g_pShaderLibrary), {L"RayGen", L"Miss", L"ClosestHit"});
    programDesc.setRayGen("RayGen");
    programDesc.addHitGroup(0, "ClosestHit", "");
    programDesc.addMiss(0, "Miss");
    mRtProgram = RtProgram::create(mRtContext, programDesc);

    mRtState = RtState::create(mRtContext); 
    mRtState->setProgram(mRtProgram);
    mRtState->setMaxTraceRecursionDepth(1);

    mRtBindings = RtBindings::create(mRtContext, mRtProgram);

    RtModel::SharedPtr model = RtModel::create(mRtContext);

    mRtScene = RtScene::create();
    mRtScene->addModel(model, DirectX::XMMatrixIdentity());
}

void DXRFrameworkApp::BuildAccelerationStructures()
{
    auto commandList = m_deviceResources->GetCommandList();
    auto commandAllocator = m_deviceResources->GetCommandAllocator();

    commandList->Reset(commandAllocator, nullptr);

    mRtScene->build(mRtContext);

    m_deviceResources->ExecuteCommandList();
    m_deviceResources->WaitForGpu();
}

void DXRFrameworkApp::CreateRaytracingOutputBuffer()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto backbufferFormat = m_deviceResources->GetBackBufferFormat();

    AllocateUAVTexture(device, backbufferFormat, m_width, m_height, &mOutputResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
    UINT outputResourceUAVDescriptorHeapIndex = mRtContext->allocateDescriptor(&uavDescriptorHandle, UINT_MAX);
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
    UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(mOutputResource.Get(), nullptr, &UAVDesc, uavDescriptorHandle);
    mOutputResourceUAVGpuDescriptor = mRtContext->getDescriptorGPUHandle(outputResourceUAVDescriptorHeapIndex);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DXRFrameworkApp::DoRaytracing()
{
    auto commandList = m_deviceResources->GetCommandList();

    mRtBindings->apply(mRtContext, mRtState);

    mRtContext->bindDescriptorHeap();

    commandList->SetComputeRootSignature(mRtProgram->getGlobalRootSignature());
    commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, mOutputResourceUAVGpuDescriptor);
    mRtContext->getFallbackCommandList()->SetTopLevelAccelerationStructure(GlobalRootSignatureParams::AccelerationStructureSlot, mRtScene->getTlasWrappedPtr());

    mRtContext->raytrace(mRtBindings, mRtState, GetWidth(), GetHeight());
}

void DXRFrameworkApp::CopyRaytracingOutputToBackbuffer()
{
    auto commandList= m_deviceResources->GetCommandList();
    auto renderTarget = m_deviceResources->GetRenderTarget();

    D3D12_RESOURCE_BARRIER preCopyBarriers[2];
    preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(mOutputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);

    commandList->CopyResource(renderTarget, mOutputResource.Get());

    D3D12_RESOURCE_BARRIER postCopyBarriers[2];
    postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(mOutputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DXRFrameworkApp::OnUpdate()
{
    DXSample::OnUpdate();

    float elapsedTime = static_cast<float>(mTimer.GetElapsedSeconds());
    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
    auto prevFrameIndex = m_deviceResources->GetPreviousFrameIndex();
}

void DXRFrameworkApp::OnRender()
{
    if (!m_deviceResources->IsWindowVisible()) {
        return;
    }

    m_deviceResources->Prepare();

    if (mRaytracingEnabled) {
        DoRaytracing();
        CopyRaytracingOutputToBackbuffer();

        m_deviceResources->Present(D3D12_RESOURCE_STATE_PRESENT);
    } else {
        auto commandList= m_deviceResources->GetCommandList();
        auto rtvHandle = m_deviceResources->GetRenderTargetView();

        const float clearColor[] = { 0.3f, 0.2f, 0.1f, 1.0f };
        commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

        // Insert rasterizeration code here

        m_deviceResources->Present(D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
}

void DXRFrameworkApp::OnKeyDown(UINT8 key)
{
    switch (key) {
    case VK_SPACE:
        mRaytracingEnabled ^= true;
        break;
    default:
        break;
    }
}

void DXRFrameworkApp::OnDestroy()
{
    m_deviceResources->WaitForGpu();
    mOutputResource.Reset();
}

void DXRFrameworkApp::OnSizeChanged(UINT width, UINT height, bool minimized)
{
    if (!m_deviceResources->WindowSizeChanged(width, height, minimized)) {
        return;
    }

    UpdateForSizeChange(width, height);

    mOutputResource.Reset();
    CreateRaytracingOutputBuffer();
    // UpdateCameraMatrices();
}
