#pragma once

#include "DXRFramework/RtBindings.h"
#include "DXRFramework/RtContext.h"
#include "DXRFramework/RtProgram.h"
#include "DXRFramework/RtRenderer.h"
#include "DXRFramework/RtScene.h"
#include "DXRFramework/RtState.h"

class ProgressiveRaytracingPipeline
{
public:
    using SharedPtr = std::shared_ptr<ProgressiveRaytracingPipeline>;

    static SharedPtr create(DXRFramework::RtContext::SharedPtr context) { return SharedPtr(new ProgressiveRaytracingPipeline(context)); }
    ~ProgressiveRaytracingPipeline();

    void setScene(DXRFramework::RtScene::SharedPtr scene);
    void buildAccelerationStructures();

    DXRFramework::RtRenderer::SharedPtr getRenderer() { return mRtRenderer; }

    void update(float elapsedTime, UINT elapsedFrames, UINT prevFrameIndex, UINT frameIndex, UINT width, UINT height);
    void render(ID3D12GraphicsCommandList *commandList, UINT frameIndex, UINT width, UINT height);

private:
    ProgressiveRaytracingPipeline(DXRFramework::RtContext::SharedPtr context);

    DXRFramework::RtRenderer::SharedPtr mRtRenderer;
    DXRFramework::RtProgram::SharedPtr mRtProgram;
    DXRFramework::RtBindings::SharedPtr mRtBindings;
    DXRFramework::RtState::SharedPtr mRtState;

    DXRFramework::RtContext::SharedPtr mRtContext;
    DXRFramework::RtScene::SharedPtr mRtScene;
};
