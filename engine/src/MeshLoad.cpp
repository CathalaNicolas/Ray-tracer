#include "MeshLoad.hpp"

#include "Content.hpp"
#include "CookSidecar.hpp"
#include "MeshCooked.hpp"
#include "MeshObj.hpp"

#include <cctype>
#include <filesystem>

namespace
{

MeshFileLoader g_loader = nullptr;
#if defined(RAYTRACER_SHIPPING)
bool g_allowSource = false;
#else
bool g_allowSource = true;
#endif

bool isSourceMeshPath(const std::filesystem::path &path)
{
    std::string ext = path.extension().string();
    for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae";
}

} // namespace

void setMeshFileLoader(MeshFileLoader loader)
{
    g_loader = loader;
}

void setAllowSourceMeshes(bool allow)
{
    g_allowSource = allow;
}

bool allowSourceMeshes()
{
    return g_allowSource;
}

std::shared_ptr<MeshGeometry> loadMeshFile(const std::filesystem::path &path, std::string &error)
{
    if (isRtmPath(path))
        return loadRtmFile(path, error);

    const std::string cooked = cookedMeshVirtualPath(path);
    if (contentExists(cooked))
    {
        std::vector<std::filesystem::path> deps;
        collectMeshSourceDeps(path, deps);
        if (cookedSidecarMatches(cooked, path, deps))
            return loadRtmFile(cooked, error);
        // Stale or missing sidecar: ignore the cooked file in editor / test builds.
        if (!g_allowSource)
        {
            error = "stale or unverified cooked mesh " + cooked + " (sidecar hash mismatch)";
            return nullptr;
        }
    }

    if (!g_allowSource)
    {
        if (isSourceMeshPath(path))
        {
            error = "source mesh formats are disabled; cook to " + cooked;
            return nullptr;
        }
        error = "missing cooked mesh " + cooked;
        return nullptr;
    }

    if (g_loader != nullptr)
        return g_loader(path, error);
    return mesh_obj::load(path, error);
}

bool refreshMeshFile(std::shared_ptr<MeshGeometry> &geometry, std::string &sourcePath, std::int64_t &sourceStamp,
    std::uintmax_t &sourceBytes, std::string &error)
{
    if (sourcePath.empty())
        return false;
    const std::filesystem::path path = std::filesystem::u8path(sourcePath);
    std::error_code stampError;
    const auto stamp = std::filesystem::last_write_time(path, stampError);
    const auto bytes = std::filesystem::file_size(path, stampError);
    if (stampError)
        return false;
    const auto ticks = static_cast<std::int64_t>(stamp.time_since_epoch().count());
    if (ticks == sourceStamp && bytes == sourceBytes)
        return false;
    auto loaded = loadMeshFile(path, error);
    if (!loaded)
        return false;
    geometry = std::move(loaded);
    sourcePath = geometry->sourcePath;
    sourceStamp = ticks;
    sourceBytes = bytes;
    return true;
}
