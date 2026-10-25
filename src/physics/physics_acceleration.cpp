// 文件：src/physics/physics_acceleration.cpp
// 作用：构建和维护物理 AABB 树，按来源元数据复用碰撞网格。
// 约定：树只保守筛选候选，最终接触仍由现有窄阶段决定。
// 类型：实现文件。

#include "physics_acceleration.h"

#include "tinyengine/platform/platform.h"
#include "tinyengine/resource/resource.h"

#include <algorithm>
#include <cmath>

namespace tinyengine::detail
{
namespace
{
Aabb unite(const Aabb& a, const Aabb& b)
{
    return {
        {std::min(a.minimum.x, b.minimum.x), std::min(a.minimum.y, b.minimum.y), std::min(a.minimum.z, b.minimum.z)},
        {std::max(a.maximum.x, b.maximum.x), std::max(a.maximum.y, b.maximum.y), std::max(a.maximum.z, b.maximum.z)}};
}

double area(const Aabb& bounds)
{
    const Vec3 size = bounds.maximum - bounds.minimum;
    return 2.0 * (static_cast<double>(size.x) * size.y + static_cast<double>(size.y) * size.z +
                  static_cast<double>(size.z) * size.x);
}
} // namespace

Aabb transformBounds(const Aabb& bounds, const Mat4& matrix)
{
    // 按矩阵项符号选择极值角点，并保持与 transformPoint 相同的运算顺序。
    // 不经过中心/半径重建，避免 (min + max) / 2 - extent 的抵消误差缩小包围盒。
    const auto coordinate = [&](std::size_t row, bool minimum)
    {
        const float x = (matrix.at(row, 0) >= 0.0f) == minimum ? bounds.minimum.x : bounds.maximum.x;
        const float y = (matrix.at(row, 1) >= 0.0f) == minimum ? bounds.minimum.y : bounds.maximum.y;
        const float z = (matrix.at(row, 2) >= 0.0f) == minimum ? bounds.minimum.z : bounds.maximum.z;
        return matrix.at(row, 0) * x + matrix.at(row, 1) * y + matrix.at(row, 2) * z + matrix.at(row, 3);
    };
    return {{coordinate(0, true), coordinate(1, true), coordinate(2, true)},
            {coordinate(0, false), coordinate(1, false), coordinate(2, false)}};
}

bool sameBounds(const Aabb& left, const Aabb& right)
{
    return left.minimum.x == right.minimum.x && left.minimum.y == right.minimum.y &&
           left.minimum.z == right.minimum.z && left.maximum.x == right.maximum.x &&
           left.maximum.y == right.maximum.y && left.maximum.z == right.maximum.z;
}

int BoundsTree::build(const std::size_t begin, const std::size_t end)
{
    const int index = static_cast<int>(m_nodes.size());
    m_nodes.push_back({m_entries[begin].bounds, begin, end});
    Aabb centers{(m_entries[begin].bounds.minimum + m_entries[begin].bounds.maximum) * 0.5f,
                 (m_entries[begin].bounds.minimum + m_entries[begin].bounds.maximum) * 0.5f};
    for (std::size_t i = begin + 1; i < end; ++i)
    {
        m_nodes[index].bounds = unite(m_nodes[index].bounds, m_entries[i].bounds);
        const Vec3 center = (m_entries[i].bounds.minimum + m_entries[i].bounds.maximum) * 0.5f;
        centers = unite(centers, {center, center});
    }
    if (end - begin > 4)
    {
        const Vec3 size = centers.maximum - centers.minimum;
        const int axis = size.x >= size.y && size.x >= size.z ? 0 : size.y >= size.z ? 1 : 2;
        const auto coordinate = [axis](const Aabb& bounds)
        {
            const Vec3 center = (bounds.minimum + bounds.maximum) * 0.5f;
            return axis == 0 ? center.x : axis == 1 ? center.y : center.z;
        };
        const std::size_t middle = begin + (end - begin) / 2;
        std::nth_element(m_entries.begin() + begin, m_entries.begin() + middle, m_entries.begin() + end,
                         [&](const BoundsEntry& left, const BoundsEntry& right)
                         {
                             const float a = coordinate(left.bounds), b = coordinate(right.bounds);
                             return a == b ? left.payload < right.payload : a < b;
                         });
        const int left = build(begin, middle), right = build(middle, end);
        m_nodes[index].left = left;
        m_nodes[index].right = right;
    }
    return index;
}

bool BoundsTree::update(std::vector<BoundsEntry> entries)
{
    bool rebuild = entries.size() != m_entries.size();
    bool changed = rebuild;
    if (!rebuild)
        for (const auto& entry : entries)
        {
            const auto found = m_payloadIndices.find(entry.payload);
            if (found == m_payloadIndices.end())
            {
                rebuild = true;
                break;
            }
            if (!sameBounds(entry.bounds, m_entries[found->second].bounds))
            {
                m_entries[found->second].bounds = entry.bounds;
                changed = true;
            }
        }
    if (!rebuild && !changed)
        return false;
    if (!rebuild)
    {
        double cost = 0.0;
        for (auto it = m_nodes.rbegin(); it != m_nodes.rend(); ++it)
        {
            if (it->left >= 0)
                it->bounds = unite(m_nodes[it->left].bounds, m_nodes[it->right].bounds);
            else
            {
                it->bounds = m_entries[it->begin].bounds;
                for (std::size_t i = it->begin + 1; i < it->end; ++i)
                    it->bounds = unite(it->bounds, m_entries[i].bounds);
            }
            cost += area(it->bounds);
        }
        // 保留拓扑可摊薄移动成本；空间分布显著改变时重新划分。
        rebuild = cost > std::max(1.0, m_buildCost) * 2.0;
        if (!rebuild)
            return false;
    }
    m_entries = std::move(entries);
    m_nodes.clear();
    m_payloadIndices.clear();
    m_buildCost = 0.0;
    if (m_entries.empty())
        return false;
    m_nodes.reserve(m_entries.size() * 2);
    build(0, m_entries.size());
    for (std::size_t i = 0; i < m_entries.size(); ++i)
        m_payloadIndices.emplace(m_entries[i].payload, i);
    for (const auto& node : m_nodes)
        m_buildCost += area(node.bounds);
    return true;
}

void BoundsTree::visit(const std::function<bool(const Aabb&)>& intersects,
                       const std::function<bool(std::uint64_t)>& visitor, std::uint64_t& boundsTests) const
{
    if (m_nodes.empty())
        return;
    // 中位数分割的深度不超过负载数量的位数；refit 不改变拓扑。
    std::array<int, 64> stack{};
    std::size_t count = 1;
    while (count)
    {
        const auto& node = m_nodes[stack[--count]];
        ++boundsTests;
        if (!intersects(node.bounds))
            continue;
        if (node.left >= 0)
        {
            stack[count++] = node.right;
            stack[count++] = node.left;
        }
        else
            for (std::size_t i = node.begin; i < node.end; ++i)
            {
                ++boundsTests;
                if (intersects(m_entries[i].bounds) && !visitor(m_entries[i].payload))
                    return;
            }
    }
}

void PhysicsAcceleration::beginOperation()
{
    ++epoch;
}

std::shared_ptr<const CollisionMesh> PhysicsAcceleration::meshFor(const std::string& path)
{
    if (path.empty())
        return {};
    const auto filePath = fromUtf8Path(path);
    const auto normalized = filePath.generic_u8string();
    auto& cached = meshes[std::string(reinterpret_cast<const char*>(normalized.data()), normalized.size())];
    if (cached.checkedEpoch == epoch && cached.checkedEpoch != 0)
        return cached.mesh;
    cached.checkedEpoch = epoch;
    const std::string key = PlatformFile::getReadCacheKey(filePath);
    if (cached.sourceKey == key)
        return cached.mesh;
    cached.sourceKey = key;
    cached.mesh.reset();
    ++statistics.meshLoads;
    MeshResource resource;
    if (!resource.load(path) || resource.GetVertices().empty())
        return {};
    auto mesh = std::make_shared<CollisionMesh>();
    mesh->bounds = resource.GetBounds();
    mesh->vertices.reserve(resource.GetVertices().size());
    for (const auto& vertex : resource.GetVertices())
        mesh->vertices.push_back(vertex.position);
    std::vector<BoundsEntry> entries;
    for (std::size_t i = 0; i + 2 < resource.GetIndices().size(); i += 3)
    {
        const auto a = resource.GetIndices()[i], b = resource.GetIndices()[i + 1], c = resource.GetIndices()[i + 2];
        if (a >= mesh->vertices.size() || b >= mesh->vertices.size() || c >= mesh->vertices.size())
            continue;
        const auto payload = mesh->triangles.size();
        mesh->triangles.push_back({a, b, c});
        entries.push_back(
            {payload, unite(unite({mesh->vertices[a], mesh->vertices[a]}, {mesh->vertices[b], mesh->vertices[b]}),
                            {mesh->vertices[c], mesh->vertices[c]})});
    }
    mesh->tree.update(std::move(entries));
    cached.mesh = std::move(mesh);
    return cached.mesh;
}
} // namespace tinyengine::detail
