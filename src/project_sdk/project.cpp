#include "tinyengine/project_sdk/project.h"
namespace tinyengine::project_sdk
{
namespace
{
TinyProjectString span(std::string_view s)
{
    return {s.data(), static_cast<uint64_t>(s.size())};
}
} // namespace
TinyProjectEntity Context::findEntity(std::string_view name)
{
    TinyProjectEntity out{};
    m_result = m_host.find_entity ? m_host.find_entity(m_host.host_context, m_world, span(name), &out)
                                  : TINY_PROJECT_UNSUPPORTED;
    return out;
}
bool Context::isAlive(TinyProjectEntity e)
{
    uint32_t out = 0;
    m_result = m_host.is_alive ? m_host.is_alive(m_host.host_context, e, &out) : TINY_PROJECT_UNSUPPORTED;
    return m_result == TINY_PROJECT_OK && out;
}
bool Context::getTransform(TinyProjectEntity e, TinyProjectTransform& out)
{
    out = {};
    m_result = m_host.get_transform ? m_host.get_transform(m_host.host_context, e, &out) : TINY_PROJECT_UNSUPPORTED;
    return m_result == TINY_PROJECT_OK;
}
bool Context::setTransform(TinyProjectEntity e, const TinyProjectTransform& v)
{
    m_result = m_host.set_transform ? m_host.set_transform(m_host.host_context, e, &v) : TINY_PROJECT_UNSUPPORTED;
    return m_result == TINY_PROJECT_OK;
}
float Context::getAxis(std::string_view name)
{
    float out = 0;
    m_result = m_host.get_axis ? m_host.get_axis(m_host.host_context, span(name), &out) : TINY_PROJECT_UNSUPPORTED;
    return out;
}
bool Context::isDown(std::string_view name)
{
    uint32_t out = 0;
    m_result = m_host.is_down ? m_host.is_down(m_host.host_context, span(name), &out) : TINY_PROJECT_UNSUPPORTED;
    return m_result == TINY_PROJECT_OK && out;
}
void Context::log(std::string_view text) const noexcept
{
    try
    {
        if (m_host.log)
            m_host.log(m_host.host_context, TINY_PROJECT_LOG_INFO, span(text));
    }
    catch (...)
    {
    }
}
} // namespace tinyengine::project_sdk
