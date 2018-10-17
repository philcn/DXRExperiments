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

namespace GlobalRootSignatureParams 
{
    enum Value 
    {
        AccelerationStructureSlot = 0,
        OutputViewSlot,
        PerFrameConstantsSlot,
        Count 
    };
}

ProgressiveRaytracingPipeline::ProgressiveRaytracingPipeline(RtContext::SharedPtr context) :
    mRtContext(context),
    mOutputUavHeapIndex(UINT_MAX),
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
        programDesc.configureGlobalRootSignature([] (RootSignatureGenerator &config) {
            // GlobalRootSignatureParams::AccelerationStructureSlot
            config.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0 /* t0 */); 
            // GlobalRootSignatureParams::OutputViewSlot
            config.AddHeapRangesParameter({{0 /* u0 */, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0}}); 
            // GlobalRootSignatureParams::PerFrameConstantsSlot
            config.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0 /* b0 */); 

            D3D12_STATIC_SAMPLER_DESC cubeSampler = {};
            cubeSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            cubeSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            cubeSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            cubeSampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            cubeSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            cubeSampler.ShaderRegister = 0;
            config.AddStaticSampler(cubeSampler);
        });
        programDesc.configureHitGroupRootSignature([] (RootSignatureGenerator &config) {
            config.AddHeapRangesParameter({{0 /* t0 */, 1, 1 /* space1 */, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0}});
            config.AddHeapRangesParameter({{1 /* t1 */, 1, 1 /* space1 */, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0}});
            config.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, 0, 1, SizeOfInUint32(MaterialParams)); // space1 b0
        });
        programDesc.configureMissRootSignature([] (RootSignatureGenerator &config) {
            config.AddHeapRangesParameter({{0 /* t0 */, 1, 2 /* space2 */, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0}});
            config.AddHeapRangesParameter({{1 /* t1 */, 1, 2 /* space2 */, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0}});
        });
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
    mConstantBuffer.Create(device, frameCount, L"PerFrameConstantBuffer");
}

void ProgressiveRaytracingPipeline::createOutputResource(DXGI_FORMAT format, UINT width, UINT height)
{
    auto device = mRtContext->getDevice();

    AllocateUAVTexture(device, format, width, height, mOutputResource.ReleaseAndGetAddressOf(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    D3D12_CPU_DESCRIPTOR_HANDLE descriptorCpuHandle;
    mOutputUavHeapIndex = mRtContext->allocateDescriptor(&descriptorCpuHandle, mOutputUavHeapIndex);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(mOutputResource.Get(), nullptr, &uavDesc, descriptorCpuHandle);

    mOutputUavGpuHandle = mRtContext->getDescriptorGPUHandle(mOutputUavHeapIndex);
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

    CameraParams &cameraParams = mConstantBuffer->cameraParams;
    XMStoreFloat4(&cameraParams.worldEyePos, mCamera->GetPosition());
    calculateCameraVariables(*mCamera, mCamera->GetAspectRatio(), &cameraParams.U, &cameraParams.V, &cameraParams.W);
    float xJitter = (mRngDist(mRng) - 0.5f) / float(width);
    float yJitter = (mRngDist(mRng) - 0.5f) / float(height);
    cameraParams.jitters = XMFLOAT2(xJitter, yJitter);
    cameraParams.frameCount = elapsedFrames;
    cameraParams.accumCount = mAccumCount++;

    XMVECTOR dirLightVector = XMVectorSet(0.3f, -0.2f, -1.0f, 0.0f);
    XMMATRIX rotation =  XMMatrixRotationY(sin(elapsedTime * 0.2f) * 3.14f * 0.5f);
    dirLightVector = XMVector4Transform(dirLightVector, rotation);
    XMStoreFloat4(&mConstantBuffer->directionalLight.forwardDir, dirLightVector);
    mConstantBuffer->directionalLight.color = dirLightColor;

    // XMVECTOR pointLightPos = XMVectorSet(sin(elapsedTime * 0.97f), sin(elapsedTime * 0.45f), sin(elapsedTime * 0.32f), 1.0f);
    // pointLightPos = XMVectorAdd(pointLightPos, XMVectorSet(0.0f, 0.5f, 1.0f, 0.0f));
    // pointLightPos = XMVectorMultiply(pointLightPos, XMVectorSet(0.221f, 0.049f, 0.221f, 1.0f));
    XMVECTOR pointLightPos = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMStoreFloat4(&mConstantBuffer->pointLight.worldPos, pointLightPos);
    mConstantBuffer->pointLight.color = pointLightColor;

    mConstantBuffer->options = mShaderDebugOptions;

    mConstantBuffer.CopyStagingToGpu(frameIndex);
}

void ProgressiveRaytracingPipeline::render(ID3D12GraphicsCommandList *commandList, UINT frameIndex, UINT width, UINT height)
{
    // Update shader table root arguments
    auto program = mRtBindings->getProgram();

    for (int rayType = 0; rayType < program->getHitProgramCount(); ++rayType) {
        for (int instance = 0; instance < mRtScene->getNumInstances(); ++instance) {
            auto &hitVars = mRtBindings->getHitVars(rayType, instance);
            hitVars->appendHeapRanges(mRtScene->getModel(instance)->getVertexBufferSrvHandle().ptr);
            hitVars->appendHeapRanges(mRtScene->getModel(instance)->getIndexBufferSrvHandle().ptr);
            hitVars->append32BitConstants((void*)&mMaterials[instance].params, SizeOfInUint32(MaterialParams));
        }
    }

    for (int rayType = 0; rayType < program->getMissProgramCount(); ++rayType) {
        auto &missVars = mRtBindings->getMissVars(rayType);
        missVars->appendHeapRanges(mTextureSrvGpuHandles[0].ptr);
        missVars->appendHeapRanges(mTextureSrvGpuHandles[1].ptr);
    }

    mRtBindings->apply(mRtContext, mRtState);

    // Set global root arguments
    mRtContext->bindDescriptorHeap();
    commandList->SetComputeRootSignature(program->getGlobalRootSignature());
    commandList->SetComputeRootConstantBufferView(GlobalRootSignatureParams::PerFrameConstantsSlot, mConstantBuffer.GpuVirtualAddress(frameIndex));
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
