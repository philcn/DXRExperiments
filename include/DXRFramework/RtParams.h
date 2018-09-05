#pragma once
#include "RtShader.h"
#include "RtProgram.h"
#include "D3D12RaytracingFallback.h"

namespace DXRFramework
{
    class RtParams
    {
    public:
        using SharedPtr = std::shared_ptr<RtParams>;

        static SharedPtr create();
        ~RtParams();

        void allocateStorage(UINT sizeInBytes) { mData.resize(sizeInBytes); }
        void appendCBV(WRAPPED_GPU_POINTER cbvHandle);
        void appendSRV(WRAPPED_GPU_POINTER srvHandle);
        void append32BitConstants(void *constants, UINT num32BitConstants);
        void appendHeapRanges(UINT64 gpuHandle);

        void applyRootParams(RtShader::SharedPtr shader, uint8_t *record);
        void applyRootParams(const RtProgram::HitGroup &hitGroup, uint8_t *record);

    private:
        RtParams();

        std::vector<uint8_t> mData;
        UINT mBytesWritten;
    };
}
