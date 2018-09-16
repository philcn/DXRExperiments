#include "stdafx.h"
#include "DXRFrameworkApp.h"
#include "nv_helpers_dx12/DXRHelper.h"
#include "RaytracingHlslCompat.h"
#include "CompiledShaders/ShaderLibrary.hlsl.h"

#include "WICTextureLoader.h"
#include "DDSTextureLoader.h"
#include "ResourceUploadBatch.h"

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
    CreateConstantBuffers();
}

static ComPtr<ID3D12Resource> sTextureResources[2];
static WRAPPED_GPU_POINTER sTextureHandle[2];

void DXRFrameworkApp::InitRaytracing()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto commandList = m_deviceResources->GetCommandList();
    mRtContext = RtContext::create(device, commandList);

    RtProgram::Desc programDesc;
    std::vector<std::wstring> libraryExports = { L"RayGen", L"PrimaryClosestHit", L"PrimaryMiss", L"ShadowAnyHit", L"ShadowMiss" };
    programDesc.addShaderLibrary(g_pShaderLibrary, ARRAYSIZE(g_pShaderLibrary), libraryExports);
    programDesc.setRayGen("RayGen");
    programDesc.addHitGroup(0, "PrimaryClosestHit", "").addMiss(0, "PrimaryMiss");
    programDesc.addHitGroup(1, "", "ShadowAnyHit").addMiss(1, "ShadowMiss");
    mRtProgram = RtProgram::create(mRtContext, programDesc);

    mRtState = RtState::create(mRtContext); 
    mRtState->setProgram(mRtProgram);
    mRtState->setMaxTraceRecursionDepth(3); // allow primary - reflection - shadow

    mRtScene = RtScene::create();

    // Setup scene
    {
        const float scale = 0.3f;
        auto transform = DirectX::XMMatrixScaling(scale, scale, scale) * DirectX::XMMatrixTranslation(0.52f, -0.23f, 0.3f);
        auto identity = DirectX::XMMatrixIdentity();

        // working directory is "vc2015"
        mRtScene->addModel(RtModel::create(mRtContext, "..\\assets\\models\\cornell.obj"), identity);
        mRtScene->addModel(RtModel::create(mRtContext, "..\\assets\\models\\susanne.obj"), transform);
    }

    mRtBindings = RtBindings::create(mRtContext, mRtProgram, mRtScene);

    // Setup texture loader
    #if (_WIN32_WINNT >= 0x0A00 /*_WIN32_WINNT_WIN10*/)
        Microsoft::WRL::Wrappers::RoInitializeWrapper initialize(RO_INIT_MULTITHREADED);
        ThrowIfFailed(initialize, L"Cannot initialize WIC");
    #else
        #error Unsupported Windows version
    #endif

    // Load textures
    {
        ResourceUploadBatch resourceUpload(device);
        resourceUpload.Begin();
        ThrowIfFailed(CreateWICTextureFromFile(device, resourceUpload, L"..\\assets\\textures\\Mans_Outside_8k_TMap.jpg", &sTextureResources[0], true));
        ThrowIfFailed(CreateDDSTextureFromFile(device, resourceUpload, L"..\\assets\\textures\\CathedralRadiance.dds", &sTextureResources[1]));

        auto uploadResourcesFinished = resourceUpload.End(m_deviceResources->GetCommandQueue());
        uploadResourcesFinished.wait();

        sTextureHandle[0] = mRtContext->createTextureSRVWrappedPointer(sTextureResources[0].Get());
        sTextureHandle[1] = mRtContext->createTextureSRVWrappedPointer(sTextureResources[1].Get());
    }
}

void DXRFrameworkApp::BuildAccelerationStructures()
{
    auto commandList = m_deviceResources->GetCommandList();
    auto commandAllocator = m_deviceResources->GetCommandAllocator();

    commandList->Reset(commandAllocator, nullptr);

    mRtScene->build(mRtContext, mRtProgram->getHitProgramCount());

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

void DXRFrameworkApp::CreateConstantBuffers()
{
    auto device = m_deviceResources->GetD3DDevice();

    mAlignedPerFrameConstantBufferSize = CalculateConstantBufferByteSize(sizeof(PerFrameConstants));
    auto allocationSize = mAlignedPerFrameConstantBufferSize * FrameCount;

    mPerFrameConstantBuffer = nv_helpers_dx12::CreateBuffer(device, allocationSize, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

    D3D12_CPU_DESCRIPTOR_HANDLE cbvDescriptorHandle;
    UINT descriptorHeapIndex = mRtContext->allocateDescriptor(&cbvDescriptorHandle);
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = mPerFrameConstantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = allocationSize;
    device->CreateConstantBufferView(&cbvDesc, cbvDescriptorHandle);
    mPerFrameCBVGpuHandle = mRtContext->getDescriptorGPUHandle(descriptorHeapIndex); 

    // Map the constant buffer and cache its heap pointers. We don't unmap this until the app closes.
    CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(mPerFrameConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedPerFrameConstantsData)));
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

void DXRFrameworkApp::UpdatePerFrameConstants(float elapsedTime)
{
    PerFrameConstants constants = {};

    // camera
    XMVECTOR Eye = XMVectorSet(sinf(elapsedTime * 0.3f) * 2.4f, 0.0f, 3.5f, 1.0f);
    XMVECTOR At = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMStoreFloat4(&constants.cameraParams.worldEyePos, Eye);
    calculateCameraVariables(Eye, At, Up, 45.0f, m_aspectRatio, 
        &constants.cameraParams.U, &constants.cameraParams.V, &constants.cameraParams.W);

    // directional light
    XMVECTOR dirLightVector = XMVectorSet(0.3f, -0.2f, -1.0f, 0.0f);
    XMMATRIX rotation =  XMMatrixRotationY(sin(elapsedTime * 0.2f) * 3.14f * 0.5f);
    dirLightVector = XMVector4Transform(dirLightVector, rotation);
    constants.directionalLight.color = XMFLOAT4(0.7f, 0.0f, 0.0f, 1.0f);
    XMStoreFloat4(&constants.directionalLight.forwardDir, dirLightVector);

    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
    memcpy((uint8_t*)mMappedPerFrameConstantsData + mAlignedPerFrameConstantBufferSize * frameIndex, &constants, sizeof(constants));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DXRFrameworkApp::DoRaytracing()
{
    auto commandList = m_deviceResources->GetCommandList();
    commandList->SetComputeRootSignature(mRtProgram->getGlobalRootSignature());
    mRtContext->bindDescriptorHeap();

    const int numRayTypes = mRtProgram->getHitProgramCount();
    for (int rayType = 0; rayType < numRayTypes; ++rayType) {
        int32_t constant0 = 16;
        mRtBindings->getMissVars(rayType)->append32BitConstants(&constant0, 1);
        mRtBindings->getMissVars(rayType)->appendDescriptor(sTextureHandle[0]);
        mRtBindings->getMissVars(rayType)->appendDescriptor(sTextureHandle[1]);

        for (int instance = 0; instance < mRtScene->getNumInstances(); ++instance) {
            WRAPPED_GPU_POINTER srvWrappedPtr = mRtScene->getModel(instance)->getVertexBufferWrappedPtr();
            mRtBindings->getHitVars(rayType, instance)->appendDescriptor(srvWrappedPtr);
        }
    }

    mRtBindings->apply(mRtContext, mRtState);

    commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, mOutputResourceUAVGpuDescriptor);

    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
    auto cbGpuAddress = mPerFrameConstantBuffer->GetGPUVirtualAddress() + mAlignedPerFrameConstantBufferSize * frameIndex;
    commandList->SetComputeRootConstantBufferView(GlobalRootSignatureParams::PerFrameConstantsSlot, cbGpuAddress);

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

    UpdatePerFrameConstants(elapsedTime);
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
