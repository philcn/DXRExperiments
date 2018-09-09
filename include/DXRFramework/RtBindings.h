#pragma once
#include "RtContext.h"
#include "RtProgram.h"
#include "RtParams.h"
#include "RtState.h"
#include "RtScene.h"

namespace DXRFramework
{
    class RtBindings
    {
    public:
        using SharedPtr = std::shared_ptr<RtBindings>;

        static SharedPtr create(RtContext::SharedPtr context, RtProgram::SharedPtr program, RtScene::SharedPtr scene);
        ~RtBindings();
        
        void apply(RtContext::SharedPtr context, RtState::SharedPtr state);

        ID3D12Resource *getShaderTable() const { return mShaderTable.Get(); }
        uint32_t getRecordSize() const { return mRecordSize; }
        uint32_t getRayGenRecordIndex() const { return kRayGenRecordIndex; }
        uint32_t getFirstMissRecordIndex() const { return kFirstMissRecordIndex; }
        uint32_t getFirstHitRecordIndex() const { return mFirstHitVarEntry; }
        uint32_t getHitProgramsCount() const { return mHitProgCount; }
        uint32_t getMissProgramsCount() const { return mMissProgCount; }

        const RtParams::SharedPtr& getHitVars(uint32_t rayID, uint32_t meshID) { return mHitParams[rayID][meshID]; }
        const RtParams::SharedPtr& getRayGenVars() { return mRayGenParams; }
        const RtParams::SharedPtr& getMissVars(uint32_t rayID) { return mMissParams[rayID]; }
        const RtParams::SharedPtr& getGlobalVars() { return mGlobalParams; }

    private:
        RtBindings(RtContext::SharedPtr context, RtProgram::SharedPtr program, RtScene::SharedPtr scene); 
        bool init(RtContext::SharedPtr context);

        void applyRtProgramVars(uint8_t *record, RtShader::SharedPtr shader, ID3D12RaytracingFallbackStateObject *rtso, RtParams::SharedPtr params);
        void applyRtProgramVars(uint8_t *record, const RtProgram::HitGroup &hitGroup, ID3D12RaytracingFallbackStateObject *rtso, RtParams::SharedPtr params);

        RtProgram::SharedPtr mProgram;
        RtScene::SharedPtr mScene;

        ComPtr<ID3D12Resource> mShaderTable;
        std::vector<uint8_t> mShaderTableData;

        static const uint32_t kRayGenRecordIndex = 0;
        static const uint32_t kFirstMissRecordIndex = 1;
        uint32_t mMissProgCount = 0;
        uint32_t mHitProgCount = 0;
        uint32_t mFirstHitVarEntry = 0;

        uint32_t mRecordSize;
        uint32_t mProgramIdentifierSize;
        
        uint8_t *getRayGenRecordPtr();
        uint8_t *getMissRecordPtr(uint32_t missId);
        uint8_t *getHitRecordPtr(uint32_t hitId, uint32_t meshId);        

        RtParams::SharedPtr mGlobalParams;
        RtParams::SharedPtr mRayGenParams;
        std::vector<std::vector<RtParams::SharedPtr>> mHitParams;
        std::vector<RtParams::SharedPtr> mMissParams;
    };
}
