#pragma once
#include "RtContext.h"
#include "Helpers/RootSignatureGenerator.h"

namespace DXRFramework
{
    using nv_helpers_dx12::RootSignatureGenerator;

    enum class RtShaderType
    {
        RayGeneration,  ///< Ray generation shader
        Intersection,   ///< Intersection shader
        AnyHit,         ///< Any hit shader
        ClosestHit,     ///< Closest hit shader
        Miss,           ///< Miss shader
        Callable,       ///< Callable shader
        Count           ///< Shader Type count
    };

    class RtShader
    {
    public:
        using SharedPtr = std::shared_ptr<RtShader>;

        static SharedPtr create(
            RtContext::SharedPtr context, /* library ,*/ RtShaderType shaderType, const std::string &entryPoint, 
            uint32_t maxPayloadSize, uint32_t maxAttributesSize, RootSignatureGenerator rootSignatureConfig);

        std::string getEntryPoint() const { return mEntryPoint; }

        ~RtShader();
    private:
        friend class RtState;

        RtShader(
            RtContext::SharedPtr context, /* library ,*/ RtShaderType shaderType, const std::string &entryPoint, 
            uint32_t maxPayloadSize, uint32_t maxAttributesSize, RootSignatureGenerator rootSignatureConfig);

        RtShaderType mShaderType;
        std::string mEntryPoint;

        uint32_t mMaxPayloadSize;
        uint32_t mMaxAttributesSize;

        // TODO: Library reference

        ID3D12RaytracingFallbackDevice *mFallbackDevice;
        ComPtr<ID3D12RootSignature> mLocalRootSignature;
    };
}
