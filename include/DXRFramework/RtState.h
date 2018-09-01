#pragma once
#include "RtProgram.h"

namespace DXRFramework
{
    class RtState
    {
    public:
        using SharedPtr = std::shared_ptr<RtState>;
        
        static SharedPtr create();
        ~RtState();
        
        void setProgram(RtProgram::SharedPtr pProg) { mProgram = pProg; }
        RtProgram::SharedPtr getProgram() const { return mProgram; }

        void setMaxTraceRecursionDepth(uint32_t maxDepth) { mMaxTraceRecursionDepth = maxDepth; }
        uint32_t getMaxTraceRecursionDepth() const { return mMaxTraceRecursionDepth; }

        // RtStateObject::SharedPtr getRtso();
    private:
        RtState();

        RtProgram::SharedPtr mProgram;
        uint32_t mMaxTraceRecursionDepth = 1;
    };
}


        
  