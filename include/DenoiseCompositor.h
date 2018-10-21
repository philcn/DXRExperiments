#pragma once

#include "DXRFramework/RtContext.h"

class DenoiseCompositor
{
public:
    using SharedPtr = std::shared_ptr<DenoiseCompositor>;
    static SharedPtr create(DXRFramework::RtContext::SharedPtr context) { return SharedPtr(new DenoiseCompositor(context)); }

    ~DenoiseCompositor() = default;

    void userInterface();
    void dispatch(ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle, UINT frameIndex, UINT width, UINT height);

    void loadResources(ID3D12CommandQueue *uploadCommandQueue, UINT frameCount, bool loadMockResources);
    void createOutputResource(DXGI_FORMAT format, UINT width, UINT height);

    ID3D12Resource *getOutputResource() { return mOutputResource.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE getOutputUavHandle() { return mOutputUavGpuHandle; }

    bool mActive;
private:
    DenoiseCompositor(DXRFramework::RtContext::SharedPtr context);

    ComPtr<ID3D12RootSignature> mComputeRootSignature;
    ComPtr<ID3D12PipelineState> mComputeState;

    DXRFramework::RtContext::SharedPtr mRtContext;

    // Output resource
    ComPtr<ID3D12Resource> mOutputResource;
    UINT mOutputUavHeapIndex;
    D3D12_GPU_DESCRIPTOR_HANDLE mOutputUavGpuHandle;

    struct DenoiserParams
    {
        float exposure;
        float gamma;
        UINT tonemap;
        UINT gammaCorrect;
    };

    ConstantBuffer<DenoiserParams> mConstantBuffer;

    // Mock resources
    std::vector<ComPtr<ID3D12Resource>> mTextureResources;
    std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> mTextureSrvGpuHandles;
};
