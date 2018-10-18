#pragma once

#include "DXRFramework/RtContext.h"

class DenoiseCompositor
{
public:
    using SharedPtr = std::shared_ptr<DenoiseCompositor>;
    static SharedPtr create(DXRFramework::RtContext::SharedPtr context) { return SharedPtr(new DenoiseCompositor(context)); }

    ~DenoiseCompositor() = default;

    void dispatch(ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle, UINT width, UINT height);
private:
    DenoiseCompositor(DXRFramework::RtContext::SharedPtr context);

    ComPtr<ID3D12RootSignature> mComputeRootSignature;
    ComPtr<ID3D12PipelineState> mComputeState;

    DXRFramework::RtContext::SharedPtr mRtContext;
};
