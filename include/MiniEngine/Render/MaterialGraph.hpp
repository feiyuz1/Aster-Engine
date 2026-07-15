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
    std::uint32_t AddNode(MaterialNodeType type, std::string name, Vec2 position = {});
    bool RemoveNode(std::uint32_t id);
    bool Connect(std::uint32_t fromNode, std::uint32_t toNode, std::uint32_t toInput);
    bool Load(const std::filesystem::path &path);
    bool Save(const std::filesystem::path &path) const;
    std::string Compile(std::vector<std::string> *errors = nullptr) const;
    std::vector<MaterialNode> &GetNodes();
    const std::vector<MaterialNode> &GetNodes() const;
    std::vector<MaterialLink> &GetLinks();
    const std::vector<MaterialLink> &GetLinks() const;

  private:
    std::vector<MaterialNode> m_nodes;
    std::vector<MaterialLink> m_links;
    std::uint32_t m_nextId = 1;
};
} // namespace MiniEngine
