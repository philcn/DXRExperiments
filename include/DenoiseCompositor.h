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

    ID3D12Resource *getOutputResource() { return mOutputResource[1].Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE getOutputUavHandle() { return mOutputUavGpuHandle[1]; }

    bool mActive;
private:
    DenoiseCompositor(DXRFramework::RtContext::SharedPtr context);

    ComPtr<ID3D12RootSignature> mComputeRootSignature;
    ComPtr<ID3D12PipelineState> mComputeState;

    DXRFramework::RtContext::SharedPtr mRtContext;

    // Output resource
    ComPtr<ID3D12Resource> mOutputResource[2];
    UINT mOutputUavHeapIndex[2] = { UINT_MAX, UINT_MAX };
    UINT mOutputSrvHeapIndex[2] = { UINT_MAX, UINT_MAX };
    D3D12_GPU_DESCRIPTOR_HANDLE mOutputUavGpuHandle[2];
    D3D12_GPU_DESCRIPTOR_HANDLE mOutputSrvGpuHandle[2];

    struct DenoiserParams
    {
        float exposure;
        float gamma;
        UINT tonemap;
        UINT gammaCorrect;
        int maxKernelSize;
    };

    ConstantBuffer<DenoiserParams> mConstantBuffer;

    // Mock resources
    std::vector<ComPtr<ID3D12Resource>> mTextureResources;
    std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> mTextureSrvGpuHandles;
};
