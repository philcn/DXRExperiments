#pragma once
#include "stdafx.h"
#include "RtParams.h"

namespace DXRFramework
{
    RtParams::SharedPtr RtParams::create()
    {
        return SharedPtr(new RtParams());
    }

    RtParams::RtParams()
        : mBytesWritten(0)
    {
    }

    RtParams::~RtParams() = default;

    void RtParams::applyRootParams(RtShader::SharedPtr shader, uint8_t *record)
    {
        memcpy(record, mData.data(), mData.size());
        mBytesWritten = 0;
    }

    void RtParams::applyRootParams(const RtProgram::HitGroup &hitGroup, uint8_t *record)
    {
        memcpy(record, mData.data(), mData.size());
        mBytesWritten = 0;
    }

    void RtParams::appendCBV(WRAPPED_GPU_POINTER cbvHandle)
    {
        memcpy(mBytesWritten + mData.data(), &cbvHandle, sizeof(cbvHandle));
        mBytesWritten += sizeof(cbvHandle);
    }
    
    void RtParams::appendSRV(WRAPPED_GPU_POINTER srvHandle)
    {
        memcpy(mBytesWritten + mData.data(), &srvHandle, sizeof(srvHandle));
        mBytesWritten += sizeof(srvHandle);
    }
    
    void RtParams::append32BitConstants(void *constants, UINT num32BitConstants)
    {
        memcpy(mBytesWritten + mData.data(), constants, sizeof(uint32_t) * num32BitConstants);
        mBytesWritten += sizeof(uint32_t) * num32BitConstants;
    }

    void RtParams::appendHeapRanges(UINT64 gpuHandle)
    {
        memcpy(mBytesWritten + mData.data(), &gpuHandle, sizeof(UINT64));
        mBytesWritten += 8;
    }
}
