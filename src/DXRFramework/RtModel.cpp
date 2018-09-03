#include "stdafx.h"
#include "RtModel.h"
#include "RaytracingHlslCompat.h" // for Vertex
#include "DirectXRaytracingHelper.h" // for AllocateUploadBuffer()
#include "nv_helpers_dx12/DXRHelper.h" // for CreateBuffer()
#include "nv_helpers_dx12/BottomLevelASGenerator.h"

namespace DXRFramework
{
    RtModel::SharedPtr RtModel::create(RtContext::SharedPtr context)
    {
        return SharedPtr(new RtModel(context));
    }

    RtModel::RtModel(RtContext::SharedPtr context)
    {
        Vertex triangleVertices[] =
        {
            { { 0.0f, 0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
            { { 0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
            { { -0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
        };

        const UINT vertexBufferSize = sizeof(triangleVertices);

        auto device = context->getDevice();
        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        AllocateUploadBuffer(device, triangleVertices, sizeof(triangleVertices), &mVertexBuffer);
    }

    RtModel::~RtModel() = default;

    void RtModel::build(RtContext::SharedPtr context)
    {
        auto device = context->getDevice();
        auto commandList = context->getCommandList();
        auto fallbackDevice = context->getFallbackDevice();
        auto fallbackCommandList = context->getFallbackCommandList();

        nv_helpers_dx12::BottomLevelASGenerator blasGenerator;

        // Just one vertex buffer per blas for now
        blasGenerator.AddVertexBuffer(mVertexBuffer.Get(), 0, 3, sizeof(Vertex), nullptr, 0);

        UINT64 scratchSizeInBytes = 0;
        UINT64 resultSizeInBytes = 0;
        blasGenerator.ComputeASBufferSizes(fallbackDevice, false, &scratchSizeInBytes, &resultSizeInBytes);

        ComPtr<ID3D12Resource> scratch = nv_helpers_dx12::CreateBuffer(
            device, scratchSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nv_helpers_dx12::kDefaultHeapProps);

        D3D12_RESOURCE_STATES initialResourceState = fallbackDevice->GetAccelerationStructureResourceState();
        mBlasBuffer = nv_helpers_dx12::CreateBuffer(
            device, resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
            initialResourceState, nv_helpers_dx12::kDefaultHeapProps);

        blasGenerator.Generate(commandList, fallbackCommandList, scratch.Get(), mBlasBuffer.Get());
    }
}
