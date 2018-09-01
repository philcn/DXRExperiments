#pragma once

namespace DXRFramework
{
    class RtModel
    {
    public:
        using SharedPtr = std::shared_ptr<RtMotel>;
        
        static SharedPtr create();
        ~RtModel();
        
    private:
        RtModel();

        ComPtr<ID3D12Resource> mBlasBuffer;
    };
}
