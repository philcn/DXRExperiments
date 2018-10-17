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

class ProgressiveRaytracingPipeline
{
public:
    using SharedPtr = std::shared_ptr<ProgressiveRaytracingPipeline>;

    static SharedPtr create(DXRFramework::RtContext::SharedPtr context) { return SharedPtr(new ProgressiveRaytracingPipeline(context)); }
    ~ProgressiveRaytracingPipeline();

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

    ID3D12Resource *getOutputResource() { return mOutputResource.Get(); }

private:
    ProgressiveRaytracingPipeline(DXRFramework::RtContext::SharedPtr context);

    void userInterface();

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
    UINT mOutputUavHeapIndex;
    D3D12_GPU_DESCRIPTOR_HANDLE mOutputUavGpuHandle;

    ComPtr<ID3D12Resource> mConstantBufferResource;
    UINT mConstantBufferAlignedSize;
    void *mConstantBufferData;

    std::vector<ComPtr<ID3D12Resource>> mTextureResources;
    std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> mTextureSrvGpuHandles;

    // Rendering states
    UINT mAccumCount;
    bool mFrameAccumulationEnabled;
    bool mAnimationPaused;
    DebugOptions mShaderDebugOptions;

    Math::Matrix4 mLastCameraVPMatrix;

    std::mt19937 mRng;
    std::uniform_real_distribution<float> mRngDist;     
};
