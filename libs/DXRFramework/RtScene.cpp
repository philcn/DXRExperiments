#include "RtScene.h"
#include "Helpers/TopLevelASGenerator.h"
#include "Helpers/DirectXRaytracingHelper.h"

namespace DXRFramework
{
    RtScene::SharedPtr RtScene::create()
    {
        return SharedPtr(new RtScene());
    }

    RtScene::RtScene()
    {
    }

    RtScene::~RtScene() = default;

    void RtScene::build(RtContext::SharedPtr context, UINT hitGroupCount)
    {
        auto device = context->getDevice();
        auto commandList = context->getCommandList();
        auto fallbackDevice = context->getFallbackDevice();
        auto fallbackCommandList = context->getFallbackCommandList();

        nv_helpers_dx12::TopLevelASGenerator tlasGenerator;

        for (int i = 0; i < mInstances.size(); ++i) {
            mInstances[i]->mModel->build(context);
            tlasGenerator.AddInstance(mInstances[i]->mModel->mBlasBuffer.Get(), mInstances[i]->mTransform, i, i * hitGroupCount);
        }

        UINT64 scratchSizeInBytes = 0;
        UINT64 resultSizeInBytes = 0;
        UINT64 instanceDescsSize = 0;
        tlasGenerator.ComputeASBufferSizes(fallbackDevice, true, &scratchSizeInBytes, &resultSizeInBytes, &instanceDescsSize);

        // Allocate on default heap since the build is done on GPU
        ComPtr<ID3D12Resource> scratch = CreateBuffer(device, scratchSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kDefaultHeapProps);

        D3D12_RESOURCE_STATES initialResourceState = fallbackDevice->GetAccelerationStructureResourceState();
        mTlasBuffer = CreateBuffer(device, resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, initialResourceState, kDefaultHeapProps);

        ComPtr<ID3D12Resource> instanceDesc = CreateBuffer(device, instanceDescsSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps); 

        // Set the descriptor heaps to be used during acceleration structure build for the Fallback Layer.
        context->bindDescriptorHeap();

        tlasGenerator.Generate(commandList, fallbackCommandList, scratch.Get(), mTlasBuffer.Get(), instanceDesc.Get(), 
            [&](ID3D12Resource *resource) -> WRAPPED_GPU_POINTER { return context->createBufferUAVWrappedPointer(resource); });

        mTlasWrappedPointer = context->createBufferUAVWrappedPointer(mTlasBuffer.Get());
    }
}
