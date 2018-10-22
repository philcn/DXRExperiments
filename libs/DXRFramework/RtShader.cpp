#include "RtShader.h"

namespace DXRFramework
{
    RtShader::SharedPtr RtShader::create(
        RtContext::SharedPtr context, RtShaderType shaderType, const std::string &entryPoint, 
        uint32_t maxPayloadSize, uint32_t maxAttributesSize, RootSignatureGenerator rootSignatureConfig)
    {
        return SharedPtr(new RtShader(context, shaderType, entryPoint, maxPayloadSize, maxAttributesSize, rootSignatureConfig));
    }

    RtShader::RtShader(
        RtContext::SharedPtr context, RtShaderType shaderType, const std::string &entryPoint, 
        uint32_t maxPayloadSize, uint32_t maxAttributesSize, RootSignatureGenerator rootSignatureConfig) :
        mFallbackDevice(context->getFallbackDevice()), 
        mShaderType(shaderType), 
        mEntryPoint(entryPoint), 
        mMaxPayloadSize(maxPayloadSize), 
        mMaxAttributesSize(maxAttributesSize)
    {
        mLocalRootSignature = rootSignatureConfig.Generate(mFallbackDevice, true);
    }

    RtShader::~RtShader() = default;
}
