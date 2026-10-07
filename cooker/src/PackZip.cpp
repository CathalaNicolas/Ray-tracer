#include "CookMesh.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace
{

void appendU16(std::vector<std::uint8_t> &out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void appendU32(std::vector<std::uint8_t> &out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

std::uint32_t crc32(const std::uint8_t *data, std::size_t size)
{
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}

struct Entry
{
    std::string name;
    std::vector<std::uint8_t> data;
    std::uint32_t crc = 0;
};

} // namespace

bool packCookedZip(const std::filesystem::path &cookedDir, const std::filesystem::path &zipPath, std::string &error)
{
    std::vector<Entry> entries;
    std::error_code ec;
    for (const auto &file : std::filesystem::recursive_directory_iterator(cookedDir, ec))
    {
        if (!file.is_regular_file())
            continue;
        if (file.path().filename() == zipPath.filename())
            continue;
        Entry entry;
        entry.name = std::filesystem::relative(file.path(), cookedDir, ec).generic_string();
        std::ifstream in(file.path(), std::ios::binary);
        entry.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        entry.crc = crc32(entry.data.data(), entry.data.size());
        entries.push_back(std::move(entry));
    }
    std::vector<std::uint8_t> zip;
    std::vector<std::uint32_t> localOffsets;
    for (const Entry &entry : entries)
    {
        localOffsets.push_back(static_cast<std::uint32_t>(zip.size()));
        zip.insert(zip.end(), {'P', 'K', 3, 4});
        appendU16(zip, 20);
        appendU16(zip, 0);
        appendU16(zip, 0);
        appendU16(zip, 0);
        appendU16(zip, 0);
        appendU32(zip, entry.crc);
        appendU32(zip, static_cast<std::uint32_t>(entry.data.size()));
        appendU32(zip, static_cast<std::uint32_t>(entry.data.size()));
        appendU16(zip, static_cast<std::uint16_t>(entry.name.size()));
        appendU16(zip, 0);
        zip.insert(zip.end(), entry.name.begin(), entry.name.end());
        zip.insert(zip.end(), entry.data.begin(), entry.data.end());
    }
    const std::uint32_t centralStart = static_cast<std::uint32_t>(zip.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const Entry &entry = entries[i];
        zip.insert(zip.end(), {'P', 'K', 1, 2});
        appendU16(zip, 20);
        appendU16(zip, 20);
        appendU16(zip, 0);
        appendU16(zip, 0);
        appendU16(zip, 0);
        appendU16(zip, 0);
        appendU32(zip, entry.crc);
        appendU32(zip, static_cast<std::uint32_t>(entry.data.size()));
        appendU32(zip, static_cast<std::uint32_t>(entry.data.size()));
        appendU16(zip, static_cast<std::uint16_t>(entry.name.size()));
        appendU16(zip, 0);
        appendU16(zip, 0);
        appendU16(zip, 0);
        appendU16(zip, 0);
        appendU32(zip, 0);
        appendU32(zip, localOffsets[i]);
        zip.insert(zip.end(), entry.name.begin(), entry.name.end());
    }
    const std::uint32_t centralSize = static_cast<std::uint32_t>(zip.size() - centralStart);
    zip.insert(zip.end(), {'P', 'K', 5, 6});
    appendU16(zip, 0);
    appendU16(zip, 0);
    appendU16(zip, static_cast<std::uint16_t>(entries.size()));
    appendU16(zip, static_cast<std::uint16_t>(entries.size()));
    appendU32(zip, centralSize);
    appendU32(zip, centralStart);
    appendU16(zip, 0);
    std::filesystem::create_directories(zipPath.parent_path());
    std::ofstream out(zipPath, std::ios::binary);
    if (!out)
    {
        error = "could not write zip";
        return false;
    }
    out.write(reinterpret_cast<const char *>(zip.data()), static_cast<std::streamsize>(zip.size()));
    return true;
}
