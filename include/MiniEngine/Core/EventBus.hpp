// 文件：include/MiniEngine/Core/EventBus.hpp
// 作用：提供按事件类型订阅和同步广播的轻量消息总线。
// 约定：核心层不依赖上层业务，修改公开契约时需检查所有模块。
// 类型：公开声明。

#pragma once

#include <functional>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
struct Event
{
    virtual ~Event() = default;
};

class EventBus
{
  public:
    template <typename TEvent> using Handler = std::function<void(const TEvent &)>;

    template <typename TEvent> void Subscribe(Handler<TEvent> handler)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_handlers[typeid(TEvent)].push_back(
            [captured = std::move(handler)](const Event &event) { captured(static_cast<const TEvent &>(event)); });
    }

    template <typename TEvent> void Publish(const TEvent &event)
    {
        std::vector<BaseHandler> localHandlers;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto found = m_handlers.find(typeid(TEvent));
            if (found == m_handlers.end())
            {
                return;
            }
            localHandlers = found->second;
        }

        for (const auto &handler : localHandlers)
        {
            handler(event);
        }
    }

  private:
    using BaseHandler = std::function<void(const Event &)>;
    std::unordered_map<std::type_index, std::vector<BaseHandler>> m_handlers;
    std::mutex m_mutex;
};
} // namespace MiniEngine
