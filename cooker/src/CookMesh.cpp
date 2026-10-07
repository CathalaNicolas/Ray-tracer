#include "CookMesh.hpp"
#include "CookDeps.hpp"

#include "MeshCooked.hpp"
#include "MeshGeometry.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <meshoptimizer.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <vector>

namespace
{

std::string pathUtf8(const std::filesystem::path &path)
{
    const std::u8string bytes = path.u8string();
    return std::string(bytes.begin(), bytes.end());
}

bool sourceIsMesh(const std::filesystem::path &path)
{
    std::string ext = path.extension().string();
    for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".dae";
}

struct Vertex
{
    float px = 0;
    float py = 0;
    float pz = 0;
    float nx = 0;
    float ny = 0;
    float nz = 0;
    float u = 0;
    float v = 0;
};

std::string aiTexturePath(const aiMaterial *material, aiTextureType type)
{
    if (material == nullptr)
        return {};
    aiString path;
    if (material->GetTexture(type, 0, &path) != AI_SUCCESS || path.length == 0)
        return {};
    return std::string(path.C_Str());
}

void collectMaterials(const aiScene *scene, std::vector<MeshMaterialRef> &materials)
{
    materials.clear();
    if (scene == nullptr)
        return;
    materials.reserve(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
    {
        const aiMaterial *source = scene->mMaterials[i];
        MeshMaterialRef material;
        aiString name;
        if (source != nullptr && source->Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
            material.name = name.C_Str();
        aiColor3D diffuse(0.8f, 0.8f, 0.8f);
        if (source != nullptr)
            source->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
        material.albedo = Vec3(diffuse.r, diffuse.g, diffuse.b);
        material.albedoMap = aiTexturePath(source, aiTextureType_DIFFUSE);
        material.normalMap = aiTexturePath(source, aiTextureType_NORMALS);
        if (material.normalMap.empty())
            material.normalMap = aiTexturePath(source, aiTextureType_HEIGHT);
        materials.push_back(std::move(material));
    }
}

void appendMesh(const aiMesh *mesh, std::vector<Vertex> &vertices, std::vector<unsigned int> &indices,
    std::vector<MeshSubmesh> &submeshes)
{
    if (mesh == nullptr || !mesh->HasPositions() || mesh->mNumFaces == 0)
        return;
    const unsigned int base = static_cast<unsigned int>(vertices.size());
    const std::uint32_t indexOffset = static_cast<std::uint32_t>(indices.size());
    vertices.reserve(vertices.size() + mesh->mNumVertices);
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
    {
        Vertex vertex;
        vertex.px = mesh->mVertices[i].x;
        vertex.py = mesh->mVertices[i].y;
        vertex.pz = mesh->mVertices[i].z;
        if (mesh->HasNormals())
        {
            vertex.nx = mesh->mNormals[i].x;
            vertex.ny = mesh->mNormals[i].y;
            vertex.nz = mesh->mNormals[i].z;
        }
        if (mesh->HasTextureCoords(0))
        {
            vertex.u = mesh->mTextureCoords[0][i].x;
            vertex.v = mesh->mTextureCoords[0][i].y;
        }
        vertices.push_back(vertex);
    }
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
    {
        const aiFace &face = mesh->mFaces[f];
        if (face.mNumIndices != 3)
            continue;
        indices.push_back(base + face.mIndices[0]);
        indices.push_back(base + face.mIndices[1]);
        indices.push_back(base + face.mIndices[2]);
    }
    const std::uint32_t indexCount = static_cast<std::uint32_t>(indices.size()) - indexOffset;
    if (indexCount == 0)
        return;
    MeshSubmesh submesh;
    submesh.materialIndex = mesh->mMaterialIndex;
    submesh.indexOffset = indexOffset;
    submesh.indexCount = indexCount;
    submeshes.push_back(submesh);
}

void walk(const aiScene *scene, const aiNode *node, std::vector<Vertex> &vertices, std::vector<unsigned int> &indices,
    std::vector<MeshSubmesh> &submeshes)
{
    if (node == nullptr)
        return;
    for (unsigned int i = 0; i < node->mNumMeshes; ++i)
        appendMesh(scene->mMeshes[node->mMeshes[i]], vertices, indices, submeshes);
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        walk(scene, node->mChildren[i], vertices, indices, submeshes);
}

bool importWithAssimp(const std::filesystem::path &source, std::vector<Vertex> &vertices,
    std::vector<unsigned int> &indices, std::vector<MeshMaterialRef> &materials, std::vector<MeshSubmesh> &submeshes,
    std::string &error)
{
    Assimp::Importer importer;
    const unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals
        | aiProcess_ImproveCacheLocality | aiProcess_PreTransformVertices;
    const aiScene *scene = importer.ReadFile(pathUtf8(source), flags);
    if (scene == nullptr || scene->mRootNode == nullptr)
    {
        error = importer.GetErrorString();
        if (error.empty())
            error = "Assimp failed to read " + pathUtf8(source);
        return false;
    }
    collectMaterials(scene, materials);
    walk(scene, scene->mRootNode, vertices, indices, submeshes);
    if (indices.empty() || vertices.empty())
    {
        error = "no triangles in " + pathUtf8(source);
        return false;
    }
    for (MeshSubmesh &submesh : submeshes)
    {
        if (materials.empty())
            submesh.materialIndex = 0;
        else if (submesh.materialIndex >= materials.size())
            submesh.materialIndex = 0;
    }
    return true;
}

void optimize(std::vector<Vertex> &vertices, std::vector<unsigned int> &indices, std::vector<MeshSubmesh> &submeshes)
{
    const size_t indexCount = indices.size();
    const size_t vertexCount = vertices.size();
    if (submeshes.empty())
    {
        meshopt_optimizeVertexCache(indices.data(), indices.data(), indexCount, vertexCount);
        meshopt_optimizeOverdraw(indices.data(), indices.data(), indexCount, &vertices[0].px, vertexCount, sizeof(Vertex),
            1.05f);
    }
    else
    {
        for (const MeshSubmesh &submesh : submeshes)
        {
            if (submesh.indexCount < 3)
                continue;
            unsigned int *begin = indices.data() + submesh.indexOffset;
            meshopt_optimizeVertexCache(begin, begin, submesh.indexCount, vertexCount);
            meshopt_optimizeOverdraw(begin, begin, submesh.indexCount, &vertices[0].px, vertexCount, sizeof(Vertex),
                1.05f);
        }
    }
    std::vector<Vertex> reordered(vertexCount);
    const size_t newCount =
        meshopt_optimizeVertexFetch(reordered.data(), indices.data(), indexCount, vertices.data(), vertexCount, sizeof(Vertex));
    reordered.resize(newCount);
    vertices.swap(reordered);
}

MeshGeometry toGeometry(const std::vector<Vertex> &vertices, const std::vector<unsigned int> &indices,
    std::vector<MeshMaterialRef> materials, std::vector<MeshSubmesh> submeshes)
{
    MeshGeometry geometry;
    geometry.positions.resize(vertices.size());
    geometry.normals.resize(vertices.size());
    geometry.u.resize(vertices.size());
    geometry.v.resize(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        const Vertex &vertex = vertices[i];
        geometry.positions[i] = Vec3(vertex.px, vertex.py, vertex.pz);
        geometry.normals[i] = Vec3(vertex.nx, vertex.ny, vertex.nz);
        geometry.u[i] = vertex.u;
        geometry.v[i] = vertex.v;
    }
    geometry.indices.assign(indices.begin(), indices.end());
    geometry.materials = std::move(materials);
    geometry.submeshes = std::move(submeshes);
    finishGeometry(geometry);
    return geometry;
}

} // namespace

bool cookSourceToRtm(const std::filesystem::path &source, const std::filesystem::path &output, std::string &error,
    bool ifNewer)
{
    (void)ifNewer;
    std::error_code ec;
    if (!std::filesystem::exists(source, ec))
    {
        error = "missing source " + pathUtf8(source);
        return false;
    }
    std::vector<std::filesystem::path> deps;
    cookCollectObjDeps(source, deps);
    if (!cookNeedsUpdate(output, source, deps))
        return true;
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<MeshMaterialRef> materials;
    std::vector<MeshSubmesh> submeshes;
    if (!importWithAssimp(source, vertices, indices, materials, submeshes, error))
        return false;
    optimize(vertices, indices, submeshes);
    MeshGeometry geometry = toGeometry(vertices, indices, std::move(materials), std::move(submeshes));
    const size_t indexCount = indices.size();
    if (indexCount >= 12)
    {
        const float *positions = &vertices[0].px;
        for (const float ratio : {0.5f, 0.25f})
        {
            const size_t target = std::max<size_t>(6, static_cast<size_t>(static_cast<float>(indexCount) * ratio) / 3 * 3);
            std::vector<unsigned int> lod(indexCount);
            const size_t simplified = meshopt_simplify(lod.data(), indices.data(), indexCount, positions, vertices.size(),
                sizeof(Vertex), target, 0.15f);
            if (simplified < 6 || simplified >= indexCount)
                continue;
            lod.resize(simplified);
            meshopt_optimizeVertexCache(lod.data(), lod.data(), lod.size(), vertices.size());
            MeshLod level;
            level.indices.assign(lod.begin(), lod.end());
            geometry.extraLods.push_back(std::move(level));
        }
    }
    if (!writeRtmFile(output, geometry, error))
        return false;
    return cookWriteSidecar(output, source, deps, error);
}

int cookDirectoryToRtm(const std::filesystem::path &sourceDir, const std::filesystem::path &outputDir, bool ifNewer,
    std::string &error)
{
    int cooked = 0;
    std::error_code ec;
    if (!std::filesystem::exists(sourceDir, ec))
    {
        error = "missing directory " + pathUtf8(sourceDir);
        return -1;
    }
    for (const auto &entry : std::filesystem::recursive_directory_iterator(sourceDir, ec))
    {
        if (!entry.is_regular_file() || !sourceIsMesh(entry.path()))
            continue;
        const std::filesystem::path relative = std::filesystem::relative(entry.path(), sourceDir, ec);
        std::filesystem::path out = outputDir / relative;
        out.replace_extension(".rtm");
        if (!cookSourceToRtm(entry.path(), out, error, ifNewer))
            return -1;
        ++cooked;
    }
    return cooked;
}
