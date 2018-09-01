#pragma once

namespace DXRFramework
{
    class RtContext
    {
    public:
        using SharedPtr = std::shared_ptr<RtContext>;

        static SharedPtr create();
        ~RtContext();

        void DispatchRays();

    private:
        RtContext();

        ID3D12Device* mDevice;
        ComPtr<ID3D12RaytracingFallbackDevice> mFallbackDevice;
        ComPtr<ID3D12RaytracingFallbackCommandList> mFallbackCommandList;
        
        // RT global descriptor heap
        ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
        UINT mDescriptorsAllocated;
        UINT mDescriptorSize;
    };
}
