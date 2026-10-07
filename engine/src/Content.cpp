#include "Content.hpp"

#include <physfs.h>

#include <fstream>
#include <mutex>

namespace
{

bool g_ready = false;
std::filesystem::path g_root;
std::mutex g_mutex;
Catalog g_catalog;
bool g_catalogLoaded = false;

std::string utf8(const std::filesystem::path &path)
{
    const std::u8string bytes = path.u8string();
    return std::string(bytes.begin(), bytes.end());
}

} // namespace

bool contentInit(const std::filesystem::path &root)
{
    contentShutdown();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_root = root;
        g_catalog = {};
        g_catalogLoaded = false;
        if (PHYSFS_init("raytracer") != 0)
        {
            g_ready = true;
            const std::filesystem::path cooked = root / "cooked";
            PHYSFS_mount(utf8(cooked).c_str(), "cooked", 1);
            const std::filesystem::path pak = cooked / "pak0.zip";
            std::error_code ec;
            if (std::filesystem::exists(pak, ec))
                PHYSFS_mount(utf8(pak).c_str(), "cooked", 1);
        }
    }

    std::vector<std::uint8_t> bytes;
    std::string error;
    if (contentRead("cooked/catalog.bin", bytes, error))
    {
        Catalog loaded;
        if (loadCatalogBytes(bytes.data(), bytes.size(), loaded, error))
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_catalog = std::move(loaded);
            g_catalogLoaded = true;
        }
    }
    return true;
}

void contentShutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_ready)
        PHYSFS_deinit();
    g_ready = false;
    g_catalog = {};
    g_catalogLoaded = false;
}

bool contentReady()
{
    return g_ready;
}

bool contentExists(const std::string &virtualPath)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_ready && PHYSFS_exists(virtualPath.c_str()) != 0)
        return true;
    std::error_code ec;
    return std::filesystem::exists(g_root / virtualPath, ec);
}

bool contentRead(const std::string &virtualPath, std::vector<std::uint8_t> &bytes, std::string &error)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_ready && PHYSFS_exists(virtualPath.c_str()) != 0)
    {
        PHYSFS_File *file = PHYSFS_openRead(virtualPath.c_str());
        if (file == nullptr)
        {
            error = "physfs open failed";
            return false;
        }
        const PHYSFS_sint64 length = PHYSFS_fileLength(file);
        if (length < 0)
        {
            PHYSFS_close(file);
            error = "content length failed";
            return false;
        }
        bytes.resize(static_cast<std::size_t>(length));
        const PHYSFS_sint64 got = PHYSFS_readBytes(file, bytes.data(), static_cast<PHYSFS_uint64>(length));
        PHYSFS_close(file);
        if (got != length)
        {
            error = "content read short";
            return false;
        }
        return true;
    }
    std::ifstream in(g_root / virtualPath, std::ios::binary);
    if (!in)
    {
        error = "missing " + virtualPath;
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::string cookedMeshVirtualPath(const std::filesystem::path &sourcePath)
{
    std::filesystem::path relative = sourcePath.lexically_normal();
    if (relative.is_absolute())
    {
        // Fall back to the filename when the caller passes an absolute path.
        relative = relative.filename();
    }
    relative = relative.relative_path();
    relative.replace_extension(".rtm");
    std::string mirrored = relative.generic_string();
    while (!mirrored.empty() && (mirrored[0] == '/' || mirrored[0] == '\\'))
        mirrored.erase(mirrored.begin());
    if (mirrored.rfind("cooked/", 0) == 0)
        return mirrored;
    return "cooked/" + mirrored;
}

const Catalog &contentCatalog()
{
    return g_catalog;
}

bool contentCatalogLoaded()
{
    return g_catalogLoaded;
}
