#include "pch.h"
#include "RealtimeRaytracingPipeline.h"
#include "CompiledShaders/RealtimeRaytracing.hlsl.h"
#include "WICTextureLoader.h"
#include "DDSTextureLoader.h"
#include "ResourceUploadBatch.h"
#include "Helpers/DirectXRaytracingHelper.h"
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

RealtimeRaytracingPipeline::RealtimeRaytracingPipeline(RtContext::SharedPtr context) :
    mRtContext(context),
    mAnimationPaused(true),
    mActive(true)
{
    RtProgram::Desc programDesc;
    {
        std::vector<std::wstring> libraryExports = { L"RayGen", L"PrimaryClosestHit", L"PrimaryMiss", L"ShadowClosestHit", L"ShadowAnyHit", L"ShadowMiss" };
        programDesc.addShaderLibrary(g_pRealtimeRaytracing, ARRAYSIZE(g_pRealtimeRaytracing), libraryExports);
        programDesc.setRayGen("RayGen");
        programDesc.addHitGroup(0, "PrimaryClosestHit", "").addMiss(0, "PrimaryMiss");
        programDesc.addHitGroup(1, "ShadowClosestHit", "ShadowAnyHit").addMiss(1, "ShadowMiss");
        programDesc.configureGlobalRootSignature([] (RootSignatureGenerator &config) {
            // GlobalRootSignatureParams::AccelerationStructureSlot
            config.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0 /* t0 */); 
            // GlobalRootSignatureParams::OutputViewSlot
            config.AddHeapRangesParameter({{0 /* u0-u1 */, 2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0}}); 
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
    mRtState->setMaxAttributeSize(8);
    mRtState->setMaxPayloadSize(60);

    auto now = std::chrono::high_resolution_clock::now();
    auto msTime = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    mRng = std::mt19937(uint32_t(msTime.time_since_epoch().count()));
}

RealtimeRaytracingPipeline::~RealtimeRaytracingPipeline() = default;

void RealtimeRaytracingPipeline::setScene(RtScene::SharedPtr scene)
{
    mRtScene = scene;
    mRtBindings = RtBindings::create(mRtContext, mRtProgram, scene);
}

void RealtimeRaytracingPipeline::buildAccelerationStructures()
{
    mRtScene->build(mRtContext, mRtProgram->getHitProgramCount());
}

void RealtimeRaytracingPipeline::loadResources(ID3D12CommandQueue *uploadCommandQueue, UINT frameCount)
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

void RealtimeRaytracingPipeline::createOutputResource(DXGI_FORMAT format, UINT width, UINT height)
{
    auto device = mRtContext->getDevice();


    for (int i = 0; i < kNumOutputResources; ++i) {
        AllocateUAVTexture(device, format, width, height, mOutputResource[i].ReleaseAndGetAddressOf(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    // Make sure UAV handles are continuous on the descriptor heap for descriptor table binding
    for (int i = 0; i < kNumOutputResources; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle;
        mOutputUavHeapIndex[i] = mRtContext->allocateDescriptor(&uavCpuHandle, mOutputUavHeapIndex[i]);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(mOutputResource[i].Get(), nullptr, &uavDesc, uavCpuHandle);

        mOutputUavGpuHandle[i] = mRtContext->getDescriptorGPUHandle(mOutputUavHeapIndex[i]);
    }

    for (int i = 0; i < kNumOutputResources; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle;
        mOutputSrvHeapIndex[i] = mRtContext->allocateDescriptor(&srvCpuHandle, mOutputSrvHeapIndex[i]);
        mOutputSrvGpuHandle[i] = mRtContext->createTextureSRVHandle(mOutputResource[i].Get(), false, mOutputSrvHeapIndex[i]);
    }
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

void RealtimeRaytracingPipeline::update(float elapsedTime, UINT elapsedFrames, UINT prevFrameIndex, UINT frameIndex, UINT width, UINT height)
{
    if (mAnimationPaused) {
        elapsedTime = 142.0f;
    }

    CameraParams &cameraParams = mConstantBuffer->cameraParams;
    XMStoreFloat4(&cameraParams.worldEyePos, mCamera->GetPosition());
    calculateCameraVariables(*mCamera, mCamera->GetAspectRatio(), &cameraParams.U, &cameraParams.V, &cameraParams.W);
    float xJitter = (mRngDist(mRng) - 0.5f) / float(width);
    float yJitter = (mRngDist(mRng) - 0.5f) / float(height);
    cameraParams.jitters = XMFLOAT2(xJitter, yJitter);
    cameraParams.frameCount = elapsedFrames;
    cameraParams.accumCount = 0;

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

    mConstantBuffer->options.environmentStrength = 1.0f;

    mConstantBuffer.CopyStagingToGpu(frameIndex);
}

void RealtimeRaytracingPipeline::render(ID3D12GraphicsCommandList *commandList, UINT frameIndex, UINT width, UINT height)
{
    // Update shader table root arguments
    auto program = mRtBindings->getProgram();

    for (UINT rayType = 0; rayType < program->getHitProgramCount(); ++rayType) {
        for (UINT instance = 0; instance < mRtScene->getNumInstances(); ++instance) {
            auto &hitVars = mRtBindings->getHitVars(rayType, instance);
            hitVars->appendHeapRanges(mRtScene->getModel(instance)->getVertexBufferSrvHandle().ptr);
            hitVars->appendHeapRanges(mRtScene->getModel(instance)->getIndexBufferSrvHandle().ptr);
            hitVars->append32BitConstants((void*)&mMaterials[instance].params, SizeOfInUint32(MaterialParams));
        }
    }

    for (UINT rayType = 0; rayType < program->getMissProgramCount(); ++rayType) {
        auto &missVars = mRtBindings->getMissVars(rayType);
        missVars->appendHeapRanges(mTextureSrvGpuHandles[0].ptr);
        missVars->appendHeapRanges(mTextureSrvGpuHandles[1].ptr);
    }

    mRtBindings->apply(mRtContext, mRtState);

    // Set global root arguments
    mRtContext->bindDescriptorHeap();
    commandList->SetComputeRootSignature(program->getGlobalRootSignature());
    commandList->SetComputeRootConstantBufferView(GlobalRootSignatureParams::PerFrameConstantsSlot, mConstantBuffer.GpuVirtualAddress(frameIndex));
    commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, mOutputUavGpuHandle[0]);
    mRtContext->getFallbackCommandList()->SetTopLevelAccelerationStructure(GlobalRootSignatureParams::AccelerationStructureSlot, mRtScene->getTlasWrappedPtr());

    mRtContext->raytrace(mRtBindings, mRtState, width, height, 3);

    for (int i = 0; i < kNumOutputResources; ++i) {
        mRtContext->insertUAVBarrier(mOutputResource[i].Get());
    }
}

void RealtimeRaytracingPipeline::userInterface()
{
    ui::Begin("Lighting");
    {
        ui::ColorPicker4("Point Light", (float*)&pointLightColor);
        ui::ColorPicker4("Directional Light", (float*)&dirLightColor);
    }
    ui::End();

    ui::Begin("Material");
    {
        ui::SliderFloat3("Albedo", &mMaterials[0].params.albedo.x, 0.0f, 1.0f);
        ui::SliderFloat3("Specular", &mMaterials[0].params.specular.x, 0.0f, 1.0f);
        ui::SliderFloat("Reflectivity", &mMaterials[0].params.reflectivity, 0.0f, 1.0f);
        ui::SliderFloat("Roughness", &mMaterials[0].params.roughness, 0.0f, 1.0f);
    }
    ui::End();

    ui::Begin("Realtime Raytracing");
    {

    }
    ui::End();
}
