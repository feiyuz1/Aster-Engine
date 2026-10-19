#include "MiniEngine/ProjectSDK/ProjectAPI.h"

#include <stddef.h>
#include <string.h>

/* Exercise the public function table from a C translation unit. */
int ProjectSDKExerciseC(const TinyProjectHostAPI *host)
{
    TinyProjectEntity entity = {0, 0};
    TinyProjectWorld world = {41};
    TinyProjectString name = {"Player", 6};
    if (sizeof(TinyProjectEntity) != 16 || sizeof(TinyProjectVec3) != 12 ||
        offsetof(TinyProjectHostAPI, host_context) != 8)
        return 0;
    if (host->find_entity(host->host_context, world, name, &entity) != TINY_PROJECT_OK)
        return 0;
    return entity.world_id == 41 && entity.value == 0x100000007ULL;
}
