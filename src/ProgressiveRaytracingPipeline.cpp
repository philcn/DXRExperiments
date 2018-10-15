#include "stdafx.h"
#include "ProgressiveRaytracingPipeline.h"
#include "CompiledShaders/ShaderLibrary.hlsl.h"
#include "WICTextureLoader.h"
#include "DDSTextureLoader.h"
#include "ResourceUploadBatch.h"
#include "DirectXRaytracingHelper.h"
#include "nv_helpers_dx12/DXRHelper.h"
#include "ImGuiRendererDX.h"
#include <chrono>

using namespace DXRFramework;

static XMFLOAT4 pointLightColor = XMFLOAT4(0.2f, 0.8f, 0.6f, 2.0f);
static XMFLOAT4 dirLightColor = XMFLOAT4(0.9f, 0.0f, 0.0f, 1.0f);

ProgressiveRaytracingPipeline::ProgressiveRaytracingPipeline(RtContext::SharedPtr context) :
    mRtContext(context),
    mFrameAccumulationEnabled(false),
    mAnimationPaused(true)
{
    RtProgram::Desc programDesc;
    {
        std::vector<std::wstring> libraryExports = { L"RayGen", L"PrimaryClosestHit", L"PrimaryMiss", L"ShadowClosestHit", L"ShadowAnyHit", L"ShadowMiss", L"SecondaryMiss" };
        programDesc.addShaderLibrary(g_pShaderLibrary, ARRAYSIZE(g_pShaderLibrary), libraryExports);
        programDesc.setRayGen("RayGen");
        programDesc.addHitGroup(0, "PrimaryClosestHit", "").addMiss(0, "PrimaryMiss");
        programDesc.addHitGroup(1, "ShadowClosestHit", "ShadowAnyHit").addMiss(1, "ShadowMiss");
        programDesc.addMiss(2, "SecondaryMiss");
    }
    mRtProgram = RtProgram::create(context, programDesc);
    mRtState = RtState::create(context); 
    mRtState->setProgram(mRtProgram);
    mRtState->setMaxTraceRecursionDepth(4);

    mShaderDebugOptions.maxIterations = 1024;
    mShaderDebugOptions.cosineHemisphereSampling = true;
    mShaderDebugOptions.showIndirectDiffuseOnly = false;
    mShaderDebugOptions.showIndirectSpecularOnly = false;
    mShaderDebugOptions.showAmbientOcclusionOnly = false;
    mShaderDebugOptions.showGBufferAlbedoOnly = false;
    mShaderDebugOptions.showDirectLightingOnly = false;
    mShaderDebugOptions.showReflectionDenoiseGuide = false;
    mShaderDebugOptions.showFresnelTerm = false;
    mShaderDebugOptions.environmentStrength = 1.0f;
    mShaderDebugOptions.debug = 0;

    auto now = std::chrono::high_resolution_clock::now();
    auto msTime = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    mRng = std::mt19937(uint32_t(msTime.time_since_epoch().count()));
}

ProgressiveRaytracingPipeline::~ProgressiveRaytracingPipeline() = default;

void ProgressiveRaytracingPipeline::setScene(RtScene::SharedPtr scene)
{
    mRtScene = scene;
    mRtBindings = RtBindings::create(mRtContext, mRtProgram, scene);
}

void ProgressiveRaytracingPipeline::buildAccelerationStructures()
{
    mRtScene->build(mRtContext, mRtProgram->getHitProgramCount());
}

void ProgressiveRaytracingPipeline::loadResources(ID3D12CommandQueue *uploadCommandQueue, UINT frameCount)
{        
    mTextureResources.resize(2);
    mTextureSrvGpuHandles.resize(2);

    // Create and upload global textures
    auto device = mRtContext->getDevice();
    ResourceUploadBatch resourceUpload(device);
    resourceUpload.Begin();
    {
        ThrowIfFailed(CreateWICTextureFromFile(device, resourceUpload, L"..\\assets\\textures\\HdrStudioProductNightStyx001_JPG_8K.jpg", &mTextureResources[0], true));
        ThrowIfFailed(CreateDDSTextureFromFile(device, resourceUpload, L"..\\assets\\textures\\CathedralRadiance.dds", &mTextureResources[1]));
    }
    auto uploadResourcesFinished = resourceUpload.End(uploadCommandQueue);
    uploadResourcesFinished.wait();

    mTextureSrvGpuHandles[0] = mRtContext->createTextureSRVHandle(mTextureResources[0].Get());
    mTextureSrvGpuHandles[1] = mRtContext->createTextureSRVHandle(mTextureResources[1].Get(), true);

    // Create per-frame constant buffer
    mConstantBufferAlignedSize = CalculateConstantBufferByteSize(sizeof(PerFrameConstants));
    mConstantBufferResource = nv_helpers_dx12::CreateBuffer(mRtContext->getDevice(), mConstantBufferAlignedSize * frameCount, 
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

    // Map the constant buffer and cache its heap pointers. We don't unmap this until the app closes.
    ThrowIfFailed(mConstantBufferResource->Map(0, nullptr, reinterpret_cast<void**>(&mConstantBufferData)));
}

void ProgressiveRaytracingPipeline::createOutputResource(DXGI_FORMAT format, UINT width, UINT height)
{
    auto device = mRtContext->getDevice();
    AllocateUAVTexture(device, format, width, height, &mOutputResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    D3D12_CPU_DESCRIPTOR_HANDLE descriptorCpuHandle;
    UINT descriptorHeapIndex = mRtContext->allocateDescriptor(&descriptorCpuHandle, UINT_MAX);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(mOutputResource.Get(), nullptr, &uavDesc, descriptorCpuHandle);
    mOutputUavGpuHandle = mRtContext->getDescriptorGPUHandle(descriptorHeapIndex);
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

inline bool hasCameraMoved(Math::Camera &camera, Math::Matrix4 &lastVPMatrix)
{
    const Math::Matrix4 &currentMatrix = camera.GetViewProjMatrix();
    return !(XMVector4Equal(lastVPMatrix.GetX(), currentMatrix.GetX()) && XMVector4Equal(lastVPMatrix.GetY(), currentMatrix.GetY()) &&
        XMVector4Equal(lastVPMatrix.GetZ(), currentMatrix.GetZ()) && XMVector4Equal(lastVPMatrix.GetW(), currentMatrix.GetW()));
}

void ProgressiveRaytracingPipeline::update(float elapsedTime, UINT elapsedFrames, UINT prevFrameIndex, UINT frameIndex, UINT width, UINT height)
{
    userInterface();

    if (mAnimationPaused) {
        elapsedTime = 142.0f;
    }

    if (hasCameraMoved(*mCamera, mLastCameraVPMatrix) || !mFrameAccumulationEnabled) {
        mAccumCount = 0;
        mLastCameraVPMatrix = mCamera->GetViewProjMatrix();
    }

    // Reuse constants for last frame
    PerFrameConstants constants = {};
    memcpy(&constants, (uint8_t*)mConstantBufferData + mConstantBufferAlignedSize * prevFrameIndex, sizeof(constants));
    {
        XMStoreFloat4(&constants.cameraParams.worldEyePos, mCamera->GetPosition());
        calculateCameraVariables(*mCamera, mCamera->GetAspectRatio(), &constants.cameraParams.U, &constants.cameraParams.V, &constants.cameraParams.W);
        float xJitter = (mRngDist(mRng) - 0.5f) / float(width);
        float yJitter = (mRngDist(mRng) - 0.5f) / float(height);
        constants.cameraParams.jitters = XMFLOAT2(xJitter, yJitter);
        constants.cameraParams.frameCount = elapsedFrames;
        constants.cameraParams.accumCount = mAccumCount++;
        constants.options = mShaderDebugOptions;

        XMVECTOR dirLightVector = XMVectorSet(0.3f, -0.2f, -1.0f, 0.0f);
        XMMATRIX rotation =  XMMatrixRotationY(sin(elapsedTime * 0.2f) * 3.14f * 0.5f);
        dirLightVector = XMVector4Transform(dirLightVector, rotation);
        XMStoreFloat4(&constants.directionalLight.forwardDir, dirLightVector);
        constants.directionalLight.color = dirLightColor;

        // XMVECTOR pointLightPos = XMVectorSet(sin(elapsedTime * 0.97f), sin(elapsedTime * 0.45f), sin(elapsedTime * 0.32f), 1.0f);
        // pointLightPos = XMVectorAdd(pointLightPos, XMVectorSet(0.0f, 0.5f, 1.0f, 0.0f));
        // pointLightPos = XMVectorMultiply(pointLightPos, XMVectorSet(0.221f, 0.049f, 0.221f, 1.0f));
        XMVECTOR pointLightPos = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        XMStoreFloat4(&constants.pointLight.worldPos, pointLightPos);
        constants.pointLight.color = pointLightColor;
    }
    memcpy((uint8_t*)mConstantBufferData + mConstantBufferAlignedSize * frameIndex, &constants, sizeof(constants));
}

void ProgressiveRaytracingPipeline::render(ID3D12GraphicsCommandList *commandList, UINT frameIndex, UINT width, UINT height)
{
    auto program = mRtBindings->getProgram();
    commandList->SetComputeRootSignature(program->getGlobalRootSignature());
    mRtContext->bindDescriptorHeap();

    // Update shader table root arguments
    for (int rayType = 0; rayType < program->getHitProgramCount(); ++rayType) {
        for (int instance = 0; instance < mRtScene->getNumInstances(); ++instance) {
            auto &hitVars = mRtBindings->getHitVars(rayType, instance);

            D3D12_GPU_DESCRIPTOR_HANDLE vbSrvHandle = mRtScene->getModel(instance)->getVertexBufferSrvHandle();
            D3D12_GPU_DESCRIPTOR_HANDLE ibSrvHandle = mRtScene->getModel(instance)->getIndexBufferSrvHandle();
            hitVars->appendHeapRanges(vbSrvHandle.ptr);
            hitVars->appendHeapRanges(ibSrvHandle.ptr);

            const Material &material = mMaterials[instance];
            hitVars->append32BitConstants((void*)&material.params, SizeOfInUint32(MaterialParams));
        }
    }

    for (int rayType = 0; rayType < program->getMissProgramCount(); ++rayType) {
        auto &missVars = mRtBindings->getMissVars(rayType);
        missVars->appendHeapRanges(mTextureSrvGpuHandles[0].ptr);
        missVars->appendHeapRanges(mTextureSrvGpuHandles[1].ptr);
    }

    mRtBindings->apply(mRtContext, mRtState);

    // Set global root arguments
    auto cbGpuAddress = mConstantBufferResource->GetGPUVirtualAddress() + mConstantBufferAlignedSize * frameIndex;
    commandList->SetComputeRootConstantBufferView(GlobalRootSignatureParams::PerFrameConstantsSlot, cbGpuAddress);

    commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, mOutputUavGpuHandle);
    mRtContext->getFallbackCommandList()->SetTopLevelAccelerationStructure(GlobalRootSignatureParams::AccelerationStructureSlot, mRtScene->getTlasWrappedPtr());

    mRtContext->raytrace(mRtBindings, mRtState, width, height, 3);
}

void ProgressiveRaytracingPipeline::userInterface()
{
    ui::Begin("Lighting");
    {
        ui::ColorPicker4("Point Light", (float*)&pointLightColor);
        ui::ColorPicker4("Directional Light", (float*)&dirLightColor);
    }
    ui::End();

    bool frameDirty = false;
    frameDirty |= ui::Checkbox("Pause Animation", &mAnimationPaused);

    ui::Separator();

    if (ui::Checkbox("Frame Accumulation", &mFrameAccumulationEnabled)) {
        mAnimationPaused = true;
        frameDirty = true;
    }

    if (mFrameAccumulationEnabled) {
        int currentIterations = min(mAccumCount, mShaderDebugOptions.maxIterations);
        int oldMaxIterations = mShaderDebugOptions.maxIterations;
        if (ui::SliderInt("Max Iterations", (int*)&mShaderDebugOptions.maxIterations, 1, 2048)) {
            frameDirty |= (mShaderDebugOptions.maxIterations < mAccumCount);
            mAccumCount = min(mAccumCount, oldMaxIterations);
        }
        ui::ProgressBar(float(currentIterations) / float(mShaderDebugOptions.maxIterations), ImVec2(), std::to_string(currentIterations).c_str());
    }

    ui::Separator();

    frameDirty |= ui::Checkbox("Cosine Hemisphere Sampling", (bool*)&mShaderDebugOptions.cosineHemisphereSampling);
    frameDirty |= ui::Checkbox("Indirect Diffuse Only", (bool*)&mShaderDebugOptions.showIndirectDiffuseOnly);
    frameDirty |= ui::Checkbox("Indirect Specular Only", (bool*)&mShaderDebugOptions.showIndirectSpecularOnly);
    frameDirty |= ui::Checkbox("Ambient Occlusion Only", (bool*)&mShaderDebugOptions.showAmbientOcclusionOnly);
    frameDirty |= ui::Checkbox("GBuffer Albedo Only", (bool*)&mShaderDebugOptions.showGBufferAlbedoOnly);
    frameDirty |= ui::Checkbox("Direct Lighting Only", (bool*)&mShaderDebugOptions.showDirectLightingOnly);
    frameDirty |= ui::Checkbox("Reflection Denoise Guide", (bool*)&mShaderDebugOptions.showReflectionDenoiseGuide);
    frameDirty |= ui::Checkbox("Fresnel Term Only", (bool*)&mShaderDebugOptions.showFresnelTerm);
    frameDirty |= ui::SliderFloat("Environment Strength", &mShaderDebugOptions.environmentStrength, 0.1f, 10.0f);
    frameDirty |= ui::SliderInt("Debug", (int*)&mShaderDebugOptions.debug, 0, 2);

    ui::Separator();

    ui::Text("Press space to toggle first person camera");

    if (frameDirty) {
        mLastCameraVPMatrix = Math::Matrix4();
    }
}
