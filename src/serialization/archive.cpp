// 文件：src/serialization/archive.cpp
// 作用：提供有边界检查的二进制读写器，作为网络和资源格式的基础。
// 约定：格式读取始终执行尺寸和版本检查，写入保持确定性。
// 类型：实现文件。

#include "tinyengine/serialization/archive.h"

#include "tinyengine/platform/platform.h"

#include <cstring>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace tinyengine
{
namespace
{
void setError(std::string* error, std::string message)
{
    if (error)
        *error = std::move(message);
}
} // namespace

void BinaryWriter::writeBool(const bool value)
{
    m_data.push_back(value ? 1u : 0u);
}

void BinaryWriter::writeU32(const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        m_data.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

void BinaryWriter::writeU64(const std::uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8)
        m_data.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

void BinaryWriter::writeFloat(const float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t), "TinyEngine archives require 32-bit floats");
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU32(bits);
}

void BinaryWriter::writeString(const std::string& value)
{
    writeU64(static_cast<std::uint64_t>(value.size()));
    writeBytes(value.data(), value.size());
}

void BinaryWriter::writeBytes(const void* data, const std::size_t size)
{
    if (!data || size == 0)
        return;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    m_data.insert(m_data.end(), bytes, bytes + size);
}

const std::vector<std::uint8_t>& BinaryWriter::GetData() const
{
    return m_data;
}

bool BinaryWriter::Save(const std::filesystem::path& path) const
{
    return PlatformFile::writeAllBytes(path, m_data);
}

BinaryReader::BinaryReader(const std::vector<std::uint8_t>& data) : m_data(data) {}

bool BinaryReader::readBool(bool& value)
{
    std::uint8_t byte = 0;
    if (!readBytes(&byte, sizeof(byte)) || byte > 1u)
    {
        m_valid = false;
        return false;
    }
    value = byte != 0;
    return true;
}

bool BinaryReader::readU32(std::uint32_t& value)
{
    value = 0;
    std::uint8_t bytes[4]{};
    if (!readBytes(bytes, sizeof(bytes)))
        return false;
    for (unsigned index = 0; index < 4; ++index)
        value |= static_cast<std::uint32_t>(bytes[index]) << (index * 8u);
    return true;
}

bool BinaryReader::readU64(std::uint64_t& value)
{
    value = 0;
    std::uint8_t bytes[8]{};
    if (!readBytes(bytes, sizeof(bytes)))
        return false;
    for (unsigned index = 0; index < 8; ++index)
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8u);
    return true;
}

bool BinaryReader::readFloat(float& value)
{
    std::uint32_t bits = 0;
    if (!readU32(bits))
        return false;
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

// 先验证声明长度、调用方上限和剩余字节，再修改输出字符串，抵御损坏或恶意输入。
bool BinaryReader::readString(std::string& value, const std::size_t maximumLength)
{
    std::uint64_t size = 0;
    if (!readU64(size) || size > maximumLength || size > getRemainingBytes())
    {
        m_valid = false;
        return false;
    }
    value.assign(reinterpret_cast<const char*>(m_data.data() + m_offset), static_cast<std::size_t>(size));
    m_offset += static_cast<std::size_t>(size);
    return true;
}

bool BinaryReader::readBytes(void* destination, const std::size_t size)
{
    if (!m_valid || size > getRemainingBytes() || (size != 0 && !destination))
    {
        m_valid = false;
        return false;
    }
    if (size != 0)
        std::memcpy(destination, m_data.data() + m_offset, size);
    m_offset += size;
    return true;
}

std::size_t BinaryReader::getRemainingBytes() const
{
    return m_offset <= m_data.size() ? m_data.size() - m_offset : 0;
}

bool BinaryReader::isValid() const
{
    return m_valid;
}

std::string VersionedTextArchive::serialize(const VersionedDocument& document)
{
    std::vector<std::string> keys;
    keys.reserve(document.fields.size());
    for (const auto& field : document.fields)
        keys.push_back(field.first);
    std::sort(keys.begin(), keys.end());
    std::ostringstream output;
    output << "TINYARCHIVE " << std::quoted(document.format) << ' ' << document.version << '\n';
    for (const auto& key : keys)
        output << "field " << std::quoted(key) << ' ' << std::quoted(document.fields.at(key)) << '\n';
    return output.str();
}

bool VersionedTextArchive::deserialize(const std::string& text, VersionedDocument& document, std::string* error)
{
    std::istringstream input(text);
    std::string signature;
    VersionedDocument parsed;
    if (!(input >> signature >> std::quoted(parsed.format) >> parsed.version) || signature != "TINYARCHIVE" ||
        parsed.format.empty() || parsed.version == 0)
    {
        setError(error, "Invalid versioned archive header");
        return false;
    }
    std::string token;
    while (input >> token)
    {
        std::string key, value;
        if (token != "field" || !(input >> std::quoted(key) >> std::quoted(value)) || key.empty())
        {
            setError(error, "Invalid versioned archive field");
            return false;
        }
        parsed.fields[std::move(key)] = std::move(value);
    }
    document = std::move(parsed);
    return true;
}

// 先写同目录临时文件并关闭句柄，再原子替换目标；崩溃时旧文件或临时文件至少保留一份。
bool VersionedTextArchive::saveAtomic(const std::filesystem::path& path, const VersionedDocument& document,
                                      const bool keepBackup)
{
    return PlatformFile::writeAllTextAtomic(path, serialize(document), keepBackup);
}

bool MigrationRegistry::Key::operator==(const Key& other) const
{
    return version == other.version && format == other.format;
}

std::size_t MigrationRegistry::KeyHash::operator()(const Key& key) const
{
    return std::hash<std::string>{}(key.format) ^ (static_cast<std::size_t>(key.version) << 1u);
}

bool MigrationRegistry::Register(std::string format, const std::uint32_t sourceVersion, Migration migration)
{
    if (format.empty() || sourceVersion == 0 || !migration)
        return false;
    return m_migrations.emplace(Key{std::move(format), sourceVersion}, std::move(migration)).second;
}

bool MigrationRegistry::migrate(VersionedDocument& document, const std::uint32_t targetVersion,
                                std::string* error) const
{
    if (document.version == 0 || targetVersion < document.version)
    {
        setError(error, "Cannot migrate archive backwards");
        return false;
    }
    while (document.version < targetVersion)
    {
        const auto found = m_migrations.find({document.format, document.version});
        if (found == m_migrations.end())
        {
            setError(error, "Missing migration from version " + std::to_string(document.version));
            return false;
        }
        std::string migrationError;
        if (!found->second(document, migrationError))
        {
            setError(error, migrationError.empty() ? "Migration failed" : migrationError);
            return false;
        }
        ++document.version;
    }
    return true;
}
} // namespace tinyengine
