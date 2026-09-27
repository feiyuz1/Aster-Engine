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
    /// @brief 析构 `Event` 对象并释放其持有资源。
    virtual ~Event() = default;
};

class EventBus
{
  public:
    template <typename TEvent> using Handler = std::function<void(const TEvent &)>;

    /// @brief 执行 `Subscribe` 所表示的操作。
    /// @param handler 收到事件或 RPC 时调用的处理器。
    template <typename TEvent> void Subscribe(Handler<TEvent> handler)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_handlers[typeid(TEvent)].push_back(
            [captured = std::move(handler)](const Event &event) { captured(static_cast<const TEvent &>(event)); });
    }

    /// @brief 执行 `Publish` 所表示的操作。
    /// @param event 接口所需的输入参数。
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
} // 结束 MiniEngine 命名空间
