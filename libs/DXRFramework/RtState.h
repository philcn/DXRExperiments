#pragma once

#include "RtPrefix.h"
#include "RtContext.h"
#include "RtProgram.h"
#include "Helpers/RaytracingPipelineGenerator.h"

namespace DXRFramework
{
    class RtState
    {
    public:
        using SharedPtr = std::shared_ptr<RtState>;
        
        static SharedPtr create(RtContext::SharedPtr context);
        ~RtState();
        
        void setProgram(RtProgram::SharedPtr pProg) { mProgram = pProg; }
        RtProgram::SharedPtr getProgram() const { return mProgram; }

        void setMaxTraceRecursionDepth(uint32_t maxDepth) { mMaxTraceRecursionDepth = maxDepth; }
        uint32_t getMaxTraceRecursionDepth() const { return mMaxTraceRecursionDepth; }

        void setMaxPayloadSize(uint32_t maxSize) { mMaxPayloadSize = maxSize; }
        uint32_t getMaxPayloadSize() const { return mMaxPayloadSize; }

        void setMaxAttributeSize(uint32_t maxSize) { mMaxAttributeSize = maxSize; }
        uint32_t getMaxAttributeSize() const { return mMaxAttributeSize; }

        ID3D12RaytracingFallbackStateObject *getFallbackRtso();
    private:
        RtState(RtContext::SharedPtr context);

        uint32_t mMaxTraceRecursionDepth = 1;
        uint32_t mMaxPayloadSize = 20;
        uint32_t mMaxAttributeSize = 8;

        RtProgram::SharedPtr mProgram;
        ComPtr<ID3D12RaytracingFallbackStateObject> mFallbackStateObject;

        ID3D12Device *mDevice;
        nv_helpers_dx12::RayTracingPipelineGenerator mPipelineGenerator;
    };
}
