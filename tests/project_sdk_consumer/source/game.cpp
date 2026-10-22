#include "project_module_build_info.h"
#include <stdexcept>
#include <tinyengine/project_sdk/project.h>
#include <tinyengine/project_sdk/sdk_info.h>
using namespace tinyengine::project_sdk;
class RotateSession final : public Session
{
  public:
    void onBeginPlay(Context& context) override
    {
        const auto& info = tinyengine::project_module::getModuleInfo();
        if (std::string_view(getSdkBuildId()) !=
            std::string_view(info.sdk_build_id.data, static_cast<size_t>(info.sdk_build_id.size)))
            throw std::runtime_error("Linked SDK configuration does not match project metadata");
        m_player = context.findEntity("Player");
        if (!context.isAlive(m_player))
            throw std::runtime_error("Player is missing");
        context.log("P0_PROJECT_BEGIN");
    }
    void onUpdate(Context& context, float dt) override
    {
        TinyProjectTransform value{};
        if (!context.getTransform(m_player, value))
            throw std::runtime_error("Cannot read Player transform");
        value.rotation.y += 90.0f * dt;
        if (!context.setTransform(m_player, value))
            throw std::runtime_error("Cannot write Player transform");
        if (!m_logged)
        {
            context.log("P0_PROJECT_ROTATED");
            m_logged = true;
        }
    }
    void onEndPlay(Context& context) override
    {
        context.log("P0_PROJECT_END");
    }

  private:
    TinyProjectEntity m_player{};
    bool m_logged = false;
};
extern "C" TINY_PROJECT_EXPORT const TinyProjectAPI* TINY_PROJECT_CALL TinyProject_GetAPI(uint32_t requestedAbi)
{
    return ExportProject<RotateSession>(requestedAbi, tinyengine::project_module::getModuleInfo());
}
