#include "stdafx.h"
#include "ProgressiveRaytracingPipeline.h"
#include "CompiledShaders/ShaderLibrary.hlsl.h"

using namespace DXRFramework;

ProgressiveRaytracingPipeline::ProgressiveRaytracingPipeline(RtContext::SharedPtr context) :
    mRtContext(context)
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
}

ProgressiveRaytracingPipeline::~ProgressiveRaytracingPipeline() = default;

void ProgressiveRaytracingPipeline::setScene(RtScene::SharedPtr scene)
{
    mRtScene = scene;
    mRtRenderer = RtRenderer::create(mRtContext, scene);
    mRtBindings = RtBindings::create(mRtContext, mRtProgram, scene);
}

void ProgressiveRaytracingPipeline::buildAccelerationStructures()
{
    mRtScene->build(mRtContext, mRtProgram->getHitProgramCount());
}

void ProgressiveRaytracingPipeline::update(float elapsedTime, UINT elapsedFrames, UINT prevFrameIndex, UINT frameIndex, UINT width, UINT height)
{
    mRtRenderer->update(elapsedTime, elapsedFrames, prevFrameIndex, frameIndex, width, height);
}

void ProgressiveRaytracingPipeline::render(ID3D12GraphicsCommandList *commandList, UINT frameIndex, UINT width, UINT height)
{
    mRtRenderer->render(commandList, mRtBindings, mRtState, frameIndex, width, height);
}
