#include "MiniEngine/Serialization/Archive.hpp"

#include "MiniEngine/Platform/Platform.hpp"

#include <cstring>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace MiniEngine
{
namespace
{
void SetError(std::string *error, std::string message)
{
    if (error)
        *error = std::move(message);
}
} // namespace

void BinaryWriter::WriteBool(const bool value)
{
    m_data.push_back(value ? 1u : 0u);
}

void BinaryWriter::WriteU32(const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        m_data.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

void BinaryWriter::WriteU64(const std::uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8)
        m_data.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

void BinaryWriter::WriteFloat(const float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t), "TinyEngine archives require 32-bit floats");
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    WriteU32(bits);
}

void BinaryWriter::WriteString(const std::string &value)
{
    WriteU64(static_cast<std::uint64_t>(value.size()));
    WriteBytes(value.data(), value.size());
}

void BinaryWriter::WriteBytes(const void *data, const std::size_t size)
{
    if (!data || size == 0)
        return;
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    m_data.insert(m_data.end(), bytes, bytes + size);
}

const std::vector<std::uint8_t> &BinaryWriter::GetData() const
{
    return m_data;
}

bool BinaryWriter::Save(const std::filesystem::path &path) const
{
    return PlatformFile::WriteAllBytes(path, m_data);
}

BinaryReader::BinaryReader(const std::vector<std::uint8_t> &data) : m_data(data)
{
}

bool BinaryReader::ReadBool(bool &value)
{
    std::uint8_t byte = 0;
    if (!ReadBytes(&byte, sizeof(byte)) || byte > 1u)
    {
        m_valid = false;
        return false;
    }
    value = byte != 0;
    return true;
}

bool BinaryReader::ReadU32(std::uint32_t &value)
{
    value = 0;
    std::uint8_t bytes[4]{};
    if (!ReadBytes(bytes, sizeof(bytes)))
        return false;
    for (unsigned index = 0; index < 4; ++index)
        value |= static_cast<std::uint32_t>(bytes[index]) << (index * 8u);
    return true;
}

bool BinaryReader::ReadU64(std::uint64_t &value)
{
    value = 0;
    std::uint8_t bytes[8]{};
    if (!ReadBytes(bytes, sizeof(bytes)))
        return false;
    for (unsigned index = 0; index < 8; ++index)
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8u);
    return true;
}

bool BinaryReader::ReadFloat(float &value)
{
    std::uint32_t bits = 0;
    if (!ReadU32(bits))
        return false;
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

bool BinaryReader::ReadString(std::string &value, const std::size_t maximumLength)
{
    std::uint64_t size = 0;
    if (!ReadU64(size) || size > maximumLength || size > GetRemainingBytes())
    {
        m_valid = false;
        return false;
    }
    value.assign(reinterpret_cast<const char *>(m_data.data() + m_offset), static_cast<std::size_t>(size));
    m_offset += static_cast<std::size_t>(size);
    return true;
}

bool BinaryReader::ReadBytes(void *destination, const std::size_t size)
{
    if (!m_valid || size > GetRemainingBytes() || (size != 0 && !destination))
    {
        m_valid = false;
        return false;
    }
    if (size != 0)
        std::memcpy(destination, m_data.data() + m_offset, size);
    m_offset += size;
    return true;
}

std::size_t BinaryReader::GetRemainingBytes() const
{
    return m_offset <= m_data.size() ? m_data.size() - m_offset : 0;
}

bool BinaryReader::IsValid() const
{
    return m_valid;
}

std::string VersionedTextArchive::Serialize(const VersionedDocument &document)
{
    std::vector<std::string> keys;
    keys.reserve(document.fields.size());
    for (const auto &field : document.fields)
        keys.push_back(field.first);
    std::sort(keys.begin(), keys.end());
    std::ostringstream output;
    output << "TINYARCHIVE " << std::quoted(document.format) << ' ' << document.version << '\n';
    for (const auto &key : keys)
        output << "field " << std::quoted(key) << ' ' << std::quoted(document.fields.at(key)) << '\n';
    return output.str();
}

bool VersionedTextArchive::Deserialize(const std::string &text, VersionedDocument &document, std::string *error)
{
    std::istringstream input(text);
    std::string signature;
    VersionedDocument parsed;
    if (!(input >> signature >> std::quoted(parsed.format) >> parsed.version) || signature != "TINYARCHIVE" || parsed.format.empty() ||
        parsed.version == 0)
    {
        SetError(error, "Invalid versioned archive header");
        return false;
    }
    std::string token;
    while (input >> token)
    {
        std::string key, value;
        if (token != "field" || !(input >> std::quoted(key) >> std::quoted(value)) || key.empty())
        {
            SetError(error, "Invalid versioned archive field");
            return false;
        }
        parsed.fields[std::move(key)] = std::move(value);
    }
    document = std::move(parsed);
    return true;
}

bool VersionedTextArchive::SaveAtomic(const std::filesystem::path &path, const VersionedDocument &document, const bool keepBackup)
{
    const auto temporary = path.string() + ".tmp";
    const auto backup = path.string() + ".bak";
    if (!PlatformFile::WriteAllText(temporary, Serialize(document)))
        return false;
    std::error_code error;
    if (keepBackup && std::filesystem::is_regular_file(path))
    {
        std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, error);
        error.clear();
    }
    std::filesystem::rename(temporary, path, error);
    if (!error)
        return true;
    error.clear();
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    return !error;
}

bool MigrationRegistry::Key::operator==(const Key &other) const
{
    return version == other.version && format == other.format;
}

std::size_t MigrationRegistry::KeyHash::operator()(const Key &key) const
{
    return std::hash<std::string>{}(key.format) ^ (static_cast<std::size_t>(key.version) << 1u);
}

bool MigrationRegistry::Register(std::string format, const std::uint32_t sourceVersion, Migration migration)
{
    if (format.empty() || sourceVersion == 0 || !migration)
        return false;
    return m_migrations.emplace(Key{std::move(format), sourceVersion}, std::move(migration)).second;
}

bool MigrationRegistry::Migrate(VersionedDocument &document, const std::uint32_t targetVersion, std::string *error) const
{
    if (document.version == 0 || targetVersion < document.version)
    {
        SetError(error, "Cannot migrate archive backwards");
        return false;
    }
    while (document.version < targetVersion)
    {
        const auto found = m_migrations.find({document.format, document.version});
        if (found == m_migrations.end())
        {
            SetError(error, "Missing migration from version " + std::to_string(document.version));
            return false;
        }
        std::string migrationError;
        if (!found->second(document, migrationError))
        {
            SetError(error, migrationError.empty() ? "Migration failed" : migrationError);
            return false;
        }
        ++document.version;
    }
    return true;
}
} // namespace MiniEngine
