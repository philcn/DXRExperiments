#include "stdafx.h"
#include "DXRFrameworkApp.h"
#include "nv_helpers_dx12/DXRHelper.h"
#include "CompiledShaders/ShaderLibrary.hlsl.h"
#include "GameInput.h"
#include "WICTextureLoader.h"
#include "DDSTextureLoader.h"
#include "ResourceUploadBatch.h"
#include "ImGuiRendererDX.h"

using namespace std;
using namespace DXRFramework;

namespace GameCore 
{ 
    extern HWND g_hWnd; 
}

DXRFrameworkApp::DXRFrameworkApp(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    mRaytracingEnabled(true),
    mFrameAccumulationEnabled(false),
    mAnimationPaused(false)
{
    mShaderDebugOptions.maxIterations = 1024;
    mShaderDebugOptions.cosineHemisphereSampling = true;
    mShaderDebugOptions.showIndirectDiffuseOnly = false;
    mShaderDebugOptions.showIndirectSpecularOnly = false;
    mShaderDebugOptions.showAmbientOcclusionOnly = false;
    mShaderDebugOptions.reduceSamplesPerIteration = false;

    auto now = std::chrono::high_resolution_clock::now();
    auto msTime = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    mRng = std::mt19937(uint32_t(msTime.time_since_epoch().count()));

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

    GameInput::Initialize();

    mCamera.SetEyeAtUp(Math::Vector3(-0.3, 0.2, 3.0), Math::Vector3(Math::kZero), Math::Vector3(Math::kYUnitVector));
    mCamera.SetZRange(1.0f, 10000.0f);
    mCamController.reset(new GameCore::CameraController(mCamera, mCamera.GetUpVec()));
    mCamController->EnableFirstPersonMouse(false);

    InitRaytracing();
    BuildAccelerationStructures();

    CreateRaytracingOutputBuffer();
    CreateConstantBuffers();

    ui::RendererDX::Initialize(GameCore::g_hWnd, m_deviceResources->GetD3DDevice(), m_deviceResources->GetBackBufferFormat(), [&] () {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
        UINT heapOffset = mRtContext->allocateDescriptor(&cpuHandle);
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = mRtContext->getDescriptorGPUHandle(heapOffset);
        return std::make_pair(cpuHandle, gpuHandle);
    }); 
}

static ComPtr<ID3D12Resource> sTextureResources[2];
static WRAPPED_GPU_POINTER sTextureHandle[2];

void DXRFrameworkApp::InitRaytracing()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto commandList = m_deviceResources->GetCommandList();
    mRtContext = RtContext::create(device, commandList);

    RtProgram::Desc programDesc;
    std::vector<std::wstring> libraryExports = { L"RayGen", L"PrimaryClosestHit", L"PrimaryMiss", L"ShadowAnyHit", L"ShadowMiss", L"SecondaryMiss" };
    programDesc.addShaderLibrary(g_pShaderLibrary, ARRAYSIZE(g_pShaderLibrary), libraryExports);
    programDesc.setRayGen("RayGen");
    programDesc.addHitGroup(0, "PrimaryClosestHit", "").addMiss(0, "PrimaryMiss");
    programDesc.addHitGroup(1, "", "ShadowAnyHit").addMiss(1, "ShadowMiss");
    programDesc.addMiss(2, "SecondaryMiss");
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

        Material material1 = {};
        material1.params.albedo = XMFLOAT4(1.0f, 0.55f, 0.85f, 1.0f);
        material1.params.specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        material1.params.roughness = 0.2f;
        material1.params.reflectivity = 0.75f;
        material1.params.type = 1; 

        Material material2 = {};
        material2.params.albedo = XMFLOAT4(1.0f, 0.7f, 0.3f, 1.0f);
        material2.params.specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        material2.params.roughness = 0.05f;
        material2.params.reflectivity = 1.0f;
        material2.params.type = 1;

        mMaterials.push_back(material1);
        mMaterials.push_back(material2);
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

inline void calculateCameraVariables(Math::Camera &camera, float aspectRatio, XMFLOAT4 *U, XMFLOAT4 *V, XMFLOAT4 *W)
{
    float ulen, vlen, wlen;
    XMVECTOR w = camera.GetForwardVec(); // Do not normalize W -- it implies focal length

    wlen = XMVectorGetX(XMVector3Length(w));
    XMVECTOR u = XMVector3Normalize(XMVector3Cross(w, camera.GetUpVec()));
    XMVECTOR v = XMVector3Normalize(XMVector3Cross(u, w));

    vlen = wlen * tanf(0.5f * camera.GetFOV());
    ulen = vlen * aspectRatio;
    u = XMVectorScale(u, ulen);
    v = XMVectorScale(v, vlen);

    XMStoreFloat4(U, u);
    XMStoreFloat4(V, v);
    XMStoreFloat4(W, w);
}

void DXRFrameworkApp::UpdatePerFrameConstants(float elapsedTime)
{
    if (HasCameraMoved() || !mFrameAccumulationEnabled) {
        mAccumCount = 0;
        mLastCameraVPMatrix = mCamera.GetViewProjMatrix();
    }

    PerFrameConstants constants = {};

    // Reuse constants for last frame
    auto prevFrameIndex = m_deviceResources->GetPreviousFrameIndex();
    memcpy(&constants, (uint8_t*)mMappedPerFrameConstantsData + mAlignedPerFrameConstantBufferSize * prevFrameIndex, sizeof(constants));

    // Populate camera parameters
    XMStoreFloat4(&constants.cameraParams.worldEyePos, mCamera.GetPosition());
    calculateCameraVariables(mCamera, m_aspectRatio, &constants.cameraParams.U, &constants.cameraParams.V, &constants.cameraParams.W);

    float xJitter = (mRngDist(mRng) - 0.5f) / float(m_width);
    float yJitter = (mRngDist(mRng) - 0.5f) / float(m_height);
    constants.cameraParams.jitters = XMFLOAT2(xJitter, yJitter);

    if (!mAnimationPaused) {
        // Populate lights
        XMVECTOR dirLightVector = XMVectorSet(0.3f, -0.2f, -1.0f, 0.0f);
        XMMATRIX rotation =  XMMatrixRotationY(sin(elapsedTime * 0.2f) * 3.14f * 0.5f);
        dirLightVector = XMVector4Transform(dirLightVector, rotation);
        XMStoreFloat4(&constants.directionalLight.forwardDir, dirLightVector);
        constants.directionalLight.color = XMFLOAT4(0.9f, 0.0f, 0.0f, 1.0f);

        // XMVECTOR pointLightPos = XMVectorSet(sin(elapsedTime * 0.97f), sin(elapsedTime * 0.45f), sin(elapsedTime * 0.32f), 1.0f);
        // pointLightPos = XMVectorAdd(pointLightPos, XMVectorSet(0.0f, 0.5f, 1.0f, 0.0f));
        // pointLightPos = XMVectorMultiply(pointLightPos, XMVectorSet(0.221f, 0.049f, 0.221f, 1.0f));
        XMVECTOR pointLightPos = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        
        XMStoreFloat4(&constants.pointLight.worldPos, pointLightPos);
        constants.pointLight.color = XMFLOAT4(0.2f, 0.8f, 0.6f, 2.0f);
    }

    constants.options = mShaderDebugOptions;
    constants.frameCount = GetFrameCount();
    constants.accumCount = mAccumCount++;

    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
    memcpy((uint8_t*)mMappedPerFrameConstantsData + mAlignedPerFrameConstantBufferSize * frameIndex, &constants, sizeof(constants));
}

bool DXRFrameworkApp::HasCameraMoved()
{
    const Math::Matrix4 &currentMatrix = mCamera.GetViewProjMatrix();
    return !(XMVector4Equal(mLastCameraVPMatrix.GetX(), currentMatrix.GetX()) &&
             XMVector4Equal(mLastCameraVPMatrix.GetY(), currentMatrix.GetY()) &&
             XMVector4Equal(mLastCameraVPMatrix.GetZ(), currentMatrix.GetZ()) &&
             XMVector4Equal(mLastCameraVPMatrix.GetW(), currentMatrix.GetW()));
}

void DXRFrameworkApp::ResetAccumulation()
{
    mLastCameraVPMatrix = Math::Matrix4();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DXRFrameworkApp::DoRaytracing()
{
    auto commandList = m_deviceResources->GetCommandList();
    commandList->SetComputeRootSignature(mRtProgram->getGlobalRootSignature());
    mRtContext->bindDescriptorHeap();

    for (int rayType = 0; rayType < mRtProgram->getHitProgramCount(); ++rayType) {
        for (int instance = 0; instance < mRtScene->getNumInstances(); ++instance) {
            WRAPPED_GPU_POINTER srvWrappedPtr = mRtScene->getModel(instance)->getVertexBufferWrappedPtr();
            mRtBindings->getHitVars(rayType, instance)->appendDescriptor(srvWrappedPtr);

            const Material &material = mMaterials[instance];
            mRtBindings->getHitVars(rayType, instance)->append32BitConstants((void*)&material.params, sizeof(MaterialParams) / sizeof(int32_t));
        }
    }

    for (int rayType = 0; rayType < mRtProgram->getMissProgramCount(); ++rayType) {
        mRtBindings->getMissVars(rayType)->appendDescriptor(sTextureHandle[0]);
        mRtBindings->getMissVars(rayType)->appendDescriptor(sTextureHandle[1]);
    }

    mRtBindings->apply(mRtContext, mRtState);

    commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, mOutputResourceUAVGpuDescriptor);

    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
    auto cbGpuAddress = mPerFrameConstantBuffer->GetGPUVirtualAddress() + mAlignedPerFrameConstantBufferSize * frameIndex;
    commandList->SetComputeRootConstantBufferView(GlobalRootSignatureParams::PerFrameConstantsSlot, cbGpuAddress);

    mRtContext->getFallbackCommandList()->SetTopLevelAccelerationStructure(GlobalRootSignatureParams::AccelerationStructureSlot, mRtScene->getTlasWrappedPtr());

    mRtContext->raytrace(mRtBindings, mRtState, GetWidth(), GetHeight());
}

void DXRFrameworkApp::CopyRaytracingOutputToBackbuffer(D3D12_RESOURCE_STATES transitionToState /* = D3D12_RESOURCE_STATE_PRESENT */)
{
    auto commandList= m_deviceResources->GetCommandList();
    auto renderTarget = m_deviceResources->GetRenderTarget();

    D3D12_RESOURCE_BARRIER preCopyBarriers[2];
    preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(mOutputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);

    commandList->CopyResource(renderTarget, mOutputResource.Get());

    D3D12_RESOURCE_BARRIER postCopyBarriers[2];
    postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_COPY_DEST, transitionToState);
    postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(mOutputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);
}

void DXRFrameworkApp::UserInterface()
{
    bool resetAccumulation = false;
    resetAccumulation |= ui::Checkbox("Raytracing/Rasterization", &mRaytracingEnabled);
    resetAccumulation |= ui::Checkbox("Pause Animation", &mAnimationPaused);

    ui::Separator();

    if (ui::Checkbox("Frame Accumulation", &mFrameAccumulationEnabled)) {
        mAnimationPaused = true;
        resetAccumulation = true;
    }

    if (mFrameAccumulationEnabled) {
        int currentIterations = min(mAccumCount, mShaderDebugOptions.maxIterations);
        ui::SliderInt("Iterations", &currentIterations, 0, mShaderDebugOptions.maxIterations);

        int oldMaxIterations = mShaderDebugOptions.maxIterations;
        if (ui::SliderInt("Max Iterations", (int*)&mShaderDebugOptions.maxIterations, 1, 2048)) {
            resetAccumulation |= (mShaderDebugOptions.maxIterations < mAccumCount);
            mAccumCount = min(mAccumCount, oldMaxIterations);
        }
    }

    ui::Separator();

    resetAccumulation |= ui::Checkbox("Cosine Hemisphere Sampling", (bool*)&mShaderDebugOptions.cosineHemisphereSampling);
    resetAccumulation |= ui::Checkbox("Indirect Diffuse Only", (bool*)&mShaderDebugOptions.showIndirectDiffuseOnly);
    resetAccumulation |= ui::Checkbox("Indirect Specular Only", (bool*)&mShaderDebugOptions.showIndirectSpecularOnly);
    resetAccumulation |= ui::Checkbox("AO Only", (bool*)&mShaderDebugOptions.showAmbientOcclusionOnly);
    resetAccumulation |= ui::Checkbox("Reduce samples", (bool*)&mShaderDebugOptions.reduceSamplesPerIteration);

    ui::Separator();

    ui::Text("Press space to toggle first person camera");

    if (resetAccumulation) ResetAccumulation();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DXRFrameworkApp::OnUpdate()
{
    DXSample::OnUpdate();

    float elapsedTime = static_cast<float>(mTimer.GetTotalSeconds());
    float deltaTime = static_cast<float>(mTimer.GetElapsedSeconds());

    GameInput::Update(deltaTime);
    mCamController->Update(deltaTime);

    UpdatePerFrameConstants(elapsedTime);

    // Prepare UI draw list
    ui::RendererDX::NewFrame();
    UserInterface();
}

void DXRFrameworkApp::OnRender()
{
    if (!m_deviceResources->IsWindowVisible()) {
        return;
    }

    m_deviceResources->Prepare();
    auto commandList= m_deviceResources->GetCommandList();

    if (mRaytracingEnabled) {
        DoRaytracing();
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

    m_deviceResources->Present(D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void DXRFrameworkApp::OnKeyDown(UINT8 key)
{
    switch (key) {
    case VK_SPACE:
        mCamController->EnableFirstPersonMouse(!mCamController->IsFirstPersonMouseEnabled());
        break;
    default:
        break;
    }
}

void DXRFrameworkApp::OnDestroy()
{
    m_deviceResources->WaitForGpu();

    ui::RendererDX::Shutdown();
    GameInput::Shutdown();

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

LRESULT DXRFrameworkApp::WindowProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return ui::RendererDX::WindowProcHandler(hwnd, msg, wParam, lParam);
}
