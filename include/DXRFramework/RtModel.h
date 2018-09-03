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
        
    private:
        friend class RtScene;
        RtModel(RtContext::SharedPtr context, const std::string &filePath);

        void build(RtContext::SharedPtr context);

        UINT mNumVertices;
        ComPtr<ID3D12Resource> mVertexBuffer;
        ComPtr<ID3D12Resource> mBlasBuffer;
    };
}
