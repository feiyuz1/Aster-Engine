#include "MiniEngine/ProjectSDK/SDKInfo.hpp"
#include "MiniEngine/ProjectSDK/BuildInfo.h"

namespace MiniEngine::ProjectSDK
{
const char *GetSDKBuildId() noexcept
{
    return TINYENGINE_PROJECT_SDK_BUILD_ID;
}
}
