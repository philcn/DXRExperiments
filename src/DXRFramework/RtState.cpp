#include "stdafx.h"
#include "RtState.h"
#include <codecvt>

namespace DXRFramework
{
    RtState::SharedPtr RtState::create(RtContext::SharedPtr context)
    {
        return SharedPtr(new RtState(context));
    }

    RtState::RtState(RtContext::SharedPtr context)
        : mDevice(context->getDevice()), mPipelineGenerator(context->getDevice(), context->getFallbackDevice())
    {
    }

    RtState::~RtState() = default;

    ID3D12RaytracingFallbackStateObject *RtState::getFallbackRtso()
    {
        ThrowIfFalse(mProgram != nullptr, L"RtState program not set");
        ThrowIfFalse(mDevice != nullptr, L"RtState doesn't have a valid device");

        if (mFallbackStateObject) {
            return mFallbackStateObject.Get();
        }

        // Load DXIL libraries
        for (auto library : mProgram->getShaderLibraries()) {
            auto &dxilLibrary = library->mLibDesc.DXILLibrary;
            mPipelineGenerator.AddLibrary(dxilLibrary.pShaderBytecode, dxilLibrary.BytecodeLength, library->mExportedSymbols);
        }
        
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

        // Add hit groups
        for (int i = 0; i < mProgram->getHitProgramCount(); ++i) {
            auto &hitGroup = mProgram->getHitProgram(i);
            std::wstring closestHitSymbol = converter.from_bytes(hitGroup.mClosestHit->mEntryPoint);
            std::wstring anyHitSymbol = hitGroup.mAnyHit ? converter.from_bytes(hitGroup.mAnyHit->mEntryPoint) : L"";
            std::wstring intersectionSymbol = hitGroup.mIntersection ? converter.from_bytes(hitGroup.mIntersection->mEntryPoint) : L"";
            std::wstring hitGroupName = converter.from_bytes(hitGroup.mExportName);
            mPipelineGenerator.AddHitGroup(hitGroupName, closestHitSymbol, anyHitSymbol, intersectionSymbol);
            mPipelineGenerator.AddRootSignatureAssociation(hitGroup.mClosestHit->mLocalRootSignature.Get(), {hitGroupName});
        }

        // Add miss shader local root signature association
        for (int i = 0; i < mProgram->getMissProgramCount(); ++i) {
            auto &missProgram = mProgram->getMissProgram(i);
            std::wstring missProgramName = converter.from_bytes(missProgram->mEntryPoint);
            mPipelineGenerator.AddRootSignatureAssociation(missProgram->mLocalRootSignature.Get(), {missProgramName});
        }

        // Add raygen shader local root signature association
        auto &raygenProgram = mProgram->getRayGenProgram();
        std::wstring raygenProgramName = converter.from_bytes(raygenProgram->mEntryPoint);
        mPipelineGenerator.AddRootSignatureAssociation(raygenProgram->mLocalRootSignature.Get(), {raygenProgramName});

        // Set pipeline attributes
        mPipelineGenerator.SetMaxPayloadSize(4 * sizeof(float) + 1 * sizeof(uint32_t));
        mPipelineGenerator.SetMaxAttributeSize(2 * sizeof(float));
        mPipelineGenerator.SetMaxRecursionDepth(mMaxTraceRecursionDepth);

        mFallbackStateObject = mPipelineGenerator.FallbackGenerate(mProgram->getGlobalRootSignature());
        return mFallbackStateObject.Get();
    }
}
