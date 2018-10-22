#pragma once

#include "RtPrefix.h"
#include "RtShader.h"
#include "RtProgram.h"

namespace DXRFramework
{
    class RtParams
    {
    public:
        using SharedPtr = std::shared_ptr<RtParams>;

        static SharedPtr create(UINT initialOffset = 0);
        ~RtParams();

        void allocateStorage(UINT sizeInBytes) { mData.resize(sizeInBytes); }

        void appendHeapRanges(UINT64 gpuHandle);
        void appendDescriptor(WRAPPED_GPU_POINTER discriptorHandle);
        void append32BitConstants(void *constants, UINT num32BitConstants);

        void applyRootParams(RtShader::SharedPtr shader, uint8_t *record);
        void applyRootParams(const RtProgram::HitGroup &hitGroup, uint8_t *record);

    private:
        RtParams(UINT initialOffset);

        // We are using the following layout for the shader record:
        //
        // +--------------------+----------+------------+-----+------------+
        // |                    |          |            | ... |            |
        // |       Shader       | Optional |    Root    | ... |    Root    |
        // |     Identifier     |  padding | Argument 1 | ... | Argument N |
        // |                    |          |            | ... |            |
        // +--------------------+----------+------------+-----+------------+
        // |                    |\                            |           /
        // |                    | +--------------- mData -----|----------+
        // v                    v                             v
        // 0              mInitialOffset                mRootOffset(N)
        //
        // As per DXR spec - the argument layout is defined by packing each argument 
        // with padding as needed to align each to its individual (defined) size.

        std::vector<uint8_t> mData;

        // Current write offset relative to shader record
        UINT mRootOffset;

        // Start of root argument section in shader record, equals to shader identifier size
        UINT mInitialOffset;
    };
}
