#include "MeshObj.hpp"

#include "Jobs.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace
{

std::string pathUtf8(const std::filesystem::path &path)
{
    const std::u8string bytes = path.u8string();
    return std::string(bytes.begin(), bytes.end());
}

Vec3 min3(const Vec3 &a, const Vec3 &b)
{
    return Vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
}

Vec3 max3(const Vec3 &a, const Vec3 &b)
{
    return Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
}

struct FaceIndex
{
    int position = -1;
    int texcoord = -1;
    int normal = -1;
};

int resolveIndex(int index, int count)
{
    if (index > 0)
        return index - 1;
    if (index < 0)
        return count + index;
    return -1;
}

FaceIndex parseFaceVertex(const std::string &token)
{
    FaceIndex index;
    std::stringstream stream(token);
    std::string part;
    int field = 0;
    while (std::getline(stream, part, '/'))
    {
        if (!part.empty())
        {
            const int value = std::stoi(part);
            if (field == 0)
                index.position = value;
            else if (field == 1)
                index.texcoord = value;
            else
                index.normal = value;
        }
        ++field;
    }
    return index;
}

std::string geometryKey(const std::filesystem::path &path)
{
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    std::string key = pathUtf8(error ? path : canonical);
    const auto stamp = std::filesystem::last_write_time(path, error);
    if (!error)
    {
        key.push_back('|');
        key += std::to_string(stamp.time_since_epoch().count());
    }
    return key;
}

std::unordered_map<std::string, std::shared_ptr<MeshGeometry>> &geometryCache()
{
    static std::unordered_map<std::string, std::shared_ptr<MeshGeometry>> cache;
    return cache;
}

} // namespace

namespace mesh_obj
{

void fitToGround(std::vector<MeshTri> &triangles)
{
    if (triangles.empty())
        return;
    Vec3 boundsMin = triangles[0].position[0];
    Vec3 boundsMax = boundsMin;
    for (const MeshTri &triangle : triangles)
    {
        for (int corner = 0; corner < 3; ++corner)
        {
            boundsMin = min3(boundsMin, triangle.position[corner]);
            boundsMax = max3(boundsMax, triangle.position[corner]);
        }
    }
    const Vec3 anchor((boundsMin.x + boundsMax.x) * 0.5, boundsMin.y, (boundsMin.z + boundsMax.z) * 0.5);
    const double extent = std::max(boundsMax.x - boundsMin.x, std::max(boundsMax.y - boundsMin.y, boundsMax.z - boundsMin.z));
    const double scale = extent > 1e-8 ? 1.5 / extent : 1.0;
    for (MeshTri &triangle : triangles)
    {
        for (int corner = 0; corner < 3; ++corner)
        {
            triangle.position[corner] = (triangle.position[corner] - anchor) * scale;
            triangle.normal[corner] = normalize(triangle.normal[corner]);
        }
    }
}

bool loadTriangles(const std::filesystem::path &path, std::vector<MeshTri> &triangles, std::string &error)
{
    std::ifstream in(path);
    if (!in)
    {
        error = "Could not open " + path.string();
        return false;
    }

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::pair<float, float>> texcoords;
    std::string line;
    int lineNumber = 0;
    try
    {
        while (std::getline(in, line))
        {
            ++lineNumber;
            std::istringstream stream(line);
            std::string tag;
            if (!(stream >> tag))
                continue;
            if (tag == "v")
            {
                double x = 0, y = 0, z = 0;
                if (!(stream >> x >> y >> z))
                {
                    error = path.string() + ":" + std::to_string(lineNumber) + ": vertex needs 3 numbers";
                    return false;
                }
                positions.emplace_back(x, y, z);
            }
            else if (tag == "vt")
            {
                float u = 0, v = 0;
                if (!(stream >> u >> v))
                {
                    error = path.string() + ":" + std::to_string(lineNumber) + ": texcoord needs 2 numbers";
                    return false;
                }
                texcoords.emplace_back(u, v);
            }
            else if (tag == "vn")
            {
                double x = 0, y = 0, z = 0;
                if (!(stream >> x >> y >> z))
                {
                    error = path.string() + ":" + std::to_string(lineNumber) + ": normal needs 3 numbers";
                    return false;
                }
                normals.emplace_back(x, y, z);
            }
            else if (tag == "f")
            {
                std::vector<FaceIndex> face;
                std::string token;
                while (stream >> token)
                    face.push_back(parseFaceVertex(token));
                if (face.size() < 3)
                    continue;
                for (size_t corner = 1; corner + 1 < face.size(); ++corner)
                {
                    const FaceIndex verts[3] = {face[0], face[corner], face[corner + 1]};
                    MeshTri triangle;
                    bool valid = true;
                    for (int index = 0; index < 3; ++index)
                    {
                        const int positionIndex = resolveIndex(verts[index].position, static_cast<int>(positions.size()));
                        if (positionIndex < 0 || positionIndex >= static_cast<int>(positions.size()))
                        {
                            valid = false;
                            break;
                        }
                        triangle.position[index] = positions[static_cast<size_t>(positionIndex)];
                        const int texIndex = resolveIndex(verts[index].texcoord, static_cast<int>(texcoords.size()));
                        if (texIndex >= 0 && texIndex < static_cast<int>(texcoords.size()))
                        {
                            triangle.u[index] = texcoords[static_cast<size_t>(texIndex)].first;
                            triangle.v[index] = texcoords[static_cast<size_t>(texIndex)].second;
                        }
                        const int normalIndex = resolveIndex(verts[index].normal, static_cast<int>(normals.size()));
                        if (normalIndex >= 0 && normalIndex < static_cast<int>(normals.size()))
                            triangle.normal[index] = normals[static_cast<size_t>(normalIndex)];
                    }
                    if (!valid)
                        continue;
                    const Vec3 faceNormal = cross(triangle.position[1] - triangle.position[0], triangle.position[2] - triangle.position[0]);
                    if (length(triangle.normal[0]) <= 1e-8)
                    {
                        const Vec3 unit = length(faceNormal) > 1e-8 ? normalize(faceNormal) : Vec3(0, 1, 0);
                        triangle.normal[0] = triangle.normal[1] = triangle.normal[2] = unit;
                    }
                    triangles.push_back(triangle);
                }
            }
        }
    }
    catch (const std::exception &exception)
    {
        error = path.string() + ":" + std::to_string(lineNumber) + ": " + exception.what();
        return false;
    }

    if (triangles.empty())
    {
        error = path.string() + " contains no triangles";
        return false;
    }
    return true;
}

std::shared_ptr<MeshGeometry> load(const std::filesystem::path &path, std::string &error)
{
    const std::string key = geometryKey(path);
    auto &cache = geometryCache();
    {
        const auto found = cache.find(key);
        if (found != cache.end())
            return found->second;
    }

    std::vector<MeshTri> triangles;
    bool loaded = false;
    if (jobs::ready())
    {
        jobs::runPinned([&]() { loaded = loadTriangles(path, triangles, error); });
    }
    else
        loaded = loadTriangles(path, triangles, error);
    if (!loaded)
        return nullptr;

    fitToGround(triangles);
    auto geometry = std::make_shared<MeshGeometry>();
    geometry->triangles = std::move(triangles);
    geometry->sourcePath = pathUtf8(path);
    finishGeometry(*geometry);
    cache.emplace(key, geometry);
    return geometry;
}

} // namespace mesh_obj
