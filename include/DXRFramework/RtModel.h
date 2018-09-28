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

        WRAPPED_GPU_POINTER getVertexBufferWrappedPtr() const { return mVertexBufferWrappedPtr; }
        WRAPPED_GPU_POINTER getIndexBufferWrappedPtr() const { return mIndexBufferWrappedPtr; }

    private:
        friend class RtScene;
        RtModel(RtContext::SharedPtr context, const std::string &filePath);

        void build(RtContext::SharedPtr context);

        UINT mNumVertices;
        UINT mNumTriangles;

        ComPtr<ID3D12Resource> mVertexBuffer;
        ComPtr<ID3D12Resource> mIndexBuffer;
        ComPtr<ID3D12Resource> mBlasBuffer;

        WRAPPED_GPU_POINTER mVertexBufferWrappedPtr;
        WRAPPED_GPU_POINTER mIndexBufferWrappedPtr;
    };
}
