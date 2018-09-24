#include "stdafx.h"
#include "RtBindings.h"
#include "nv_helpers_dx12/DXRHelper.h"
#include "RaytracingHlslCompat.h"
#include <codecvt>

namespace DXRFramework
{
    RtBindings::SharedPtr RtBindings::create(RtContext::SharedPtr context, RtProgram::SharedPtr program, RtScene::SharedPtr scene)
    {
        return SharedPtr(new RtBindings(context, program, scene));
    }

    RtBindings::RtBindings(RtContext::SharedPtr context, RtProgram::SharedPtr program, RtScene::SharedPtr scene)
        : mProgram(program), mScene(scene)
    {
        init(context);
    }

    RtBindings::~RtBindings() = default;

    bool RtBindings::init(RtContext::SharedPtr context)
    {
        mHitProgCount = mProgram->getHitProgramCount();
        mMissProgCount = mProgram->getMissProgramCount();
        mFirstHitVarEntry = kFirstMissRecordIndex + mMissProgCount;
        mProgramIdentifierSize = context->getFallbackDevice()->GetShaderIdentifierSize();

        mGlobalParams = RtParams::create(); // mProgram->getGlobalRootSignature();

        // Find the max root-signature size, create params with root signatures and reserve space
        uint32_t maxRootSigSize = 4 /* padding */ + sizeof(UINT64) * 2 + sizeof(MaterialParams); // TEMP

        mRayGenParams = RtParams::create(mProgramIdentifierSize); // mProgram->getRayGenProgram();
        mRayGenParams->allocateStorage(maxRootSigSize);
        // update maxRootSigSize

        UINT recordCountPerHit = mScene->getNumInstances();
        mHitParams.resize(mHitProgCount);
        for (UINT i = 0 ; i < mHitProgCount; ++i) {
            mHitParams[i].resize(recordCountPerHit);
            for (UINT j = 0; j < recordCountPerHit; ++j) {
                mHitParams[i][j] = RtParams::create(mProgramIdentifierSize); // mProgram->getHitProgram(i);
                mHitParams[i][j]->allocateStorage(maxRootSigSize);
                // update maxRootSigSize
            }
        }

        mMissParams.resize(mMissProgCount);
        for (UINT i = 0 ; i < mMissProgCount; ++i) {
            mMissParams[i] = RtParams::create(mProgramIdentifierSize); // mProgram->getMissProgram(i);
            mMissParams[i]->allocateStorage(maxRootSigSize);
            // update maxRootSigSize
        }

        // Allocate shader table
        UINT hitEntries = recordCountPerHit * mHitProgCount;
        UINT numEntries = mMissProgCount + hitEntries + 1 /* ray-gen */;

        mRecordSize = mProgramIdentifierSize + maxRootSigSize;
        mRecordSize = ROUND_UP(mRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        assert(mRecordSize != 0);

        UINT shaderTableSize = numEntries * mRecordSize;

        mShaderTableData.resize(shaderTableSize);
        mShaderTable = nv_helpers_dx12::CreateBuffer(context->getDevice(), shaderTableSize, D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

        return true;
    }

    void RtBindings::applyRtProgramVars(uint8_t *record, RtShader::SharedPtr shader, ID3D12RaytracingFallbackStateObject *rtso, RtParams::SharedPtr params)
    {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        std::wstring entryPoint = converter.from_bytes(shader->getEntryPoint());
        void *id = rtso->GetShaderIdentifier(entryPoint.c_str());
        if (!id) {
            throw std::logic_error("Unknown shader identifier used in the SBT: " + shader->getEntryPoint());
        }
        memcpy(record, id, mProgramIdentifierSize);
        record += mProgramIdentifierSize;

        params->applyRootParams(shader, record);
    }
    
    void RtBindings::applyRtProgramVars(uint8_t *record, const RtProgram::HitGroup &hitGroup, ID3D12RaytracingFallbackStateObject *rtso, RtParams::SharedPtr params)
    {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        std::wstring entryPoint = converter.from_bytes(hitGroup.mExportName);
        void *id = rtso->GetShaderIdentifier(entryPoint.c_str());
        if (!id) {
            throw std::logic_error("Unknown shader identifier used in the SBT: " + hitGroup.mExportName);
        }
        memcpy(record, id, mProgramIdentifierSize);
        record += mProgramIdentifierSize;

        params->applyRootParams(hitGroup, record);
    }
    
    void RtBindings::apply(RtContext::SharedPtr context, RtState::SharedPtr state)
    {
        auto rtso = state->getFallbackRtso();

        uint8_t *rayGenRecord = getRayGenRecordPtr();
        applyRtProgramVars(rayGenRecord, mProgram->getRayGenProgram(), rtso, mRayGenParams);

        UINT hitCount = mProgram->getHitProgramCount();
        for (UINT h = 0; h < hitCount; h++) {
            UINT geometryCount = mScene->getNumInstances();
            for (UINT i = 0; i < geometryCount; i++) {
                uint8_t *pHitRecord = getHitRecordPtr(h, i);
                applyRtProgramVars(pHitRecord, mProgram->getHitProgram(h), rtso, mHitParams[h][i]);
            }
        }

        for (UINT m = 0; m < mProgram->getMissProgramCount(); m++) {
            uint8_t *pMissRecord = getMissRecordPtr(m);
            applyRtProgramVars(pMissRecord, mProgram->getMissProgram(m), rtso, mMissParams[m]);
        }

        // Update shader table
        uint8_t *mappedBuffer;
        HRESULT hr = mShaderTable->Map(0, nullptr, reinterpret_cast<void**>(&mappedBuffer));
        if (FAILED(hr)) {
            throw std::logic_error("Could not map the shader binding table");
        }
        memcpy(mappedBuffer, mShaderTableData.data(), mShaderTableData.size());
        mShaderTable->Unmap(0, nullptr);
    }

    // We are using the following layout for the shader-table:
    //
    // +------------+---------+---------+-----+--------+---------+--------+-----+--------+--------+-----+--------+-----+--------+--------+-----+--------+
    // |            |         |         | ... |        |         |        | ... |        |        | ... |        | ... |        |        | ... |        |
    // |   RayGen   |   Ray0  |   Ray1  | ... |  RayN  |   Ray0  |  Ray1  | ... |  RayN  |  Ray0  | ... |  RayN  | ... |  Ray0  |  Ray0  | ... |  RayN  |   
    // |   Entry    |   Miss  |   Miss  | ... |  Miss  |   Hit   |   Hit  | ... |  Hit   |  Hit   | ... |  Hit   | ... |  Hit   |  Hit   | ... |  Hit   |
    // |            |         |         | ... |        |  Mesh0  |  Mesh0 | ... |  Mesh0 |  Mesh1 | ... |  Mesh1 | ... | MeshN  |  MeshN | ... |  MeshN |
    // +------------+---------+---------+-----+--------+---------+--------+-----+--------+--------+-----+--------+-----+--------+--------+-----+--------+
    //
    // The first record is the ray gen, followed by the miss records, followed by the meshes records.
    // For each mesh we have N hit records, N == number of mesh instances in the model
    // The size of each record is mRecordSize
    // 
    // If this layout changes, we also need to change the constants kRayGenRecordIndex and kFirstMissRecordIndex

    uint8_t *RtBindings::getRayGenRecordPtr()
    {
        return mShaderTableData.data() + (kRayGenRecordIndex * mRecordSize);
    }

    uint8_t *RtBindings::getMissRecordPtr(uint32_t missId)
    {
        assert(missId < mMissProgCount);
        uint32_t offset = mRecordSize * (kFirstMissRecordIndex + missId);
        return mShaderTableData.data() + offset;
    }

    uint8_t *RtBindings::getHitRecordPtr(uint32_t hitId, uint32_t meshId)
    {   
        assert(hitId < mHitProgCount);
        uint32_t meshIndex = mFirstHitVarEntry + mHitProgCount * meshId;    // base record of the requested mesh
        uint32_t recordIndex = meshIndex + hitId;
        return mShaderTableData.data() + (recordIndex * mRecordSize);
    }
}
