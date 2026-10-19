#pragma once

#if defined(TINYENGINE_PROJECT_SDK_DEBUG)
#include "MiniEngine/ProjectSDK/BuildInfoDebug.h"
#elif defined(TINYENGINE_PROJECT_SDK_RELEASE)
#include "MiniEngine/ProjectSDK/BuildInfoRelease.h"
#elif defined(TINYENGINE_PROJECT_SDK_RELWITHDEBINFO)
#include "MiniEngine/ProjectSDK/BuildInfoRelWithDebInfo.h"
#elif defined(TINYENGINE_PROJECT_SDK_MINSIZEREL)
#include "MiniEngine/ProjectSDK/BuildInfoMinSizeRel.h"
#else
#error "Link TinyEngine::ProjectSDK using a supported CMake configuration"
#endif
