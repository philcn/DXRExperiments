#pragma once
#include "RtContext.h"

namespace DXRFramework
{
    class RtModel
    {
    public:
        using SharedPtr = std::shared_ptr<RtModel>;
        
        static SharedPtr create(RtContext::SharedPtr context);
        ~RtModel();
        
    private:
        friend class RtScene;
        RtModel(RtContext::SharedPtr context);

        void build(RtContext::SharedPtr context);

        ComPtr<ID3D12Resource> mVertexBuffer;
        ComPtr<ID3D12Resource> mBlasBuffer;
    };
}
