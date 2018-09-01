#include "RtProgram.h"

namespace DXRFramework
{
    RtProgram::SharedPtr RtProgram::create()
    {
        return SharedPtr(new RtProgram());
    }

    RtProgram::RtProgram()
    {
    }

    RtProgram::~RtProgram() = default;
}
