#pragma once
#include "RtScene.h"
#include "RtState.h"
#include "RtBindings.h"
#include "RtContext.h"
#include "RaytracingHlslCompat.h"
#include "Camera.h"
#include <random>

namespace DXRFramework
{
    class RtRenderer
    {
    public:
        using SharedPtr = std::shared_ptr<RtRenderer>;

        static SharedPtr create(RtContext::SharedPtr context, RtScene::SharedPtr scene) { return SharedPtr(new RtRenderer(context, scene)); }
        ~RtRenderer();

        void update(float elapsedTime, UINT elapsedFrames, UINT prevFrameIndex, UINT frameIndex, UINT width, UINT height);
        void render(ID3D12GraphicsCommandList *commandList, RtBindings::SharedPtr bindings, RtState::SharedPtr state, UINT frameIndex, UINT width, UINT height);

        void loadResources(ID3D12CommandQueue *uploadCommandQueue, UINT frameCount);
        void createOutputResource(DXGI_FORMAT format, UINT width, UINT height);
        ID3D12Resource *getOutputResource() { return mOutputResource.Get(); }

        struct Material 
        {
            MaterialParams params;
            // textures
        };

        void addMaterial(Material material) { mMaterials.push_back(material); }

        void setCamera(std::shared_ptr<Math::Camera> camera) { mCamera = camera; }

    private:
        RtRenderer(RtContext::SharedPtr context, RtScene::SharedPtr scene);

        void createConstantBuffers(UINT frameCount);
        void userInterface();

        RtContext::SharedPtr mContext;

        // Scene description
        RtScene::SharedPtr mScene;
        std::vector<Material> mMaterials;

        // Output resource
        ComPtr<ID3D12Resource> mOutputResource;
        D3D12_GPU_DESCRIPTOR_HANDLE mOutputResourceUAVGpuDescriptor;

        // Constant buffer resources
        UINT mAlignedPerFrameConstantBufferSize;
        ComPtr<ID3D12Resource> mPerFrameConstantBuffer;
        D3D12_GPU_DESCRIPTOR_HANDLE mPerFrameCBVGpuHandle;
        void *mMappedPerFrameConstantsData;

        // Render states
        UINT mAccumCount;
        bool mFrameAccumulationEnabled;
        bool mAnimationPaused;
        DebugOptions mShaderDebugOptions;

        std::shared_ptr<Math::Camera> mCamera;
        Math::Matrix4 mLastCameraVPMatrix;

        // Random number generators
        std::mt19937 mRng;
        std::uniform_real_distribution<float> mRngDist;     
    };
}
