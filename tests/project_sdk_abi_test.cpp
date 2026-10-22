#include "tinyengine/project_sdk/build_info.h"
#include "tinyengine/project_sdk/project_api.h"
#include "tinyengine/project_sdk/sdk_info.h"

#include <cstddef>
#include <cstring>
#include <iostream>
#include <type_traits>

extern "C" int projectSdkExerciseC(const TinyProjectHostAPI* host);

namespace
{
TinyProjectResult TINY_PROJECT_CALL Find(void* context, TinyProjectWorld world, TinyProjectString name,
                                         TinyProjectEntity* out)
{
    auto& called = *static_cast<bool*>(context);
    called = world.id == 41 && name.size == 6 && std::memcmp(name.data, "Player", 6) == 0;
    *out = {world.id, 0x100000007ULL};
    return called ? TINY_PROJECT_OK : TINY_PROJECT_INVALID_ARGUMENT;
}
} // namespace

int main()
{
    static_assert(std::is_standard_layout<TinyProjectHostAPI>::value, "C ABI must be standard layout");
    static_assert(sizeof(TinyProjectEntity) == 16, "Entity ABI changed");
    static_assert(sizeof(TinyProjectVec3) == 12, "Vector ABI changed");
    static_assert(offsetof(TinyProjectHostAPI, host_context) == 8, "Function table ABI changed");
    bool called = false;
    TinyProjectHostAPI host{};
    host.abi_version = TINY_PROJECT_ABI_VERSION;
    host.struct_size = sizeof(host);
    host.host_context = &called;
    host.find_entity = &Find;
    if (!projectSdkExerciseC(&host) || !called)
    {
        std::cerr << "C/C++ function-table round trip failed\n";
        return 1;
    }
    const char* id = tinyengine::project_sdk::getSdkBuildId();
    if (!id || !*id || std::strcmp(id, TINYENGINE_PROJECT_SDK_BUILD_ID) != 0)
    {
        std::cerr << "SDK library and public header identities differ\n";
        return 1;
    }
    std::cout << "SDK ABI C/C++ round trip PASS; build=" << id << '\n';
}
