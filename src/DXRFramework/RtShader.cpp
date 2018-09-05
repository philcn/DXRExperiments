#include "stdafx.h"
#include "RtShader.h"
#include "nv_helpers_dx12/RootSignatureGenerator.h"

extern bool gVertexBufferInLocalRootSignature;
extern bool gVertexBufferUseRootTableInsteadOfRootView;

namespace DXRFramework
{
    RtShader::SharedPtr RtShader::create(RtContext::SharedPtr context, RtShaderType shaderType, const std::string &entryPoint, uint32_t maxPayloadSize, uint32_t maxAttributesSize)
    {
        return SharedPtr(new RtShader(context, shaderType, entryPoint, maxPayloadSize, maxAttributesSize));
    }

    RtShader::RtShader(RtContext::SharedPtr context, RtShaderType shaderType, const std::string &entryPoint, uint32_t maxPayloadSize, uint32_t maxAttributesSize)
        : mFallbackDevice(context->getFallbackDevice()), mShaderType(shaderType), mEntryPoint(entryPoint), mMaxPayloadSize(maxPayloadSize), mMaxAttributesSize(maxAttributesSize)
    {
        // Reflection
        // TODO:

        mLocalRootSignature = TempCreateLocalRootSignature();
    }

    RtShader::~RtShader() = default;

    ID3D12RootSignature *RtShader::TempCreateLocalRootSignature()
    {
        nv_helpers_dx12::RootSignatureGenerator rootSigGenerator;

        if (mShaderType == RtShaderType::Miss || mShaderType == RtShaderType::ClosestHit) {
            if (gVertexBufferInLocalRootSignature) {
                if (gVertexBufferUseRootTableInsteadOfRootView) {
                    rootSigGenerator.AddHeapRangesParameter({{0 /* t0 */, 1, 1 /* space1 */, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0}});
                } else {
                    rootSigGenerator.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0, 1); // space1 t0
                }
            }

            // 32bit constants after the srv, to prove the alignment requirement for GPU heap handle
            rootSigGenerator.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, 0, 1, 1); // space1 b0
        }

        return rootSigGenerator.Generate(mFallbackDevice, true /* local root signature */);
    }
}
