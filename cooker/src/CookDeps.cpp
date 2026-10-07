#include "CookDeps.hpp"

#include "CookSidecar.hpp"
#include "Hash.hpp"

#include <fstream>

namespace
{

std::string utf8(const std::filesystem::path &path)
{
    const std::u8string bytes = path.u8string();
    return std::string(bytes.begin(), bytes.end());
}

} // namespace

void cookCollectObjDeps(const std::filesystem::path &obj, std::vector<std::filesystem::path> &deps)
{
    collectMeshSourceDeps(obj, deps);
}

bool cookWriteManifest(const std::filesystem::path &cookedDir, std::string &error)
{
    std::filesystem::create_directories(cookedDir);
    std::ofstream out(cookedDir / "manifest.txt", std::ios::trunc);
    if (!out)
    {
        error = "could not write manifest";
        return false;
    }
    out << "rt-manifest 1\n";
    std::error_code ec;
    if (!std::filesystem::exists(cookedDir, ec))
        return true;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(cookedDir, ec))
    {
        if (!entry.is_regular_file())
            continue;
        const auto ext = entry.path().extension();
        if (ext == ".d" || entry.path().filename() == "manifest.txt")
            continue;
        std::string hash;
        if (!sha256File(entry.path(), hash, error))
            return false;
        out << hash << " " << utf8(std::filesystem::relative(entry.path(), cookedDir, ec)) << "\n";
    }
    return true;
}
