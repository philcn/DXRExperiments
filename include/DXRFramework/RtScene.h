#pragma once
#include "RtContext.h"
#include "RtModel.h"
#include "D3D12RaytracingFallback.h"

namespace DXRFramework
{
    class RtScene
    {
    public:
        using SharedPtr = std::shared_ptr<RtScene>;
        
        static SharedPtr create();
        ~RtScene();

        class Node
        {
        public:
            using SharedPtr = std::shared_ptr<Node>;
            static SharedPtr create(RtModel::SharedPtr model, DirectX::XMMATRIX transform) { return SharedPtr(new Node(model, transform)); }
        private:
            friend class RtScene;
            Node(RtModel::SharedPtr model, DirectX::XMMATRIX transform) : mModel(model), mTransform(transform) {}

            RtModel::SharedPtr mModel;
            DirectX::XMMATRIX mTransform;
        };

        void addModel(RtModel::SharedPtr model, DirectX::XMMATRIX transform) { mInstances.emplace_back(Node::create(model, transform)); }
        RtModel::SharedPtr getModel(UINT index) const { return mInstances[index]->mModel; }
        UINT getNumInstances() const { return mInstances.size(); }

        ID3D12Resource *getTlasResource() const { return mTlasBuffer.Get(); }
        WRAPPED_GPU_POINTER getTlasWrappedPtr() const { return mTlasWrappedPointer; }

        void build(RtContext::SharedPtr context);
    private:
        RtScene();

        std::vector<Node::SharedPtr> mInstances;
        
        ComPtr<ID3D12Resource> mTlasBuffer;
        WRAPPED_GPU_POINTER mTlasWrappedPointer;
    };
}
