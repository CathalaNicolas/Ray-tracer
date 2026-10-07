#include "CookSidecar.hpp"

#include "Content.hpp"
#include "Hash.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace
{

std::string utf8(const std::filesystem::path &path)
{
    const std::u8string bytes = path.u8string();
    return std::string(bytes.begin(), bytes.end());
}

bool parseSidecarLines(const std::string &text, const std::filesystem::path &source,
    const std::vector<std::filesystem::path> &deps)
{
    std::istringstream in(text);
    std::string expected;
    std::string error;
    if (!sha256File(source, expected, error))
        return false;
    std::string line;
    if (!std::getline(in, line))
        return false;
    if (line != "src " + expected + " " + utf8(source))
        return false;
    std::vector<std::filesystem::path> remaining = deps;
    while (std::getline(in, line))
    {
        if (line.rfind("dep ", 0) != 0)
            return false;
        std::istringstream parse(line.substr(4));
        std::string hash;
        parse >> hash;
        std::string pathStr;
        std::getline(parse, pathStr);
        if (!pathStr.empty() && pathStr[0] == ' ')
            pathStr.erase(0, 1);
        std::string actual;
        if (!sha256File(pathStr, actual, error) || actual != hash)
            return false;
        for (auto it = remaining.begin(); it != remaining.end(); ++it)
        {
            if (utf8(*it) == pathStr)
            {
                remaining.erase(it);
                break;
            }
        }
    }
    return remaining.empty();
}

} // namespace

std::filesystem::path cookSidecarPath(const std::filesystem::path &cookedOutput)
{
    return std::filesystem::path(cookedOutput.string() + ".d");
}

bool cookNeedsUpdate(const std::filesystem::path &output, const std::filesystem::path &source,
    const std::vector<std::filesystem::path> &deps)
{
    std::error_code ec;
    if (!std::filesystem::exists(output, ec))
        return true;
    const auto side = cookSidecarPath(output);
    std::ifstream in(side);
    if (!in)
        return true;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return !parseSidecarLines(buffer.str(), source, deps);
}

bool cookWriteSidecar(const std::filesystem::path &output, const std::filesystem::path &source,
    const std::vector<std::filesystem::path> &deps, std::string &error)
{
    std::string hash;
    if (!sha256File(source, hash, error))
        return false;
    std::ofstream out(cookSidecarPath(output), std::ios::trunc);
    if (!out)
    {
        error = "could not write sidecar";
        return false;
    }
    out << "src " << hash << " " << utf8(source) << "\n";
    for (const auto &dep : deps)
    {
        if (!sha256File(dep, hash, error))
            return false;
        out << "dep " << hash << " " << utf8(dep) << "\n";
    }
    return true;
}

bool cookedSidecarMatches(const std::string &cookedVirtual, const std::filesystem::path &source,
    const std::vector<std::filesystem::path> &deps)
{
    std::vector<std::uint8_t> bytes;
    std::string error;
    const std::string sideVirtual = cookedVirtual + ".d";
    if (contentExists(sideVirtual))
    {
        if (!contentRead(sideVirtual, bytes, error))
            return false;
    }
    else
    {
        std::ifstream in(cookSidecarPath(cookedVirtual));
        if (!in)
            return false;
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    return parseSidecarLines(std::string(bytes.begin(), bytes.end()), source, deps);
}

void collectMeshSourceDeps(const std::filesystem::path &source, std::vector<std::filesystem::path> &deps)
{
    std::string ext = source.extension().string();
    for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext != ".obj")
        return;
    std::filesystem::path mtl = source;
    mtl.replace_extension(".mtl");
    std::error_code ec;
    if (!std::filesystem::exists(mtl, ec))
        return;
    deps.push_back(mtl);
    std::ifstream in(mtl);
    std::string word;
    while (in >> word)
    {
        if (word == "map_Kd" || word == "map_Ks" || word == "map_Bump" || word == "norm" || word == "map_d")
        {
            std::string file;
            in >> file;
            const auto tex = source.parent_path() / file;
            if (std::filesystem::exists(tex, ec))
                deps.push_back(tex);
        }
    }
}
