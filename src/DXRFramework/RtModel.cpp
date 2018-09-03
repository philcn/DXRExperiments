#include "stdafx.h"
#include "RtModel.h"
#include "RaytracingHlslCompat.h" // for Vertex
#include "DirectXRaytracingHelper.h" // for AllocateUploadBuffer()
#include "nv_helpers_dx12/DXRHelper.h" // for CreateBuffer()
#include "nv_helpers_dx12/BottomLevelASGenerator.h"
#include "assimp/cimport.h"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

namespace DXRFramework
{
    RtModel::SharedPtr RtModel::create(RtContext::SharedPtr context, const std::string &filePath)
    {
        return SharedPtr(new RtModel(context, filePath));
    }

    RtModel::RtModel(RtContext::SharedPtr context, const std::string &filePath)
    {
        const aiScene *scene = aiImportFile(filePath.c_str(), 0);

        std::vector<Vertex> interleavedVertexData;
        mNumVertices = 0;

        if (scene) {
            for (int meshId = 0; meshId < scene->mNumMeshes; ++meshId) {
                const auto &mesh = scene->mMeshes[meshId];
                mNumVertices += mesh->mNumVertices;
                for (int i = 0; i < mesh->mNumVertices; ++i) {
                    aiVector3D &position = mesh->mVertices[i];
                    aiVector3D &normal = mesh->mNormals[i];
                    Vertex vertex;
                    vertex.position = XMFLOAT3(position.x, position.y, position.z);
                    vertex.normal = mesh->HasNormals() ? XMFLOAT3(normal.x, normal.y, normal.z) : XMFLOAT3(0.0f, 0.0f, 1.0f);
                    interleavedVertexData.emplace_back(vertex);
                }
            }
        } else {
            interleavedVertexData =
            {
                { { 0.0f, 0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
                { { 0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
                { { -0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
            };
            mNumVertices = interleavedVertexData.size();
        }

        auto device = context->getDevice();
        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        AllocateUploadBuffer(device, interleavedVertexData.data(), mNumVertices * sizeof(Vertex), &mVertexBuffer);
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
        blasGenerator.AddVertexBuffer(mVertexBuffer.Get(), 0, mNumVertices, sizeof(Vertex), nullptr, 0);

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
