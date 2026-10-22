#pragma once

#if defined(TINYENGINE_PROJECT_SDK_DEBUG)
#include "tinyengine/project_sdk/build_info_debug.h"
#elif defined(TINYENGINE_PROJECT_SDK_RELEASE)
#include "tinyengine/project_sdk/build_info_release.h"
#elif defined(TINYENGINE_PROJECT_SDK_RELWITHDEBINFO)
#include "tinyengine/project_sdk/build_info_rel_with_deb_info.h"
#elif defined(TINYENGINE_PROJECT_SDK_MINSIZEREL)
#include "tinyengine/project_sdk/build_info_min_size_rel.h"
#else
#error "Link tinyengine::project_sdk using a supported CMake configuration"
#endif
