#include "RtState.h"

namespace DXRFramework
{
    RtProgram::SharedPtr RtState::create()
    {
        return SharedPtr(new RtState());
    }

    RtState::RtState()
    {
    }

    RtState::~RtState() = default;
}
