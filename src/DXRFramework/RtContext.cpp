#include "stdafx.h"
#include "RtContext.h"
#include "RtBindings.h"
#include "RtState.h"

namespace DXRFramework
{
    RtContext::SharedPtr RtContext::create(ID3D12Device *device, ID3D12GraphicsCommandList *commandList)
    {
        return SharedPtr(new RtContext(device, commandList));
    }

    RtContext::RtContext(ID3D12Device *device, ID3D12GraphicsCommandList *commandList)
        : mDevice(device), mCommandList(commandList)
    {
        // TODO: try enable experimental feature and query DXR interface
        bool isDXRDriverSupported = false;

        if (!isDXRDriverSupported) {
            CreateRaytracingFallbackDeviceFlags createDeviceFlags = CreateRaytracingFallbackDeviceFlags::EnableRootDescriptorsInShaderRecords;
            ThrowIfFailed(D3D12CreateRaytracingFallbackDevice(device, createDeviceFlags, 0, IID_PPV_ARGS(&mFallbackDevice)));
            mFallbackDevice->QueryRaytracingCommandList(commandList, IID_PPV_ARGS(&mFallbackCommandList));
        }

        // TODO: decide descriptor heap size
        createDescriptorHeap();
    }

    void RtContext::createDescriptorHeap()
    {
        D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
        // Allocate a heap for 3 descriptors:
        // 2x bottom and top level acceleration structure fallback wrapped pointer UAVs
        // 1x raytracing output texture SRV
        // 1x global camera SBV
        // 2x model_0 vertex buffer SRV & wrapped pointer for testing
        descriptorHeapDesc.NumDescriptors = 6;
        descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        descriptorHeapDesc.NodeMask = 0;
        mDevice->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&mDescriptorHeap));
        NAME_D3D12_OBJECT(mDescriptorHeap);

        mDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void RtContext::bindDescriptorHeap()
    {
        ID3D12DescriptorHeap *pDescriptorHeaps[] = { mDescriptorHeap.Get() };
        mFallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
    }
    
    D3D12_GPU_DESCRIPTOR_HANDLE RtContext::getDescriptorGPUHandle(UINT heapIndex)
    {
        return CD3DX12_GPU_DESCRIPTOR_HANDLE(
            mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), heapIndex, mDescriptorSize);
    }
    
    RtContext::~RtContext() = default;

    WRAPPED_GPU_POINTER RtContext::createUAVFallbackWrappedPointer(ID3D12Resource* resource)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC rawBufferUavDesc = {};
        rawBufferUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        rawBufferUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        rawBufferUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        rawBufferUavDesc.Buffer.NumElements = (UINT)(resource->GetDesc().Width / sizeof(UINT32));

        UINT descriptorHeapIndex = 0;
        if (!mFallbackDevice->UsingRaytracingDriver()) {
            D3D12_CPU_DESCRIPTOR_HANDLE bottomLevelDescriptor;
            descriptorHeapIndex = allocateDescriptor(&bottomLevelDescriptor);
            mDevice->CreateUnorderedAccessView(resource, nullptr, &rawBufferUavDesc, bottomLevelDescriptor);
        }
        return mFallbackDevice->GetWrappedPointerSimple(descriptorHeapIndex, resource->GetGPUVirtualAddress());
    }

    WRAPPED_GPU_POINTER RtContext::createSRVFallbackWrappedPointer(ID3D12Resource* resource, bool rawBuffer, UINT structureStride)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (rawBuffer) {
            srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            srvDesc.Buffer.NumElements = (UINT)(resource->GetDesc().Width / sizeof(UINT32)); 
        } else {
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            srvDesc.Buffer.StructureByteStride = structureStride;
            srvDesc.Buffer.NumElements = (UINT)(resource->GetDesc().Width / structureStride);
        }

        UINT descriptorHeapIndex = 0;
        if (!mFallbackDevice->UsingRaytracingDriver()) {
            D3D12_CPU_DESCRIPTOR_HANDLE bottomLevelDescriptor;
            descriptorHeapIndex = allocateDescriptor(&bottomLevelDescriptor);
            mDevice->CreateShaderResourceView(resource, &srvDesc, bottomLevelDescriptor);
        }
        return mFallbackDevice->GetWrappedPointerSimple(descriptorHeapIndex, resource->GetGPUVirtualAddress());
    }

    UINT RtContext::allocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse)
    {
        auto descriptorHeapCpuBase = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        if (descriptorIndexToUse >= mDescriptorHeap->GetDesc().NumDescriptors) {
            descriptorIndexToUse = mDescriptorsAllocated++;
        }
        *cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase, descriptorIndexToUse, mDescriptorSize);
        return descriptorIndexToUse;
    }

    void RtContext::raytrace(RtBindings::SharedPtr bindings, RtState::SharedPtr state, uint32_t width, uint32_t height)
    {
        // resourceBarrier(bindings->getShaderTable(), Resource::State::NonPixelShader);

        auto pShaderTable = bindings->getShaderTable();
        uint32_t recordSize = bindings->getRecordSize();
        D3D12_GPU_VIRTUAL_ADDRESS startAddress = pShaderTable->GetGPUVirtualAddress();

        D3D12_FALLBACK_DISPATCH_RAYS_DESC raytraceDesc = {};
        raytraceDesc.Width = width;
        raytraceDesc.Height = height;

        // RayGen is the first entry in the shader-table
        raytraceDesc.RayGenerationShaderRecord.StartAddress = startAddress + bindings->getRayGenRecordIndex() * recordSize;
        raytraceDesc.RayGenerationShaderRecord.SizeInBytes = recordSize;

        // Miss is the second entry in the shader-table
        raytraceDesc.MissShaderTable.StartAddress = startAddress + bindings->getFirstMissRecordIndex() * recordSize;
        raytraceDesc.MissShaderTable.StrideInBytes = recordSize;
        raytraceDesc.MissShaderTable.SizeInBytes = recordSize * bindings->getMissProgramsCount();

        raytraceDesc.HitGroupTable.StartAddress = startAddress + bindings->getFirstHitRecordIndex() * recordSize;
        raytraceDesc.HitGroupTable.StrideInBytes = recordSize;
        raytraceDesc.HitGroupTable.SizeInBytes = recordSize * bindings->getHitProgramsCount();

        // Dispatch
        mFallbackCommandList->DispatchRays(state->getFallbackRtso(), &raytraceDesc);
    }
}
