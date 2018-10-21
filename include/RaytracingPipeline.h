#pragma once

#include "DXRFramework/RtContext.h"
#include "DXRFramework/RtScene.h"
#include "RaytracingHlslCompat.h"
#include "Camera.h"

class RaytracingPipeline
{
public:
    using SharedPtr = std::shared_ptr<RaytracingPipeline>;
    virtual ~RaytracingPipeline() = 0 {};

    virtual void userInterface() = 0;
    virtual void update(float elapsedTime, UINT elapsedFrames, UINT prevFrameIndex, UINT frameIndex, UINT width, UINT height) = 0;
    virtual void render(ID3D12GraphicsCommandList *commandList, UINT frameIndex, UINT width, UINT height) = 0;

    virtual void loadResources(ID3D12CommandQueue *uploadCommandQueue, UINT frameCount) = 0;
    virtual void createOutputResource(DXGI_FORMAT format, UINT width, UINT height) = 0;
    virtual void buildAccelerationStructures() = 0;

    struct Material 
    {
        MaterialParams params;
        // textures
    };

    virtual void addMaterial(Material material) = 0;
    virtual void setCamera(std::shared_ptr<Math::Camera> camera) = 0;
    virtual void setScene(DXRFramework::RtScene::SharedPtr scene) = 0;

    virtual int getNumOutputs() = 0;
    virtual ID3D12Resource *getOutputResource(UINT id) = 0;
    virtual D3D12_GPU_DESCRIPTOR_HANDLE getOutputUavHandle(UINT id) = 0;
    virtual D3D12_GPU_DESCRIPTOR_HANDLE getOutputSrvHandle(UINT id) = 0;

    virtual bool *isActive() = 0;
    virtual const char *getName() = 0;
};
