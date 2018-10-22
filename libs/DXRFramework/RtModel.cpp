#include "RtModel.h"
#include "Helpers/BottomLevelASGenerator.h"
#include "Helpers/DirectXRaytracingHelper.h"
#include "assimp/cimport.h"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include <DirectXMath.h>

using namespace DirectX;

namespace DXRFramework
{
    struct Vertex
    {
        XMFLOAT3 position;
        XMFLOAT3 normal;
    };

    RtModel::SharedPtr RtModel::create(RtContext::SharedPtr context, const std::string &filePath)
    {
        return SharedPtr(new RtModel(context, filePath));
    }

    RtModel::RtModel(RtContext::SharedPtr context, const std::string &filePath)
    {
        auto flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices;
        const aiScene *scene = aiImportFile(filePath.c_str(), flags);

        std::vector<Vertex> interleavedVertexData;
        std::vector<uint32_t> indices;
        mNumVertices = 0;
        mNumTriangles = 0;

        if (scene) {
            for (int meshId = 0; meshId < scene->mNumMeshes; ++meshId) {
                const auto &mesh = scene->mMeshes[meshId];

                for (int i = 0; i < mesh->mNumVertices; ++i) {
                    aiVector3D &position = mesh->mVertices[i];
                    aiVector3D &normal = mesh->mNormals[i];
                    Vertex vertex;
                    vertex.position = XMFLOAT3(position.x, position.y, position.z);
                    vertex.normal = mesh->HasNormals() ? XMFLOAT3(normal.x, normal.y, normal.z) : XMFLOAT3(0.0f, 0.0f, 0.0f);
                    interleavedVertexData.emplace_back(vertex);
                }

                for (int i = 0; i < mesh->mNumFaces; ++i) {
                    const aiFace &face = mesh->mFaces[i];
                    assert(face.mNumIndices == 3);
                    indices.push_back(mNumVertices + face.mIndices[0]);
                    indices.push_back(mNumVertices + face.mIndices[1]);
                    indices.push_back(mNumVertices + face.mIndices[2]);
                }

                mNumTriangles += mesh->mNumFaces;
                mNumVertices += mesh->mNumVertices;
            }
        } else {
            interleavedVertexData =
            {
                { { 0.0f, 0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
                { { 0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
                { { -0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
            };
            indices = { 0, 2, 1 };
            mNumTriangles = 1;
            mNumVertices = interleavedVertexData.size();
        }

        mHasIndexBuffer = indices.size() > 0;

        auto device = context->getDevice();
        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        AllocateUploadBuffer(device, interleavedVertexData.data(), mNumVertices * sizeof(Vertex), &mVertexBuffer);

        if (mHasIndexBuffer) {
            AllocateUploadBuffer(device, indices.data(), indices.size() * sizeof(uint32_t), &mIndexBuffer);
        }
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
        if (mHasIndexBuffer) {
            blasGenerator.AddVertexBuffer(mVertexBuffer.Get(), 0, mNumVertices, sizeof(Vertex), 
                mIndexBuffer.Get(), 0, mNumTriangles * 3, DXGI_FORMAT_R32_UINT, nullptr, 0);
        } else {
            blasGenerator.AddVertexBuffer(mVertexBuffer.Get(), 0, mNumVertices, sizeof(Vertex), nullptr, 0);
        }

        UINT64 scratchSizeInBytes = 0;
        UINT64 resultSizeInBytes = 0;
        blasGenerator.ComputeASBufferSizes(fallbackDevice, false, &scratchSizeInBytes, &resultSizeInBytes);

        ComPtr<ID3D12Resource> scratch = CreateBuffer(device, scratchSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kDefaultHeapProps);

        D3D12_RESOURCE_STATES initialResourceState = fallbackDevice->GetAccelerationStructureResourceState();
        mBlasBuffer = CreateBuffer(device, resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, initialResourceState, kDefaultHeapProps);

        blasGenerator.Generate(commandList, fallbackCommandList, scratch.Get(), mBlasBuffer.Get());

        mVertexBufferSrvHandle = context->createBufferSRVHandle(mVertexBuffer.Get(), false, sizeof(Vertex));
        if (mIndexBuffer) {
            mIndexBufferSrvHandle = context->createBufferSRVHandle(mIndexBuffer.Get(), false, sizeof(uint32_t));
        }
    }
}
