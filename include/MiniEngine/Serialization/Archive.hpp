// 文件：include/MiniEngine/Serialization/Archive.hpp
// 作用：提供有边界检查的二进制读写器，作为网络和资源格式的基础。
// 约定：格式读取始终执行尺寸和版本检查，写入保持确定性。
// 类型：公开声明。

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
class BinaryWriter
{
  public:
    /// @brief 写入 `Bool`。
    /// @param value 要写入、比较或计算的值。
    void WriteBool(bool value);
    /// @brief 写入 `U32`。
    /// @param value 要写入、比较或计算的值。
    void WriteU32(std::uint32_t value);
    /// @brief 写入 `U64`。
    /// @param value 要写入、比较或计算的值。
    void WriteU64(std::uint64_t value);
    /// @brief 写入 `Float`。
    /// @param value 要写入、比较或计算的值。
    void WriteFloat(float value);
    /// @brief 写入 `String`。
    /// @param value 要写入、比较或计算的值。
    void WriteString(const std::string &value);
    /// @brief 写入 `Bytes`。
    /// @param data 要读取、写入或发送的数据。
    /// @param size 数据、内存或目标区域的尺寸。
    void WriteBytes(const void *data, std::size_t size);
    /// @brief 获取 `Data`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<std::uint8_t> &GetData() const;
    /// @brief 保存当前数据。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Save(const std::filesystem::path &path) const;

  private:
    std::vector<std::uint8_t> m_data;
};

class BinaryReader
{
  public:
    /// @brief 构造 `BinaryReader` 对象。
    /// @param data 要读取、写入或发送的数据。
    explicit BinaryReader(const std::vector<std::uint8_t> &data);
    /// @brief 读取 `Bool`。
    /// @param value 要写入、比较或计算的值。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool ReadBool(bool &value);
    /// @brief 读取 `U32`。
    /// @param value 要写入、比较或计算的值。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool ReadU32(std::uint32_t &value);
    /// @brief 读取 `U64`。
    /// @param value 要写入、比较或计算的值。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool ReadU64(std::uint64_t &value);
    /// @brief 读取 `Float`。
    /// @param value 要写入、比较或计算的值。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool ReadFloat(float &value);
    /// @brief 读取 `String`。
    /// @param value 要写入、比较或计算的值。
    /// @param maximumLength 目标尺寸、数量、索引或限制值。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool ReadString(std::string &value, std::size_t maximumLength = 16 * 1024 * 1024);
    /// @brief 读取 `Bytes`。
    /// @param destination 目标路径、对象或输出缓冲区。
    /// @param size 数据、内存或目标区域的尺寸。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool ReadBytes(void *destination, std::size_t size);
    /// @brief 获取 `RemainingBytes`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetRemainingBytes() const;
    /// @brief 判断是否满足 `Valid` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsValid() const;

  private:
    const std::vector<std::uint8_t> &m_data;
    std::size_t m_offset = 0;
    bool m_valid = true;
};

struct VersionedDocument
{
    std::string format;
    std::uint32_t version = 0;
    std::unordered_map<std::string, std::string> fields;
};

class VersionedTextArchive
{
  public:
    /// @brief 将对象序列化为持久化数据。
    /// @param document 要保存、迁移或恢复的文档。
    /// @return 返回对应的文本或路径。
    static std::string Serialize(const VersionedDocument &document);
    /// @brief 从持久化数据恢复对象。
    /// @param text 要处理的 UTF-8 或普通文本。
    /// @param document 要保存、迁移或恢复的文档。
    /// @param error 可选的错误信息输出；可为空。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool Deserialize(const std::string &text, VersionedDocument &document, std::string *error = nullptr);
    /// @brief 保存 `Atomic`。
    /// @param path 文件、资源或目录路径。
    /// @param document 要保存、迁移或恢复的文档。
    /// @param keepBackup 是否启用对应行为。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool SaveAtomic(const std::filesystem::path &path, const VersionedDocument &document, bool keepBackup = true);
};

class MigrationRegistry
{
  public:
    using Migration = std::function<bool(VersionedDocument &, std::string &)>;

    /// @brief 注册对象或处理器。
    /// @param format 用于查找或标识对象的名称。
    /// @param sourceVersion 文档当前格式版本号。
    /// @param migration 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Register(std::string format, std::uint32_t sourceVersion, Migration migration);
    /// @brief 按已注册步骤把文档迁移到目标版本。
    /// @param document 要保存、迁移或恢复的文档。
    /// @param targetVersion 期望迁移到的格式版本号。
    /// @param error 可选的错误信息输出；可为空。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Migrate(VersionedDocument &document, std::uint32_t targetVersion, std::string *error = nullptr) const;

  private:
    struct Key
    {
        std::string format;
        std::uint32_t version = 0;
        /// @brief 实现 `==` 运算符语义。
        /// @param other 用于比较的另一个对象。
        /// @return 操作成功时返回 `true`，否则返回 `false`。
        bool operator==(const Key &other) const;
    };
    struct KeyHash
    {
        /// @brief 实现 `()` 运算符语义。
        /// @param key 用于查找数据的键。
        /// @return 返回计算得到的数值、数量或索引。
        std::size_t operator()(const Key &key) const;
    };
    std::unordered_map<Key, Migration, KeyHash> m_migrations;
};
} // namespace MiniEngine
