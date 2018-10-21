#pragma once

namespace DXRFramework
{
    class RtBindings;
    class RtState;

    class RtContext
    {
    public:
        using SharedPtr = std::shared_ptr<RtContext>;

        static SharedPtr create(ID3D12Device *device, ID3D12GraphicsCommandList *commandList, bool forceComputeFallback);
        ~RtContext();

        ID3D12Device *getDevice() const { return mDevice; }
        ID3D12GraphicsCommandList *getCommandList() const { return mCommandList; }
        ID3D12RaytracingFallbackDevice *getFallbackDevice() const { return mFallbackDevice.Get(); }
        ID3D12RaytracingFallbackCommandList *getFallbackCommandList() const { return mFallbackCommandList.Get(); }

        bool isUsingNativeDxr() const { return mFallbackDevice->UsingRaytracingDriver(); }

        void raytrace(std::shared_ptr<RtBindings> bindings, std::shared_ptr<RtState> state, uint32_t width, uint32_t height, uint32_t depth);

        void bindDescriptorHeap();
        D3D12_GPU_DESCRIPTOR_HANDLE getDescriptorGPUHandle(UINT heapIndex);

        // Allocate a descriptor and return its index. 
        // If the passed descriptorIndexToUse is valid, it will be used instead of allocating a new one.
        UINT allocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse = UINT_MAX);

        // Create a wrapped pointer for the Fallback Layer path.
        WRAPPED_GPU_POINTER createBufferUAVWrappedPointer(ID3D12Resource* resource);
        WRAPPED_GPU_POINTER createBufferSRVWrappedPointer(ID3D12Resource* resource, bool rawBuffer = true, UINT structureStride = 4);
        WRAPPED_GPU_POINTER createTextureSRVWrappedPointer(ID3D12Resource* resource, bool cubemap = false);

        D3D12_GPU_DESCRIPTOR_HANDLE createBufferUAVHandle(ID3D12Resource* resource);
        D3D12_GPU_DESCRIPTOR_HANDLE createBufferSRVHandle(ID3D12Resource* resource, bool rawBuffer = true, UINT structureStride = 4);
        D3D12_GPU_DESCRIPTOR_HANDLE createTextureSRVHandle(ID3D12Resource* resource, bool cubemap = false, UINT descriptorHeapIndex = UINT_MAX);

        void transitionResource(ID3D12Resource *resource, D3D12_RESOURCE_STATES fromState, D3D12_RESOURCE_STATES toState);
    private:
        RtContext(ID3D12Device *device, ID3D12GraphicsCommandList *commandList, bool forceComputeFallback);

        ID3D12Device *mDevice;
        ID3D12GraphicsCommandList *mCommandList;

        ComPtr<ID3D12RaytracingFallbackDevice> mFallbackDevice;
        ComPtr<ID3D12RaytracingFallbackCommandList> mFallbackCommandList;
        
        // RT global descriptor heap
        ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
        UINT mDescriptorsAllocated;
        UINT mDescriptorSize;

        void createDescriptorHeap();
    };
}
