#include "MeshImport.hpp"

#include "MeshObj.hpp"

#include <fbxsdk.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace
{

// C++20 path::u8string() returns std::u8string; scene paths stay UTF-8 in std::string.
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

void appendFbxMesh(FbxNode *node, std::vector<MeshTri> &triangles)
{
    FbxMesh *mesh = node->GetMesh();
    if (mesh != nullptr)
    {
        const FbxAMatrix global = node->EvaluateGlobalTransform();
        const FbxAMatrix normalMatrix = global.Inverse().Transpose();
        const bool hasUv = mesh->GetElementUVCount() > 0;
        const FbxString uvName = hasUv ? FbxString(mesh->GetElementUV(0)->GetName()) : FbxString();
        const int polygonCount = mesh->GetPolygonCount();
        for (int polygon = 0; polygon < polygonCount; ++polygon)
        {
            const int size = mesh->GetPolygonSize(polygon);
            if (size < 3)
                continue;
            for (int start = 1; start + 1 < size; ++start)
            {
                const int corners[3] = {0, start, start + 1};
                MeshTri triangle;
                for (int corner = 0; corner < 3; ++corner)
                {
                    const int polygonCorner = corners[corner];
                    const int index = mesh->GetPolygonVertex(polygon, polygonCorner);
                    const FbxVector4 point = global.MultT(mesh->GetControlPointAt(index));
                    triangle.position[corner] = Vec3(point[0], point[1], point[2]);
                    FbxVector4 normal(0, 1, 0, 0);
                    if (!mesh->GetPolygonVertexNormal(polygon, polygonCorner, normal))
                    {
                        const Vec3 edge1 = triangle.position[1] - triangle.position[0];
                        const Vec3 edge2 = triangle.position[2] - triangle.position[0];
                        const Vec3 face = cross(edge1, edge2);
                        normal = FbxVector4(face.x, face.y, face.z, 0);
                    }
                    const FbxVector4 turned = normalMatrix.MultT(FbxVector4(normal[0], normal[1], normal[2], 0));
                    triangle.normal[corner] = Vec3(turned[0], turned[1], turned[2]);
                    if (hasUv)
                    {
                        FbxVector2 uv;
                        bool unmapped = false;
                        if (mesh->GetPolygonVertexUV(polygon, polygonCorner, uvName.Buffer(), uv, unmapped))
                        {
                            triangle.u[corner] = static_cast<float>(uv[0]);
                            triangle.v[corner] = static_cast<float>(uv[1]);
                        }
                    }
                }
                triangles.push_back(triangle);
            }
        }
    }
    for (int child = 0; child < node->GetChildCount(); ++child)
        appendFbxMesh(node->GetChild(child), triangles);
}

bool loadFbx(const std::filesystem::path &path, std::vector<MeshTri> &triangles, std::string &error)
{
    FbxManager *manager = FbxManager::Create();
    if (manager == nullptr)
    {
        error = "Could not start the FBX SDK";
        return false;
    }
    FbxIOSettings *settings = FbxIOSettings::Create(manager, IOSROOT);
    manager->SetIOSettings(settings);
    FbxImporter *importer = FbxImporter::Create(manager, "");
    const std::string filename = path.string();
    if (!importer->Initialize(filename.c_str(), -1, manager->GetIOSettings()))
    {
        error = filename + ": " + importer->GetStatus().GetErrorString();
        manager->Destroy();
        return false;
    }
    FbxScene *scene = FbxScene::Create(manager, "");
    const bool imported = importer->Import(scene);
    const std::string status = importer->GetStatus().GetErrorString();
    importer->Destroy();
    if (!imported)
    {
        error = filename + ": " + (status.empty() ? std::string("could not be read") : status);
        manager->Destroy();
        return false;
    }
    FbxAxisSystem::OpenGL.DeepConvertScene(scene);
    FbxSystemUnit::m.ConvertScene(scene);
    FbxGeometryConverter converter(manager);
    converter.Triangulate(scene, true);
    if (scene->GetRootNode() != nullptr)
        appendFbxMesh(scene->GetRootNode(), triangles);
    manager->Destroy();
    if (triangles.empty())
    {
        error = filename + " contains no triangles";
        return false;
    }
    return true;
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

void eraseOlderGeometry(std::unordered_map<std::string, std::shared_ptr<MeshGeometry>> &cache, const std::string &key)
{
    const auto split = key.rfind('|');
    if (split == std::string::npos)
        return;
    const std::string prefix = key.substr(0, split + 1);
    for (auto entry = cache.begin(); entry != cache.end();)
    {
        if (entry->first != key && entry->first.compare(0, prefix.size(), prefix) == 0)
            entry = cache.erase(entry);
        else
            ++entry;
    }
}

void assignSourceStamp(const std::filesystem::path &path, std::int64_t &sourceStamp, std::uintmax_t &sourceBytes)
{
    std::error_code stampError;
    const auto stamp = std::filesystem::last_write_time(path, stampError);
    sourceStamp = stampError ? 0 : static_cast<std::int64_t>(stamp.time_since_epoch().count());
    sourceBytes = stampError ? 0 : std::filesystem::file_size(path, stampError);
}

} // namespace

namespace mesh_import
{

std::shared_ptr<MeshGeometry> load(const std::filesystem::path &path, std::string &error)
{
    const std::string key = geometryKey(path);
    auto &cache = geometryCache();
    {
        const auto found = cache.find(key);
        if (found != cache.end())
            return found->second;
    }

    std::string extension = path.extension().string();
    for (char &character : extension)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

    if (extension != ".fbx")
        return mesh_obj::load(path, error);

    std::vector<MeshTri> triangles;
    if (!loadFbx(path, triangles, error))
        return nullptr;

    mesh_obj::fitToGround(triangles);
    auto geometry = std::make_shared<MeshGeometry>();
    geometry->triangles = std::move(triangles);
    geometry->sourcePath = pathUtf8(path);
    finishGeometry(*geometry);
    cache.emplace(key, geometry);
    eraseOlderGeometry(cache, key);
    return geometry;
}

bool refresh(std::shared_ptr<MeshGeometry> &geometry, std::string &sourcePath, std::int64_t &sourceStamp, std::uintmax_t &sourceBytes, std::string &error)
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
    auto loaded = load(path, error);
    if (!loaded)
        return false;
    geometry = std::move(loaded);
    sourcePath = geometry->sourcePath;
    sourceStamp = ticks;
    sourceBytes = bytes;
    return true;
}

bool writeTestTriangleFbx(const std::filesystem::path &path)
{
    FbxManager *manager = FbxManager::Create();
    if (manager == nullptr)
        return false;
    FbxIOSettings *settings = FbxIOSettings::Create(manager, IOSROOT);
    manager->SetIOSettings(settings);
    FbxScene *scene = FbxScene::Create(manager, "test");
    FbxMesh *shape = FbxMesh::Create(scene, "Triangle");
    shape->InitControlPoints(3);
    shape->SetControlPointAt(FbxVector4(0, 0, 0), 0);
    shape->SetControlPointAt(FbxVector4(1, 0, 0), 1);
    shape->SetControlPointAt(FbxVector4(0, 1, 0), 2);
    shape->BeginPolygon();
    shape->AddPolygon(0);
    shape->AddPolygon(1);
    shape->AddPolygon(2);
    shape->EndPolygon();
    FbxNode *node = FbxNode::Create(scene, "Triangle");
    node->SetNodeAttribute(shape);
    scene->GetRootNode()->AddChild(node);
    FbxExporter *exporter = FbxExporter::Create(manager, "");
    const bool opened = exporter->Initialize(path.string().c_str(), -1, manager->GetIOSettings());
    const bool exported = opened && exporter->Export(scene);
    exporter->Destroy();
    manager->Destroy();
    return exported;
}

} // namespace mesh_import
