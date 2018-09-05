#include "stdafx.h"
#include "DXRFrameworkApp.h"
#include "nv_helpers_dx12/DXRHelper.h"
#include "RaytracingHlslCompat.h"
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
    CreateCameraBuffer();
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

    // working directory is "vc2015"
    const char *path = "..\\assets\\models\\cornell.obj";
    // const char *path = "..\\assets\\models\\susanne.obj";
    RtModel::SharedPtr model = RtModel::create(mRtContext, path);

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

void DXRFrameworkApp::CreateCameraBuffer()
{
    auto device = m_deviceResources->GetD3DDevice();

    mCameraConstantBufferSize = CalculateConstantBufferByteSize(sizeof(CameraParams));

    mCameraConstantBuffer = nv_helpers_dx12::CreateBuffer(device, mCameraConstantBufferSize, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

    D3D12_CPU_DESCRIPTOR_HANDLE cbvDescriptorHandle;
    UINT cameraCBVDescriptorHeapIndex = mRtContext->allocateDescriptor(&cbvDescriptorHandle);
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = mCameraConstantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = mCameraConstantBufferSize;
    device->CreateConstantBufferView(&cbvDesc, cbvDescriptorHandle);
    mCameraCBVGpuDescriptor = mRtContext->getDescriptorGPUHandle(cameraCBVDescriptorHeapIndex);
}

inline void calculateCameraVariables(XMVECTOR eye, XMVECTOR lookat, XMVECTOR up, float yfov, float aspectRatio, XMFLOAT4 *U, XMFLOAT4 *V, XMFLOAT4 *W)
{
    float ulen, vlen, wlen;
    XMVECTOR w = XMVectorSubtract(lookat, eye); // Do not normalize W -- it implies focal length
    
    wlen = XMVectorGetX(XMVector3Length(w));
    XMVECTOR u = XMVector3Normalize(XMVector3Cross(w, up));
    XMVECTOR v = XMVector3Normalize(XMVector3Cross(u, w));

    vlen = wlen * tanf(0.5f * yfov * XM_PI / 180.0f);
    ulen = vlen * aspectRatio;
    u = XMVectorScale(u, ulen);
    v = XMVectorScale(v, vlen);

    XMStoreFloat4(U, u);
    XMStoreFloat4(V, v);
    XMStoreFloat4(W, w);
}

void DXRFrameworkApp::UpdateCameraMatrices(float elapsedTime)
{
    using namespace DirectX;

    XMVECTOR Eye = XMVectorSet(sinf(elapsedTime), 0.0f, 5.5f, 1.0f);
    XMVECTOR At = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    CameraParams params = {};
    XMStoreFloat4(&params.worldEyePos, Eye);
    calculateCameraVariables(Eye, At, Up, 45.0f, m_aspectRatio, &params.U, &params.V, &params.W);

    uint8_t* pData;
    ThrowIfFailed(mCameraConstantBuffer->Map(0, nullptr, (void**)&pData));
    memcpy(pData, &params, sizeof(params));

    mCameraConstantBuffer->Unmap(0, nullptr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool gVertexBufferUseRootTableInsteadOfRootView = false;

void DXRFrameworkApp::DoRaytracing()
{
    auto commandList = m_deviceResources->GetCommandList();
    commandList->SetComputeRootSignature(mRtProgram->getGlobalRootSignature());
    mRtContext->bindDescriptorHeap();

    // TEST bind vertex buffer
    {
        WRAPPED_GPU_POINTER srvWrappedPtr = mRtScene->getModel(0)->getVertexBufferWrappedPtr();

        if (gVertexBufferUseRootTableInsteadOfRootView) {
            D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle = mRtContext->getDescriptorGPUHandle(srvWrappedPtr.EmulatedGpuPtr.DescriptorHeapIndex);
            mRtBindings->getHitVars(0)->appendHeapRanges(srvGpuHandle.ptr);
            mRtBindings->getMissVars(0)->appendHeapRanges(srvGpuHandle.ptr);
        } else {
            mRtBindings->getHitVars(0)->appendSRV(srvWrappedPtr);
            mRtBindings->getMissVars(0)->appendSRV(srvWrappedPtr);
        }
    }

    // TEST bind 32-bit constant
    {
        int32_t constant0 = 16;
        mRtBindings->getMissVars(0)->append32BitConstants(&constant0, 1);
        mRtBindings->getHitVars(0)->append32BitConstants(&constant0, 1);
    }

    mRtBindings->apply(mRtContext, mRtState);

    commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, mOutputResourceUAVGpuDescriptor);
    commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::CameraParameterSlot, mCameraCBVGpuDescriptor);
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

    float elapsedTime = static_cast<float>(mTimer.GetTotalSeconds());
    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
    auto prevFrameIndex = m_deviceResources->GetPreviousFrameIndex();

    UpdateCameraMatrices(elapsedTime);
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
}
