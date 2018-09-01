#pragma once

namespace DXRFramework
{
    class RtScene
    {
    public:
        using SharedPtr = std::shared_ptr<RtScene>;
        
        static SharedPtr create();
        ~RtScene();
        
    private:
        RtScene();

        // Instance data
        
        ComPtr<ID3D12Resource> mTlasBuffer;
        WRAPPED_GPU_POINTER mTlasWrappedPointer;
    };
}
