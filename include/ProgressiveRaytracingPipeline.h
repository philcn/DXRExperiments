#pragma once

#include "RaytracingPipeline.h"
#include "DXRFramework/RtBindings.h"
#include "DXRFramework/RtContext.h"
#include "DXRFramework/RtProgram.h"
#include "DXRFramework/RtScene.h"
#include "DXRFramework/RtState.h"
#include "RaytracingHlslCompat.h"
#include "Camera.h"
#include <vector>
#include <random>

class ProgressiveRaytracingPipeline : public RaytracingPipeline
{
public:
    using SharedPtr = std::shared_ptr<ProgressiveRaytracingPipeline>;

    static SharedPtr create(DXRFramework::RtContext::SharedPtr context) { return SharedPtr(new ProgressiveRaytracingPipeline(context)); }
    virtual ~ProgressiveRaytracingPipeline();

    virtual void userInterface() override;
    virtual void update(float elAapsedTime, UINT elapsedFrames, UINT prevFrameIndex, UINT frameIndex, UINT width, UINT height) override;
    virtual void render(ID3D12GraphicsCommandList *commandList, UINT frameIndex, UINT width, UINT height) override;

    virtual void loadResources(ID3D12CommandQueue *uploadCommandQueue, UINT frameCount) override;
    virtual void createOutputResource(DXGI_FORMAT format, UINT width, UINT height) override;
    virtual void buildAccelerationStructures() override;

    virtual void addMaterial(Material material) override { mMaterials.push_back(material); }
    virtual void setCamera(std::shared_ptr<Math::Camera> camera) override { mCamera = camera; }
    virtual void setScene(DXRFramework::RtScene::SharedPtr scene) override;

    virtual int getNumOutputs() override { return 1; }
    virtual ID3D12Resource *getOutputResource(UINT id) override { return mOutputResource.Get(); }
    virtual D3D12_GPU_DESCRIPTOR_HANDLE getOutputUavHandle(UINT id) override { return mOutputUavGpuHandle; }
    virtual D3D12_GPU_DESCRIPTOR_HANDLE getOutputSrvHandle(UINT id) override { return mOutputSrvGpuHandle; }

    virtual bool *isActive() override { return &mActive; }
    virtual const char *getName() override { return "Progressive Ray Tracing Pipeline"; }
private:
    ProgressiveRaytracingPipeline(DXRFramework::RtContext::SharedPtr context);

    // Pipeline components
    DXRFramework::RtContext::SharedPtr mRtContext;
    DXRFramework::RtProgram::SharedPtr mRtProgram;
    DXRFramework::RtBindings::SharedPtr mRtBindings;
    DXRFramework::RtState::SharedPtr mRtState;

    // Scene description
    DXRFramework::RtScene::SharedPtr mRtScene;
    std::vector<Material> mMaterials;
    std::shared_ptr<Math::Camera> mCamera;

    // Resources
    ComPtr<ID3D12Resource> mOutputResource;
    UINT mOutputUavHeapIndex = UINT_MAX;
    UINT mOutputSrvHeapIndex = UINT_MAX;
    D3D12_GPU_DESCRIPTOR_HANDLE mOutputUavGpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE mOutputSrvGpuHandle;

    ConstantBuffer<PerFrameConstants> mConstantBuffer;

    std::vector<ComPtr<ID3D12Resource>> mTextureResources;
    std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> mTextureSrvGpuHandles;

    // Rendering states
    bool mActive;
    UINT mAccumCount;
    bool mFrameAccumulationEnabled;
    bool mAnimationPaused;
    DebugOptions mShaderDebugOptions;

    Math::Matrix4 mLastCameraVPMatrix;

    std::mt19937 mRng;
    std::uniform_real_distribution<float> mRngDist;     
};
