#pragma once

#include "DXRFramework/RtBindings.h"
#include "DXRFramework/RtContext.h"
#include "DXRFramework/RtProgram.h"
#include "DXRFramework/RtScene.h"
#include "DXRFramework/RtState.h"
#include "RaytracingHlslCompat.h"
#include "Camera.h"
#include <vector>
#include <random>

class RealtimeRaytracingPipeline
{
public:
    using SharedPtr = std::shared_ptr<RealtimeRaytracingPipeline>;

    static SharedPtr create(DXRFramework::RtContext::SharedPtr context) { return SharedPtr(new RealtimeRaytracingPipeline(context)); }
    ~RealtimeRaytracingPipeline();

    void userInterface();
    void update(float elapsedTime, UINT elapsedFrames, UINT prevFrameIndex, UINT frameIndex, UINT width, UINT height);
    void render(ID3D12GraphicsCommandList *commandList, UINT frameIndex, UINT width, UINT height);

    void loadResources(ID3D12CommandQueue *uploadCommandQueue, UINT frameCount);
    void createOutputResource(DXGI_FORMAT format, UINT width, UINT height);
    void buildAccelerationStructures();

    struct Material 
    {
        MaterialParams params;
        // textures
    };

    void addMaterial(Material material) { mMaterials.push_back(material); }
    void setCamera(std::shared_ptr<Math::Camera> camera) { mCamera = camera; }
    void setScene(DXRFramework::RtScene::SharedPtr scene);

    int getNumOutputs() { return kNumOutputResources; }
    ID3D12Resource *getOutputResource(UINT id) { return mOutputResource[id].Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE getOutputUavHandle(UINT id) { return mOutputUavGpuHandle[id]; }
    D3D12_GPU_DESCRIPTOR_HANDLE getOutputSrvHandle(UINT id) { return mOutputSrvGpuHandle[id]; }

    bool mActive;
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
    bool mAnimationPaused;

    std::mt19937 mRng;
    std::uniform_real_distribution<float> mRngDist;     
};
