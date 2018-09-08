#pragma once
#include "stdafx.h"
#include "RtParams.h"

namespace DXRFramework
{
    RtParams::SharedPtr RtParams::create(UINT initialOffset)
    {
        return SharedPtr(new RtParams(initialOffset));
    }

    RtParams::RtParams(UINT initialOffset)
        : mInitialOffset(initialOffset), mRootOffset(initialOffset)
    {
    }

    RtParams::~RtParams() = default;

    void RtParams::applyRootParams(RtShader::SharedPtr shader, uint8_t *record)
    {
        memcpy(record, mData.data(), mRootOffset - mInitialOffset);
        mRootOffset = mInitialOffset;
    }

    void RtParams::applyRootParams(const RtProgram::HitGroup &hitGroup, uint8_t *record)
    {
        memcpy(record, mData.data(), mRootOffset - mInitialOffset);
        mRootOffset = mInitialOffset;
    }

    void RtParams::appendHeapRanges(UINT64 gpuHandle)
    {
        UINT size = sizeof(UINT64);
        mRootOffset = Align(mRootOffset, size);
        assert((mRootOffset % size) == 0);

        if (mData.size() < mRootOffset - mInitialOffset + size) {
            OutputDebugString(L"RtParams appendDescriptor: writing shader params out of bounds");
        }

        memcpy(mData.data() + mRootOffset - mInitialOffset, &gpuHandle, size);
        mRootOffset += size;
    }

    void RtParams::appendDescriptor(WRAPPED_GPU_POINTER discriptorHandle)
    {
        UINT size = sizeof(WRAPPED_GPU_POINTER);
        mRootOffset = Align(mRootOffset, size);
        assert((mRootOffset % size) == 0);

        if (mData.size() < mRootOffset - mInitialOffset + size) {
            OutputDebugString(L"RtParams appendDescriptor: writing shader params out of bounds");
        }

        memcpy(mData.data() + mRootOffset - mInitialOffset, &discriptorHandle, size);
        mRootOffset += size;
    }
    
    void RtParams::append32BitConstants(void *constants, UINT num32BitConstants)
    {
        UINT size = sizeof(uint32_t) * num32BitConstants;
        mRootOffset = Align(mRootOffset, sizeof(uint32_t));
        assert((mRootOffset % sizeof(uint32_t)) == 0);

        if (mData.size() < mRootOffset - mInitialOffset + size) {
            OutputDebugString(L"RtParams appendDescriptor: writing shader params out of bounds");
        }

        memcpy(mData.data() + mRootOffset - mInitialOffset, constants, size);
        mRootOffset += size;
    }
}
