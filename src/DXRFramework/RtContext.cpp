#include "RtContext.h"

namespace DXRFramework
{
    RtContext::SharedPtr RtContext::create()
    {
        return SharedPtr(new RtContext());
    }

    RtContext::RtContext()
    {
    }

    RtContext::~RtContext() = default;

    void RtContext::DispatchRays()
    {
    }
}
