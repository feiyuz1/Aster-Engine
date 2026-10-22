#include "tinyengine/project_sdk/sdk_info.h"
#include "tinyengine/project_sdk/build_info.h"

namespace tinyengine::project_sdk
{
const char* getSdkBuildId() noexcept
{
    return TINYENGINE_PROJECT_SDK_BUILD_ID;
}
} // namespace tinyengine::project_sdk
