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
    void WriteBool(bool value);
    void WriteU32(std::uint32_t value);
    void WriteU64(std::uint64_t value);
    void WriteFloat(float value);
    void WriteString(const std::string &value);
    void WriteBytes(const void *data, std::size_t size);
    const std::vector<std::uint8_t> &GetData() const;
    bool Save(const std::filesystem::path &path) const;

  private:
    std::vector<std::uint8_t> m_data;
};

class BinaryReader
{
  public:
    explicit BinaryReader(const std::vector<std::uint8_t> &data);
    bool ReadBool(bool &value);
    bool ReadU32(std::uint32_t &value);
    bool ReadU64(std::uint64_t &value);
    bool ReadFloat(float &value);
    bool ReadString(std::string &value, std::size_t maximumLength = 16 * 1024 * 1024);
    bool ReadBytes(void *destination, std::size_t size);
    std::size_t GetRemainingBytes() const;
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
    static std::string Serialize(const VersionedDocument &document);
    static bool Deserialize(const std::string &text, VersionedDocument &document, std::string *error = nullptr);
    static bool SaveAtomic(const std::filesystem::path &path, const VersionedDocument &document, bool keepBackup = true);
};

class MigrationRegistry
{
  public:
    using Migration = std::function<bool(VersionedDocument &, std::string &)>;

    bool Register(std::string format, std::uint32_t sourceVersion, Migration migration);
    bool Migrate(VersionedDocument &document, std::uint32_t targetVersion, std::string *error = nullptr) const;

  private:
    struct Key
    {
        std::string format;
        std::uint32_t version = 0;
        bool operator==(const Key &other) const;
    };
    struct KeyHash
    {
        std::size_t operator()(const Key &key) const;
    };
    std::unordered_map<Key, Migration, KeyHash> m_migrations;
};
} // namespace MiniEngine
