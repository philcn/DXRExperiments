#include "stdafx.h"
#include "RtModel.h"

namespace DXRFramework
{
    RtModel::SharedPtr RtModel::create()
    {
        return SharedPtr(new RtModel());
    }

    RtModel::RtModel()
    {
    }

    RtModel::~RtModel() = default;
}
