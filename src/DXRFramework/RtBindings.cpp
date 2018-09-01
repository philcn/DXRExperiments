#include "RtBindings.h"

namespace DXRFramework
{
    RtBindings::SharedPtr RtBindings::create(RtProgram::SharedPtr program)
    {
        return SharedPtr(new RtBindings(program));
    }

    RtBindings::RtBindings()
    {
    }

    RtBindings::~RtBindings() = default;

    uuint8_t* RtBindings::getRayGenRecordPtr()
    {
        return mShaderTableData.data() + (kRayGenRecordIndex * mRecordSize);
    }

    uint8_t* RtBindings::getMissRecordPtr(uint32_t missId)
    {
        assert(missId < mMissProgCount);
        uint32_t offset = mRecordSize * (kFirstMissRecordIndex + missId);
        return mShaderTableData.data() + offset;
    }

    uint8_t* RtBindings::getHitRecordPtr(uint32_t hitId, uint32_t meshId)
    {   
        assert(hitId < mHitProgCount);
        uint32_t meshIndex = mFirstHitVarEntry + mHitProgCount * meshId;    // base record of the requested mesh
        uint32_t recordIndex = meshIndex + hitId;
        return mShaderTableData.data() + (recordIndex * mRecordSize);
    }
}
