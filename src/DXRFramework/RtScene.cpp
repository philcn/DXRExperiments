#include "RtScene.h"

namespace DXRFramework
{
    RtScene::SharedPtr RtScene::create()
    {
        return SharedPtr(new RtScene());
    }

    RtScene::RtScene()
    {
    }

    RtScene::~RtScene() = default;
}
