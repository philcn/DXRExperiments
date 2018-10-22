#pragma once

#include "RaytracingPipeline.h"
#include "RtBindings.h"
#include "RtContext.h"
#include "RtProgram.h"
#include "RtScene.h"
#include "RtState.h"
#include "RaytracingHlslCompat.h"
#include "Camera.h"
#include <vector>
#include <random>

class RealtimeRaytracingPipeline : public RaytracingPipeline
{
public:
    using SharedPtr = std::shared_ptr<RealtimeRaytracingPipeline>;

    static SharedPtr create(DXRFramework::RtContext::SharedPtr context) { return SharedPtr(new RealtimeRaytracingPipeline(context)); }
    virtual ~RealtimeRaytracingPipeline();

    virtual void userInterface() override;
    virtual void update(float elapsedTime, UINT elapsedFrames, UINT prevFrameIndex, UINT frameIndex, UINT width, UINT height) override;
    virtual void render(ID3D12GraphicsCommandList *commandList, UINT frameIndex, UINT width, UINT height) override;

    virtual void loadResources(ID3D12CommandQueue *uploadCommandQueue, UINT frameCount) override;
    virtual void createOutputResource(DXGI_FORMAT format, UINT width, UINT height) override;
    virtual void buildAccelerationStructures() override;

    virtual void addMaterial(Material material) override { mMaterials.push_back(material); }
    virtual void setCamera(std::shared_ptr<Math::Camera> camera) override { mCamera = camera; }
    virtual void setScene(DXRFramework::RtScene::SharedPtr scene) override;

    virtual int getNumOutputs() override { return kNumOutputResources; }
    virtual ID3D12Resource *getOutputResource(UINT id) override { return mOutputResource[id].Get(); }
    virtual D3D12_GPU_DESCRIPTOR_HANDLE getOutputUavHandle(UINT id) override { return mOutputUavGpuHandle[id]; }
    virtual D3D12_GPU_DESCRIPTOR_HANDLE getOutputSrvHandle(UINT id) override { return mOutputSrvGpuHandle[id]; }

    virtual bool *isActive() override { return &mActive; }
    virtual const char *getName() override { return "Realtime Ray Tracing Pipeline"; }
private:
    RealtimeRaytracingPipeline(DXRFramework::RtContext::SharedPtr context);

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
    const int kNumOutputResources = 2;
    ComPtr<ID3D12Resource> mOutputResource[2];
    UINT mOutputUavHeapIndex[2] = { UINT_MAX, UINT_MAX };
    UINT mOutputSrvHeapIndex[2] = { UINT_MAX, UINT_MAX };
    D3D12_GPU_DESCRIPTOR_HANDLE mOutputUavGpuHandle[2];
    D3D12_GPU_DESCRIPTOR_HANDLE mOutputSrvGpuHandle[2];

    ConstantBuffer<PerFrameConstants> mConstantBuffer;

    std::vector<ComPtr<ID3D12Resource>> mTextureResources;
    std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> mTextureSrvGpuHandles;

    // Rendering states
    bool mActive;
    bool mAnimationPaused;

    std::mt19937 mRng;
    std::uniform_real_distribution<float> mRngDist;     
};
