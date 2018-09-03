#pragma once
#include "RtContext.h"
#include "RtProgram.h"
#include "nv_helpers_dx12/RaytracingPipelineGenerator.h"

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

        ID3D12RaytracingFallbackStateObject *getFallbackRtso();
    private:
        RtState(RtContext::SharedPtr context);

        RtProgram::SharedPtr mProgram;
        uint32_t mMaxTraceRecursionDepth = 1;
        ComPtr<ID3D12RaytracingFallbackStateObject> mFallbackStateObject;

        ID3D12Device *mDevice;
        nv_helpers_dx12::RayTracingPipelineGenerator mPipelineGenerator;
    };
}
