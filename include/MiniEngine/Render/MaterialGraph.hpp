// 文件：include/MiniEngine/Render/MaterialGraph.hpp
// 作用：定义可视化材质节点图并生成可供着色器管线使用的源码。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Math/Math.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace MiniEngine
{
enum class MaterialNodeType
{
    Constant,
    Texture,
    Add,
    Multiply,
    PbrOutput
};
struct MaterialNode
{
    std::uint32_t id = 0;
    MaterialNodeType type = MaterialNodeType::Constant;
    std::string name;
    Vec2 position{};
    Vec4 value{1.0f, 1.0f, 1.0f, 1.0f};
    std::string texturePath;
};
struct MaterialLink
{
    std::uint32_t fromNode = 0;
    std::uint32_t toNode = 0;
    std::uint32_t toInput = 0;
};
class MaterialGraph
{
  public:
    /// @brief 添加 `Node`。
    /// @param type 目标状态、模式或类型。
    /// @param name 对象名称或注册键。
    /// @param position 世界空间或局部空间位置。
    /// @return 返回计算得到的数值、数量或索引。
    std::uint32_t AddNode(MaterialNodeType type, std::string name, Vec2 position = {});
    /// @brief 移除 `Node`。
    /// @param id 目标对象、消息或任务标识符。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool RemoveNode(std::uint32_t id);
    /// @brief 连接两个节点或端点。
    /// @param fromNode 接口所需的输入参数。
    /// @param toNode 接口所需的输入参数。
    /// @param toInput 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Connect(std::uint32_t fromNode, std::uint32_t toNode, std::uint32_t toInput);
    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Load(const std::filesystem::path &path);
    /// @brief 保存当前数据。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Save(const std::filesystem::path &path) const;
    /// @brief 编译当前描述并生成可执行结果。
    /// @param errors 用于接收或提供诊断信息的文本。
    /// @return 返回对应的文本或路径。
    std::string Compile(std::vector<std::string> *errors = nullptr) const;
    /// @brief 获取 `Nodes`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    std::vector<MaterialNode> &GetNodes();
    /// @brief 获取 `Nodes`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<MaterialNode> &GetNodes() const;
    /// @brief 获取 `Links`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    std::vector<MaterialLink> &GetLinks();
    /// @brief 获取 `Links`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<MaterialLink> &GetLinks() const;

  private:
    std::vector<MaterialNode> m_nodes;
    std::vector<MaterialLink> m_links;
    std::uint32_t m_nextId = 1;
};
} // 结束 MiniEngine 命名空间
