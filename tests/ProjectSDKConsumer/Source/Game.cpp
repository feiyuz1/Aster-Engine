#include <MiniEngine/ProjectSDK/Project.hpp>
#include <MiniEngine/ProjectSDK/SDKInfo.hpp>
#include "ProjectModuleBuildInfo.h"
#include <stdexcept>
using namespace MiniEngine::ProjectSDK;
class RotateSession final : public Session
{
public:
    void OnBeginPlay(Context &context) override
    {
        const auto &info=TinyEngineProject::GetModuleInfo();
        if (std::string_view(GetSDKBuildId()) != std::string_view(info.sdk_build_id.data,static_cast<size_t>(info.sdk_build_id.size)))
            throw std::runtime_error("Linked SDK configuration does not match project metadata");
        m_player=context.FindEntity("Player");
        if (!context.IsAlive(m_player)) throw std::runtime_error("Player is missing");
        context.Log("P0_PROJECT_BEGIN");
    }
    void OnUpdate(Context &context,float dt) override
    {
        TinyProjectTransform value{};
        if (!context.GetTransform(m_player,value)) throw std::runtime_error("Cannot read Player transform");
        value.rotation.y+=90.0f*dt;
        if (!context.SetTransform(m_player,value)) throw std::runtime_error("Cannot write Player transform");
        if (!m_logged) { context.Log("P0_PROJECT_ROTATED"); m_logged=true; }
    }
    void OnEndPlay(Context &context) override { context.Log("P0_PROJECT_END"); }
private:
    TinyProjectEntity m_player{};
    bool m_logged=false;
};
extern "C" TINY_PROJECT_EXPORT const TinyProjectAPI *TINY_PROJECT_CALL TinyProject_GetAPI(uint32_t requestedAbi)
{
    return ExportProject<RotateSession>(requestedAbi,TinyEngineProject::GetModuleInfo());
}
