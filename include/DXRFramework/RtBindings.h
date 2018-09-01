#pragma once
#include "RtProgram.h"

namespace DXRFramework
{
    class RtBindings
    {
    public:
        using SharedPtr = std::shared_ptr<RtBindings>;

        static SharedPtr create(RtProgram::SharedPtr program);
        ~RtBindings();
        
    private:
        RtBindings(RtProgram::SharedPtr program) : mProgram(program) {}

        RtProgram::SharedPtr mProgram;

        ComPtr<ID3D12Resource> mShaderTable;
        std::vector<uint8_t> mShaderTableData;

        static const uint32_t kRayGenRecordIndex = 0;
        static const uint32_t kFirstMissRecordIndex = 1;
        uint32_t mMissProgCount = 0;
        uint32_t mHitProgCount = 0;
        uint32_t mFirstHitVarEntry = 0;

        uint32_t mRecordSize;
        uint32_t mProgramIdentifierSize;
        
        uint8_t* getRayGenRecordPtr();
        uint8_t* getMissRecordPtr(uint32_t missId);
        uint8_t* getHitRecordPtr(uint32_t hitId, uint32_t meshId);
        
        // Global vars

        // Hit, miss, raygen vars
    };
}
