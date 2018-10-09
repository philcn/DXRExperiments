#include "stdafx.h"
#include "RtProgram.h"
#include "nv_helpers_dx12/RootSignatureGenerator.h"

extern bool gVertexBufferUseRootTableInsteadOfRootView;

namespace DXRFramework
{
    RtProgram::ShaderLibrary::ShaderLibrary(IDxcBlob* dxil, const std::vector<std::wstring>& symbolExports)
        : mDXIL(dxil), mExportedSymbols(symbolExports)
    {
        // Create one export descriptor per symbol
        mExports.resize(mExportedSymbols.size());
        for (size_t i = 0; i < mExportedSymbols.size(); i++) {
            mExports[i] = {};
            mExports[i].Name = mExportedSymbols[i].c_str();
            mExports[i].ExportToRename = nullptr;
            mExports[i].Flags = D3D12_EXPORT_FLAG_NONE;
        }

        mLibDesc.DXILLibrary.BytecodeLength = dxil->GetBufferSize();
        mLibDesc.DXILLibrary.pShaderBytecode = dxil->GetBufferPointer();
        mLibDesc.NumExports = static_cast<UINT>(mExportedSymbols.size());
        mLibDesc.pExports = mExports.data();
    }

    RtProgram::ShaderLibrary::ShaderLibrary(const uint8_t *bytecode, UINT bytecodeSize, const std::vector<std::wstring>& symbolExports)
        : mDXIL(nullptr), mExportedSymbols(symbolExports)
    {
        // Create one export descriptor per symbol
        mExports.resize(mExportedSymbols.size());
        for (size_t i = 0; i < mExportedSymbols.size(); i++) {
            mExports[i] = {};
            mExports[i].Name = mExportedSymbols[i].c_str();
            mExports[i].ExportToRename = nullptr;
            mExports[i].Flags = D3D12_EXPORT_FLAG_NONE;
        }

        mLibDesc.DXILLibrary.BytecodeLength = bytecodeSize;
        mLibDesc.DXILLibrary.pShaderBytecode = bytecode;
        mLibDesc.NumExports = static_cast<UINT>(mExportedSymbols.size());
        mLibDesc.pExports = mExports.data();
    }

    RtProgram::Desc& RtProgram::Desc::addShaderLibrary(const std::shared_ptr<ShaderLibrary>& library)
    {
        ThrowIfFalse(library != nullptr, L"Can't add a null library to RtProgram::Desc");

        mActiveLibraryIndex = (uint32_t)mShaderLibraries.size();
        mShaderLibraries.emplace_back(library);
        return *this;
    }

    RtProgram::Desc& RtProgram::Desc::addShaderLibrary(IDxcBlob* dxilLibrary, const std::vector<std::wstring>& symbolExports)
    {
        return addShaderLibrary(std::shared_ptr<ShaderLibrary>(new ShaderLibrary(dxilLibrary, symbolExports)));
    }
    
    RtProgram::Desc& RtProgram::Desc::addShaderLibrary(const uint8_t *bytecode, UINT bytecodeSize, const std::vector<std::wstring>& symbolExports)
    {
        return addShaderLibrary(std::shared_ptr<ShaderLibrary>(new ShaderLibrary(bytecode, bytecodeSize, symbolExports)));
    }

    RtProgram::Desc& RtProgram::Desc::setRayGen(const std::string& raygen)
    {
        ThrowIfFalse(mActiveLibraryIndex != -1, L"Can't set raygen shader entry-point. Please add a shader-library first");
        ThrowIfFalse(mRayGen.libraryIndex == -1, L"RtProgram::Desc::setRayGen() - a ray-generation entry point is already set. Replacing the old entry-point");

        mRayGen.libraryIndex = mActiveLibraryIndex;
        mRayGen.entryPoint = raygen;
        return *this;
    }
    
    RtProgram::Desc& RtProgram::Desc::addMiss(uint32_t missIndex, const std::string& miss)
    {
        ThrowIfFalse(mActiveLibraryIndex != -1, L"Can't set miss shader entry-point. Please add a shader-library first");
        if (mMiss.size() <= missIndex) {
            mMiss.resize(missIndex + 1);
        } else {
            ThrowIfFalse(mMiss[missIndex].libraryIndex == -1, L"RtProgram::Desc::addMiss() - a miss entry point already exists. Replacing the old entry-point");
        }
        mMiss[missIndex].libraryIndex = mActiveLibraryIndex;
        mMiss[missIndex].entryPoint = miss;
        return *this;
    }
    
    RtProgram::Desc& RtProgram::Desc::addHitGroup(uint32_t hitIndex, const std::string& closestHit, const std::string& anyHit, const std::string& intersection)
    {
        ThrowIfFalse(mActiveLibraryIndex != -1, L"Can't set hit shader entry-point. Please add a shader-library first");
        if (mHit.size() <= hitIndex) {
            mHit.resize(hitIndex + 1);
        } else {
            ThrowIfFalse(mHit[hitIndex].libraryIndex == -1, L"RtProgram::Desc::addHitGroup() - a hit-group already exists. Replacing the old group");
        }
        mHit[hitIndex].anyHit = anyHit;
        mHit[hitIndex].closestHit = closestHit;
        mHit[hitIndex].intersection = intersection;
        mHit[hitIndex].libraryIndex = mActiveLibraryIndex;
        return *this;
    }

    RtProgram::SharedPtr RtProgram::create(RtContext::SharedPtr context, const Desc& desc, uint32_t maxPayloadSize, uint32_t maxAttributesSize)
    {
        ThrowIfFalse(desc.mRayGen.libraryIndex != -1, L"Can't create an RtProgram without a ray-generation shader");
        return SharedPtr(new RtProgram(context, desc, maxPayloadSize, maxAttributesSize));
    }

    RtProgram::RtProgram(RtContext::SharedPtr context, const RtProgram::Desc& desc, uint32_t maxPayloadSize, uint32_t maxAttributesSize)
        : mFallbackDevice(context->getFallbackDevice()), mDesc(desc)
    {
        // Shader reflection
        // TODO:

        // Create global root signature
        // TODO:
        mGlobalRootSignature = TempCreateGlobalRootSignature();

        // Create all programs and local root signature
        // Associate shader library with all programs
        // TODO:

        // const std::string raygenFile = desc.mShaderLibraries[desc.mRayGen.libraryIndex]->getFilename();
        mRayGenProgram = RtShader::create(context, RtShaderType::RayGeneration, desc.mRayGen.entryPoint, maxPayloadSize, maxAttributesSize);

        mMissPrograms.resize(desc.mMiss.size());
        for (size_t i = 0 ; i < desc.mMiss.size() ; i++) {
            const auto& m = desc.mMiss[i];
            if (m.libraryIndex != -1) {
                // const std::string missFile = desc.mShaderLibraries[m.libraryIndex]->getFilename();
                mMissPrograms[i] = RtShader::create(context, RtShaderType::Miss, m.entryPoint, maxPayloadSize, maxAttributesSize);
            }
        }

        mHitPrograms.resize(desc.mHit.size());
        for (size_t i = 0 ; i < desc.mHit.size() ; i++) {
            const auto& m = desc.mHit[i];
            if (m.libraryIndex != -1) {
                // const std::string hitFile = desc.mShaderLibraries[h.libraryIndex]->getFilename();
                HitGroup &hitGroup = mHitPrograms[i];
                hitGroup.mClosestHit = RtShader::create(context, RtShaderType::ClosestHit, m.closestHit, maxPayloadSize, maxAttributesSize);
                if (!m.anyHit.empty()) {
                    hitGroup.mAnyHit = RtShader::create(context, RtShaderType::AnyHit, m.anyHit, maxPayloadSize, maxAttributesSize);
                }
                if (!m.intersection.empty()) {
                    hitGroup.mIntersection = RtShader::create(context, RtShaderType::Intersection, m.intersection, maxPayloadSize, maxAttributesSize);
                }
                hitGroup.mExportName = "HitGroup" + std::to_string(i);
            }
        }
    }

    ID3D12RootSignature *RtProgram::TempCreateGlobalRootSignature()
    {
        #if 1
            nv_helpers_dx12::RootSignatureGenerator rootSigGenerator;
            // slot 0, GlobalRootSignatureParams::AccelerationStructureSlot
            rootSigGenerator.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0); // t0
            // slot 1, GlobalRootSignatureParams::OutputViewSlot
            rootSigGenerator.AddHeapRangesParameter({{0 /* u0 */, 1, 0 /* space0 */, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0}});
            // slot 2, GlobalRootSignatureParams::PerFrameConstantsSlot
            rootSigGenerator.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0); // b0

            D3D12_STATIC_SAMPLER_DESC cubeSampler = {};
            cubeSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            cubeSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            cubeSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            cubeSampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            cubeSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            cubeSampler.ShaderRegister = 0;

            rootSigGenerator.AddStaticSampler(cubeSampler);

            return rootSigGenerator.Generate(mFallbackDevice, false /* not local root signature */);
        #else
            CD3DX12_DESCRIPTOR_RANGE ranges[1]; // Perfomance TIP: Order from most frequent to least frequent.
            ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // 1 output texture

            CD3DX12_ROOT_PARAMETER rootParameters[GlobalRootSignatureParams::Count];
            rootParameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);
            rootParameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &ranges[0]);
            CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
            SerializeAndCreateRaytracingRootSignature(globalRootSignatureDesc, &mGlobalRootSignature);
        #endif
    }

    RtProgram::~RtProgram() = default;
}
