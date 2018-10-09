#pragma once
#include "RtContext.h"

namespace DXRFramework
{
    class RtModel
    {
    public:
        using SharedPtr = std::shared_ptr<RtModel>;
        
        static SharedPtr create(RtContext::SharedPtr context, const std::string &filePath);
        ~RtModel();
        
        ID3D12Resource *getVertexBuffer() const { return mVertexBuffer.Get(); }
        ID3D12Resource *getIndexBuffer() const { return mIndexBuffer.Get(); }

        D3D12_GPU_DESCRIPTOR_HANDLE getVertexBufferSrvHandle() const { return mVertexBufferSrvHandle; }
        D3D12_GPU_DESCRIPTOR_HANDLE getIndexBufferSrvHandle() const { return mIndexBufferSrvHandle; }

    private:
        friend class RtScene;
        RtModel(RtContext::SharedPtr context, const std::string &filePath);

        void build(RtContext::SharedPtr context);

        bool mHasIndexBuffer;
        UINT mNumVertices;
        UINT mNumTriangles;

        ComPtr<ID3D12Resource> mVertexBuffer;
        ComPtr<ID3D12Resource> mIndexBuffer;
        ComPtr<ID3D12Resource> mBlasBuffer;

        D3D12_GPU_DESCRIPTOR_HANDLE mVertexBufferSrvHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE mIndexBufferSrvHandle;
    };
}
