#include "GpuRayTracer.hpp"

#include "EngineSettings.hpp"
#include "ImageIO.hpp"
#include "Mesh.hpp"
#include "Plane.hpp"
#include "Sphere.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <unordered_map>

#if defined(_WIN32)

#include "GpuGl.hpp"
#include "GpuRenderDetail.hpp"
#include "GpuShaders.hpp"

#include <cstddef>

namespace gpu_detail
{

void pushVec3(std::vector<float> &values, const Vec3 &v)
{
    values.push_back(static_cast<float>(v.x));
    values.push_back(static_cast<float>(v.y));
    values.push_back(static_cast<float>(v.z));
    values.push_back(0.0f);
}

unsigned short floatToHalf(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const int exponent = static_cast<int>((bits >> 23) & 0xffu) - 127 + 15;
    const std::uint32_t mantissa = bits & 0x7fffffu;
    if (exponent <= 0)
        return static_cast<unsigned short>(sign);
    if (exponent >= 31)
        return static_cast<unsigned short>(sign | 0x7c00u);
    return static_cast<unsigned short>(sign | (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13));
}

std::uint64_t mixBits(std::uint64_t hash, std::uint64_t bits)
{
    hash ^= bits + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

std::uint64_t hashTexels(const std::vector<float> &texels)
{
    std::uint64_t hash = 0x9e3779b97f4a7c15ull;
    for (float value : texels)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        hash = mixBits(hash, bits);
    }
    return hash;
}

void uploadDataTexture(unsigned &texture, const std::vector<float> &texels, std::vector<float> &cached, int &cachedHeight, std::uint64_t &cachedHash, std::size_t &cachedCount)
{
    if (texture == 0)
        glGenTextures(1, &texture);
    const int count = std::max(1, static_cast<int>(texels.size() / 4));
    const int height = std::max(1, (count + kDataWidth - 1) / kDataWidth);
    const std::size_t texelCount = texels.size();
    const std::uint64_t hash = hashTexels(texels);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (cachedHeight == height && cachedCount == texelCount && cachedHash == hash)
        return;
    std::vector<float> padded(static_cast<size_t>(kDataWidth * height * 4), 0.0f);
    if (!texels.empty())
        std::copy(texels.begin(), texels.end(), padded.begin());
    if (cachedHeight == height)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kDataWidth, height, GL_RGBA, GL_FLOAT, padded.data());
    else
    {
        configureTexture(GL_TEXTURE_2D, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kDataWidth, height, 0, GL_RGBA, GL_FLOAT, padded.data());
    }
    cached = texels;
    cachedHeight = height;
    cachedHash = hash;
    cachedCount = texelCount;
}

struct DiskStat
{
    std::filesystem::file_time_type stamp{};
    std::uintmax_t bytes = 0;
    bool failed = false;
};

std::unordered_map<std::string, DiskStat> &imageStatMemo()
{
    static std::unordered_map<std::string, DiskStat> memo;
    return memo;
}

void beginImageStats()
{
    imageStatMemo().clear();
}

bool &shadowReuseFlag()
{
    static bool reuse = false;
    return reuse;
}

void setReuseShadowMaps(bool reuse)
{
    shadowReuseFlag() = reuse;
}

bool reuseShadowMaps()
{
    return shadowReuseFlag();
}

DiskStat statOnce(const std::string &path)
{
    std::unordered_map<std::string, DiskStat> &memo = imageStatMemo();
    const auto found = memo.find(path);
    if (found != memo.end())
        return found->second;
    DiskStat stat;
    const std::filesystem::path file = std::filesystem::u8path(path);
    std::error_code error;
    stat.stamp = std::filesystem::last_write_time(file, error);
    stat.bytes = std::filesystem::file_size(file, error);
    stat.failed = static_cast<bool>(error);
    memo.emplace(path, stat);
    return stat;
}

void keepRequestedImage(std::unordered_map<std::string, ImageCache> &cache, const std::string &path)
{
    if (cache.size() <= 64)
        return;
    for (auto it = cache.begin(); it != cache.end();)
    {
        if (it->first != path)
            it = cache.erase(it);
        else
            ++it;
    }
}

ImageCache &cachedImage(const std::string &path)
{
    static std::unordered_map<std::string, ImageCache> cache;
    static ImageCache absent;
    const DiskStat stat = statOnce(path);
    const auto stamp = stat.stamp;
    const auto bytes = stat.bytes;
    const bool error = stat.failed;
    const std::filesystem::path file = std::filesystem::u8path(path);
    auto found = cache.find(path);
    if (error)
    {
        if (found == cache.end() || !found->second.missing)
            std::cerr << "Could not open " << path << "\n";
        if (found != cache.end())
            cache.erase(found);
        absent = ImageCache{};
        absent.missing = true;
        return absent;
    }
    if (found != cache.end() && !found->second.missing && found->second.image.width > 0 && found->second.stamp == stamp && found->second.bytes == bytes)
    {
        keepRequestedImage(cache, path);
        return cache.find(path)->second;
    }
    ImageCache &entry = cache[path];
    entry.stamp = stamp;
    entry.bytes = bytes;
    std::string loadError;
    entry.missing = !loadImage(file, entry.image, loadError);
    if (entry.missing)
        std::cerr << loadError << "\n";
    keepRequestedImage(cache, path);
    return cache.find(path)->second;
}

std::vector<unsigned char> scaledAlbedo(const LoadedImage &image)
{
    std::vector<unsigned char> source = image.rgba8;
    int width = image.width;
    int height = image.height;
    if (image.hdr)
    {
        width = image.width;
        height = image.height;
        source.resize(static_cast<size_t>(width * height * 4));
        for (size_t index = 0; index < source.size(); ++index)
        {
            const float value = image.rgba32[index];
            const float mapped = value / (1.0f + std::max(value, 0.0f));
            source[index] = static_cast<unsigned char>(std::clamp(mapped, 0.0f, 1.0f) * 255.0f);
        }
    }
    std::vector<unsigned char> dest(static_cast<size_t>(kAlbedoEdge * kAlbedoEdge * 4), 255);
    if (width <= 0 || height <= 0 || source.empty())
        return dest;
    for (int y = 0; y < kAlbedoEdge; ++y)
    {
        int sourceY = height - 1 - (y * height / kAlbedoEdge);
        sourceY = std::clamp(sourceY, 0, height - 1);
        for (int x = 0; x < kAlbedoEdge; ++x)
        {
            int sourceX = std::clamp(x * width / kAlbedoEdge, 0, width - 1);
            const size_t from = (static_cast<size_t>(sourceY) * static_cast<size_t>(width) + static_cast<size_t>(sourceX)) * 4;
            const size_t to = (static_cast<size_t>(y) * kAlbedoEdge + static_cast<size_t>(x)) * 4;
            std::copy_n(source.data() + from, 4, dest.data() + to);
        }
    }
    return dest;
}

void pushOpt(std::vector<float> &values, const Material &material, int layer)
{
    values.push_back(static_cast<float>(material.transmission));
    values.push_back(static_cast<float>(material.ior));
    values.push_back(static_cast<float>(layer));
    values.push_back(static_cast<float>(material.uvScale > 0 ? material.uvScale : 1));
}

float shadeExponent(const Material &material)
{
    if (material.roughness < 0)
        return static_cast<float>(material.shininess);
    const double roughness = std::clamp(material.roughness, 0.04, 1.0);
    return static_cast<float>(std::clamp(2.0 / (roughness * roughness) - 2.0, 1.0, 256.0));
}

void mixDouble(std::uint64_t &hash, double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    hash = mixBits(hash, bits);
}

void mixVec(std::uint64_t &hash, const Vec3 &value)
{
    mixDouble(hash, value.x);
    mixDouble(hash, value.y);
    mixDouble(hash, value.z);
}

void mixText(std::uint64_t &hash, const std::string &text)
{
    hash = mixBits(hash, text.size());
    for (unsigned char character : text)
        hash = mixBits(hash, character);
}

void mixMaterial(std::uint64_t &hash, const Material &material)
{
    mixVec(hash, material.albedo);
    mixDouble(hash, material.ambient);
    mixDouble(hash, material.diffuse);
    mixDouble(hash, material.specular);
    mixDouble(hash, material.shininess);
    mixDouble(hash, material.reflectivity);
    mixDouble(hash, material.transmission);
    mixDouble(hash, material.ior);
    mixDouble(hash, material.uvScale);
    mixDouble(hash, material.uvScrollU);
    mixDouble(hash, material.uvScrollV);
    mixDouble(hash, material.roughness);
    mixDouble(hash, material.emission);
    mixText(hash, material.albedoMap);
    mixText(hash, material.normalMap);
}

std::uint64_t sceneFingerprint(const Scene &scene)
{
    std::uint64_t hash = 0x6a09e667f3bcc909ull;
    hash = mixBits(hash, scene.objects().size());
    for (const auto &object : scene.objects())
    {
        hash = mixBits(hash, static_cast<std::uint64_t>(object->id));
        hash = mixBits(hash, static_cast<std::uint64_t>(object->parentId));
        const Material material = object->material();
        mixMaterial(hash, material);
        object->mixShapeHash(hash);
    }
    hash = mixBits(hash, scene.lights().size());
    for (const PointLight &light : scene.lights())
    {
        mixVec(hash, light.position);
        mixVec(hash, light.color);
        mixDouble(hash, light.intensity);
        mixDouble(hash, light.falloff);
        mixDouble(hash, light.radius);
        hash = mixBits(hash, light.directional ? 1 : 0);
        mixVec(hash, light.spotDirection);
        mixDouble(hash, light.spotOuter);
        mixDouble(hash, light.spotInner);
    }
    return hash;
}


} // namespace gpu_detail

using namespace gpu_detail;

int GpuRayTracer::render(
    const Scene &scene,
    const Camera &camera,
    unsigned texture,
    int width,
    int height,
    int sampleGrid,
    int maxDepth,
    int selectedObject,
    bool linearOutput,
    int sampleIndex,
    double timeSeconds,
    bool mirrorBounces)
{
    if (!ready_ || width <= 0 || height <= 0 || texture == 0)
        return -1;

    beginImageStats();

    const int grid = sampleGrid < 1 ? 1 : (sampleGrid > 4 ? 4 : sampleGrid);
    if (sampleIndex < 0 && grid > 1)
    {
        struct MeshProbe : gpu_detail::GpuContribute
        {
            bool hasMesh = false;
            void sphere(const Hittable &, const Vec3 &, double) override {}
            void plane(const Hittable &, const Vec3 &, const Vec3 &, bool, const Vec3 &, double) override {}
            void mesh(const Mesh &object) override
            {
                if (!object.triangles().empty())
                    hasMesh = true;
            }
        } probe;
        for (const auto &object : scene.objects())
        {
            object->contributeGpu(probe);
            if (probe.hasMesh)
                break;
        }
        if (probe.hasMesh)
        {
            int totalMs = 0;
            for (int index = 0; index < grid * grid; ++index)
            {
                const int ms = render(scene, camera, texture, width, height, grid, maxDepth, selectedObject, linearOutput, index, timeSeconds, mirrorBounces);
                if (ms < 0)
                    return ms;
                totalMs += ms;
            }
            return totalMs;
        }
    }

    auto started = std::chrono::steady_clock::now();

    const std::uint64_t fingerprint = sceneFingerprint(scene);
    bool reuse = haveFingerprint_ && fingerprint == storedFingerprint_ && materialTex_ != 0 && instanceTex_ != 0 && storedShadowClip_.size() == static_cast<size_t>(kGpuMaxLights) * 16;
    {
        struct MeshProbe : gpu_detail::GpuContribute
        {
            bool hasMesh = false;
            void sphere(const Hittable &, const Vec3 &, double) override {}
            void plane(const Hittable &, const Vec3 &, const Vec3 &, bool, const Vec3 &, double) override {}
            void mesh(const Mesh &object) override
            {
                if (!object.triangles().empty())
                    hasMesh = true;
            }
        } probe;
        for (const auto &object : scene.objects())
        {
            object->contributeGpu(probe);
            if (probe.hasMesh)
                break;
        }
        if (probe.hasMesh && !shadowReady_)
            reuse = false;
    }

    std::vector<float> sphereGeom;
    std::vector<float> sphereAlbedo;
    std::vector<float> sphereMat;
    std::vector<float> sphereOpt;
    std::vector<int> sphereIds;
    std::vector<int> mirrorIds;
    std::vector<float> planePoint;
    std::vector<float> planeNormal;
    std::vector<float> planeAlbedo;
    std::vector<float> planeMat;
    std::vector<float> planeOpt;
    std::vector<float> planeChecker;
    std::vector<int> planeIds;
    std::vector<std::string> texturePaths;
    std::vector<std::string> normalPaths;
    std::vector<MeshTri> meshTriangles;
    std::vector<float> meshVertices;
    std::vector<BvhNode> meshNodes;
    std::vector<float> meshMin;
    std::vector<float> meshMax;
    std::vector<float> instanceTex(static_cast<size_t>(kGpuMaxMeshes) * kMeshRecordRows * 4, 0.0f);
    std::vector<float> materialTex(static_cast<size_t>(kGpuMaxMeshes) * 6 * 4, 0.0f);
    for (size_t texel = 0; texel < materialTex.size(); texel += 4)
    {
        materialTex[texel + 1] = -1.0f;
        materialTex[texel + 2] = -1.0f;
    }
    auto writeMaterial = [&](int row, int index, const Material &material, int layer) {
        const size_t at = (static_cast<size_t>(row) * kGpuMaxMeshes + static_cast<size_t>(index)) * 4;
        materialTex[at] = static_cast<float>(material.emission);
        materialTex[at + 1] = static_cast<float>(layer);
        materialTex[at + 2] = static_cast<float>(material.roughness);
        const size_t scrollAt = (static_cast<size_t>(row + 3) * kGpuMaxMeshes + static_cast<size_t>(index)) * 4;
        materialTex[scrollAt] = static_cast<float>(material.uvScrollU);
        materialTex[scrollAt + 1] = static_cast<float>(material.uvScrollV);
    };
    int meshCount = 0;
    int triangleCount = 0;
    int hasGlass = 0;
    auto splitAxis = [](const BvhNode &leftNode, const BvhNode &rightNode) {
        const double dx = std::abs((leftNode.boundsMin.x + leftNode.boundsMax.x) - (rightNode.boundsMin.x + rightNode.boundsMax.x));
        const double dy = std::abs((leftNode.boundsMin.y + leftNode.boundsMax.y) - (rightNode.boundsMin.y + rightNode.boundsMax.y));
        const double dz = std::abs((leftNode.boundsMin.z + leftNode.boundsMax.z) - (rightNode.boundsMin.z + rightNode.boundsMax.z));
        if (dy > dx && dy >= dz)
            return 1;
        if (dz > dx && dz > dy)
            return 2;
        return 0;
    };

    auto textureLayer = [&](const std::string &path) {
        if (path.empty())
            return -1;
        for (int index = 0; index < static_cast<int>(texturePaths.size()); ++index)
        {
            if (texturePaths[static_cast<size_t>(index)] == path)
                return index;
        }
        if (static_cast<int>(texturePaths.size()) >= kGpuMaxTextures)
            return -1;
        texturePaths.push_back(path);
        return static_cast<int>(texturePaths.size()) - 1;
    };
    auto normalLayer = [&](const std::string &path) {
        if (path.empty())
            return -1;
        for (int index = 0; index < static_cast<int>(normalPaths.size()); ++index)
        {
            if (normalPaths[static_cast<size_t>(index)] == path)
                return index;
        }
        if (static_cast<int>(normalPaths.size()) >= kGpuMaxTextures)
            return -1;
        normalPaths.push_back(path);
        return static_cast<int>(normalPaths.size()) - 1;
    };

    struct SharedGeom
    {
        const MeshGeometry *geometry = nullptr;
        int root = -1;
    };
    std::vector<SharedGeom> sharedGeoms;

    struct UploadSink : gpu_detail::GpuContribute
    {
        std::vector<float> *sphereGeom = nullptr;
        std::vector<float> *sphereAlbedo = nullptr;
        std::vector<float> *sphereMat = nullptr;
        std::vector<float> *sphereOpt = nullptr;
        std::vector<int> *sphereIds = nullptr;
        std::vector<int> *mirrorIds = nullptr;
        std::vector<float> *planePoint = nullptr;
        std::vector<float> *planeNormal = nullptr;
        std::vector<float> *planeAlbedo = nullptr;
        std::vector<float> *planeMat = nullptr;
        std::vector<float> *planeOpt = nullptr;
        std::vector<float> *planeChecker = nullptr;
        std::vector<int> *planeIds = nullptr;
        std::vector<MeshTri> *meshTriangles = nullptr;
        std::vector<float> *meshVertices = nullptr;
        std::vector<BvhNode> *meshNodes = nullptr;
        std::vector<float> *meshMin = nullptr;
        std::vector<float> *meshMax = nullptr;
        std::vector<SharedGeom> *sharedGeoms = nullptr;
        std::vector<float> *instanceTex = nullptr;
        std::function<int(const std::string &)> textureLayer;
        std::function<int(const std::string &)> normalLayer;
        std::function<void(int, int, const Material &, int)> writeMaterial;
        int *meshCount = nullptr;
        int *triangleCount = nullptr;
        int *hasGlass = nullptr;
        bool reuse = false;

        void writeInstance(int instance, int row, float x, float y, float z, float w)
        {
            const size_t at = (static_cast<size_t>(row) * static_cast<size_t>(kGpuMaxMeshes) + static_cast<size_t>(instance)) * 4;
            (*instanceTex)[at] = x;
            (*instanceTex)[at + 1] = y;
            (*instanceTex)[at + 2] = z;
            (*instanceTex)[at + 3] = w;
        }

        void sphere(const Hittable &object, const Vec3 &center, double worldRadius) override
        {
            if (static_cast<int>(sphereIds->size()) >= kGpuMaxSpheres)
                return;
            sphereGeom->push_back(static_cast<float>(center.x));
            sphereGeom->push_back(static_cast<float>(center.y));
            sphereGeom->push_back(static_cast<float>(center.z));
            sphereGeom->push_back(static_cast<float>(worldRadius));
            const Material material = object.material();
            sphereAlbedo->push_back(static_cast<float>(material.albedo.x));
            sphereAlbedo->push_back(static_cast<float>(material.albedo.y));
            sphereAlbedo->push_back(static_cast<float>(material.albedo.z));
            sphereAlbedo->push_back(static_cast<float>(material.ambient));
            sphereMat->push_back(static_cast<float>(material.diffuse));
            sphereMat->push_back(static_cast<float>(material.specular));
            sphereMat->push_back(reuse ? 0.0f : shadeExponent(material));
            sphereMat->push_back(static_cast<float>(material.reflectivity));
            pushOpt(*sphereOpt, material, textureLayer(material.albedoMap));
            const int sphereNormal = normalLayer(material.normalMap);
            if (!reuse)
                writeMaterial(0, static_cast<int>(sphereIds->size()), material, sphereNormal);
            if (material.transmission > 0.001)
                *hasGlass = 1;
            sphereIds->push_back(object.id);
            if (material.reflectivity > engineSettings().mirrorReflectMin && material.transmission <= 0.001 && mirrorIds->size() < 8)
                mirrorIds->push_back(static_cast<int>(sphereIds->size()) - 1);
        }

        void plane(const Hittable &object, const Vec3 &point, const Vec3 &normal, bool checker, const Vec3 &checkerAlbedo, double checkerScale) override
        {
            if (static_cast<int>(planeIds->size()) >= kGpuMaxPlanes)
                return;
            pushVec3(*planePoint, point);
            planeNormal->push_back(static_cast<float>(normal.x));
            planeNormal->push_back(static_cast<float>(normal.y));
            planeNormal->push_back(static_cast<float>(normal.z));
            planeNormal->push_back(checker ? 1.0f : 0.0f);
            const Material material = object.material();
            planeAlbedo->push_back(static_cast<float>(material.albedo.x));
            planeAlbedo->push_back(static_cast<float>(material.albedo.y));
            planeAlbedo->push_back(static_cast<float>(material.albedo.z));
            planeAlbedo->push_back(static_cast<float>(material.ambient));
            planeMat->push_back(static_cast<float>(material.diffuse));
            planeMat->push_back(static_cast<float>(material.specular));
            planeMat->push_back(reuse ? 0.0f : shadeExponent(material));
            planeMat->push_back(static_cast<float>(material.reflectivity));
            planeChecker->push_back(static_cast<float>(checkerAlbedo.x));
            planeChecker->push_back(static_cast<float>(checkerAlbedo.y));
            planeChecker->push_back(static_cast<float>(checkerAlbedo.z));
            planeChecker->push_back(static_cast<float>(checkerScale));
            pushOpt(*planeOpt, material, textureLayer(material.albedoMap));
            const int planeNormalLayer = normalLayer(material.normalMap);
            if (!reuse)
                writeMaterial(1, static_cast<int>(planeIds->size()), material, planeNormalLayer);
            if (material.transmission > 0.001)
                *hasGlass = 1;
            planeIds->push_back(object.id);
        }

        void mesh(const Mesh &meshObject) override
        {
            const MeshGeometry *geometry = meshObject.geometry().get();
            if (*meshCount >= kGpuMaxMeshes || geometry == nullptr || geometry->root < 0 || geometry->triangles.empty())
                return;
            int shared = -1;
            for (int index = 0; index < static_cast<int>(sharedGeoms->size()); ++index)
            {
                if ((*sharedGeoms)[static_cast<size_t>(index)].geometry == geometry)
                {
                    shared = index;
                    break;
                }
            }
            if (shared < 0)
            {
                if (*triangleCount + static_cast<int>(geometry->triangles.size()) > kGpuMaxTriangles)
                    return;
                const int triangleOffset = static_cast<int>(meshTriangles->size());
                const int nodeOffset = static_cast<int>(meshNodes->size());
                std::vector<BvhNode> nodes = geometry->nodes;
                for (BvhNode &node : nodes)
                {
                    if (node.right < 0)
                        node.left += triangleOffset;
                    else
                    {
                        node.left += nodeOffset;
                        node.right += nodeOffset;
                    }
                }
                meshTriangles->insert(meshTriangles->end(), geometry->triangles.begin(), geometry->triangles.end());
                meshNodes->insert(meshNodes->end(), nodes.begin(), nodes.end());
                *triangleCount += static_cast<int>(geometry->triangles.size());
                shared = static_cast<int>(sharedGeoms->size());
                sharedGeoms->push_back(SharedGeom{geometry, geometry->root + nodeOffset});
            }
            const int root = (*sharedGeoms)[static_cast<size_t>(shared)].root;
            Vec3 axisX;
            Vec3 axisY;
            Vec3 axisZ;
            meshObject.worldAxes(axisX, axisY, axisZ);
            const double worldScale = meshObject.worldScale();
            Vec3 boundsMin(1e30, 1e30, 1e30);
            Vec3 boundsMax(-1e30, -1e30, -1e30);
            for (int corner = 0; corner < 8; ++corner)
            {
                const Vec3 local(
                    (corner & 1) != 0 ? geometry->boundsMax.x : geometry->boundsMin.x,
                    (corner & 2) != 0 ? geometry->boundsMax.y : geometry->boundsMin.y,
                    (corner & 4) != 0 ? geometry->boundsMax.z : geometry->boundsMin.z);
                const Vec3 world = meshObject.position() + (axisX * local.x + axisY * local.y + axisZ * local.z) * worldScale;
                boundsMin.x = std::min(boundsMin.x, world.x);
                boundsMin.y = std::min(boundsMin.y, world.y);
                boundsMin.z = std::min(boundsMin.z, world.z);
                boundsMax.x = std::max(boundsMax.x, world.x);
                boundsMax.y = std::max(boundsMax.y, world.y);
                boundsMax.z = std::max(boundsMax.z, world.z);
            }
            boundsMin = boundsMin - Vec3(1e-3, 1e-3, 1e-3);
            boundsMax = boundsMax + Vec3(1e-3, 1e-3, 1e-3);
            for (const MeshTri &triangle : geometry->triangles)
            {
                for (int corner = 0; corner < 3; ++corner)
                {
                    const Vec3 local = triangle.position[corner];
                    const Vec3 position = meshObject.position() + (axisX * local.x + axisY * local.y + axisZ * local.z) * worldScale;
                    Vec3 normal = axisX * triangle.normal[corner].x + axisY * triangle.normal[corner].y + axisZ * triangle.normal[corner].z;
                    if (length(normal) > 1e-8)
                        normal = normalize(normal);
                    meshVertices->push_back(static_cast<float>(position.x));
                    meshVertices->push_back(static_cast<float>(position.y));
                    meshVertices->push_back(static_cast<float>(position.z));
                    meshVertices->push_back(static_cast<float>(normal.x));
                    meshVertices->push_back(static_cast<float>(normal.y));
                    meshVertices->push_back(static_cast<float>(normal.z));
                    meshVertices->push_back(triangle.u[corner]);
                    meshVertices->push_back(triangle.v[corner]);
                    meshVertices->push_back(static_cast<float>(*meshCount));
                }
            }
            meshMin->push_back(static_cast<float>(boundsMin.x));
            meshMin->push_back(static_cast<float>(boundsMin.y));
            meshMin->push_back(static_cast<float>(boundsMin.z));
            meshMin->push_back(0.0f);
            meshMax->push_back(static_cast<float>(boundsMax.x));
            meshMax->push_back(static_cast<float>(boundsMax.y));
            meshMax->push_back(static_cast<float>(boundsMax.z));
            meshMax->push_back(0.0f);
            const Material material = meshObject.material();
            writeInstance(*meshCount, 0, static_cast<float>(material.albedo.x), static_cast<float>(material.albedo.y), static_cast<float>(material.albedo.z), static_cast<float>(material.ambient));
            writeInstance(*meshCount, 1, static_cast<float>(material.diffuse), static_cast<float>(material.specular), reuse ? 0.0f : shadeExponent(material), static_cast<float>(material.reflectivity));
            writeInstance(*meshCount, 2, static_cast<float>(material.transmission), static_cast<float>(material.ior), static_cast<float>(textureLayer(material.albedoMap)), static_cast<float>(material.uvScale > 0 ? material.uvScale : 1.0));
            writeInstance(*meshCount, 3, static_cast<float>(boundsMin.x), static_cast<float>(boundsMin.y), static_cast<float>(boundsMin.z), static_cast<float>(root));
            writeInstance(*meshCount, 4, static_cast<float>(boundsMax.x), static_cast<float>(boundsMax.y), static_cast<float>(boundsMax.z), static_cast<float>(meshObject.id));
            writeInstance(*meshCount, 5, static_cast<float>(meshObject.position().x), static_cast<float>(meshObject.position().y), static_cast<float>(meshObject.position().z), static_cast<float>(worldScale));
            writeInstance(*meshCount, 6, static_cast<float>(axisX.x), static_cast<float>(axisX.y), static_cast<float>(axisX.z), 0.0f);
            writeInstance(*meshCount, 7, static_cast<float>(axisY.x), static_cast<float>(axisY.y), static_cast<float>(axisY.z), 0.0f);
            writeInstance(*meshCount, 8, static_cast<float>(axisZ.x), static_cast<float>(axisZ.y), static_cast<float>(axisZ.z), 0.0f);
            const int meshNormal = normalLayer(material.normalMap);
            if (!reuse)
                writeMaterial(2, *meshCount, material, meshNormal);
            if (material.transmission > 0.001)
                *hasGlass = 1;
            ++(*meshCount);
        }
    } upload;
    upload.sphereGeom = &sphereGeom;
    upload.sphereAlbedo = &sphereAlbedo;
    upload.sphereMat = &sphereMat;
    upload.sphereOpt = &sphereOpt;
    upload.sphereIds = &sphereIds;
    upload.mirrorIds = &mirrorIds;
    upload.planePoint = &planePoint;
    upload.planeNormal = &planeNormal;
    upload.planeAlbedo = &planeAlbedo;
    upload.planeMat = &planeMat;
    upload.planeOpt = &planeOpt;
    upload.planeChecker = &planeChecker;
    upload.planeIds = &planeIds;
    upload.meshTriangles = &meshTriangles;
    upload.meshVertices = &meshVertices;
    upload.meshNodes = &meshNodes;
    upload.meshMin = &meshMin;
    upload.meshMax = &meshMax;
    upload.sharedGeoms = &sharedGeoms;
    upload.instanceTex = &instanceTex;
    upload.textureLayer = textureLayer;
    upload.normalLayer = normalLayer;
    upload.writeMaterial = writeMaterial;
    upload.meshCount = &meshCount;
    upload.triangleCount = &triangleCount;
    upload.hasGlass = &hasGlass;
    upload.reuse = reuse;
    for (const auto &object : scene.objects())
        object->contributeGpu(upload);

    std::vector<float> lightPos;
    std::vector<float> lightColor;
    std::vector<float> lightAux;
    std::vector<float> lightSpot;
    int lightCount = 0;
    for (const PointLight &light : scene.lights())
    {
        if (lightCount >= kGpuMaxLights)
            break;
        lightPos.push_back(static_cast<float>(light.position.x));
        lightPos.push_back(static_cast<float>(light.position.y));
        lightPos.push_back(static_cast<float>(light.position.z));
        lightPos.push_back(static_cast<float>(light.intensity));
        lightColor.push_back(static_cast<float>(light.color.x));
        lightColor.push_back(static_cast<float>(light.color.y));
        lightColor.push_back(static_cast<float>(light.color.z));
        lightColor.push_back(static_cast<float>(light.falloff));
        const double radius = light.directional ? light.radius * (kPi / 180.0) : light.radius;
        const bool spot = light.spotOuter > 0.0;
        double innerDeg = light.spotInner;
        if (innerDeg < 0.0)
            innerDeg = 0.0;
        if (innerDeg > light.spotOuter)
            innerDeg = light.spotOuter;
        lightAux.push_back(static_cast<float>(radius));
        lightAux.push_back(light.directional ? 1.0f : 0.0f);
        lightAux.push_back(spot ? static_cast<float>(innerDeg * (kPi / 180.0)) : 0.0f);
        lightAux.push_back(0.0f);
        if (spot)
        {
            lightSpot.push_back(static_cast<float>(light.spotDirection.x));
            lightSpot.push_back(static_cast<float>(light.spotDirection.y));
            lightSpot.push_back(static_cast<float>(light.spotDirection.z));
            lightSpot.push_back(static_cast<float>(light.spotOuter * (kPi / 180.0)));
        }
        else
        {
            lightSpot.push_back(0.0f);
            lightSpot.push_back(0.0f);
            lightSpot.push_back(0.0f);
            lightSpot.push_back(0.0f);
        }
        ++lightCount;
    }
    for (const auto &object : scene.objects())
    {
        if (lightCount >= kGpuMaxLights)
            break;
        const Material material = object->material();
        if (material.emission <= 0.01)
            continue;
        const Vec3 position = object->worldPosition();
        lightPos.push_back(static_cast<float>(position.x));
        lightPos.push_back(static_cast<float>(position.y));
        lightPos.push_back(static_cast<float>(position.z));
        lightPos.push_back(static_cast<float>(material.emission));
        lightColor.push_back(static_cast<float>(material.albedo.x));
        lightColor.push_back(static_cast<float>(material.albedo.y));
        lightColor.push_back(static_cast<float>(material.albedo.z));
        lightColor.push_back(0.2f);
        lightAux.push_back(0.0f);
        lightAux.push_back(0.0f);
        lightAux.push_back(0.0f);
        lightAux.push_back(static_cast<float>(object->id));
        lightSpot.push_back(0.0f);
        lightSpot.push_back(0.0f);
        lightSpot.push_back(0.0f);
        lightSpot.push_back(0.0f);
        ++lightCount;
    }

    std::vector<float> triangleTexels;
    triangleTexels.reserve(meshTriangles.size() * 24);
    for (const MeshTri &triangle : meshTriangles)
    {
        auto push = [&](const Vec3 &value, float extra) {
            triangleTexels.push_back(static_cast<float>(value.x));
            triangleTexels.push_back(static_cast<float>(value.y));
            triangleTexels.push_back(static_cast<float>(value.z));
            triangleTexels.push_back(extra);
        };
        push(triangle.position[0], triangle.u[0]);
        push(triangle.position[1], triangle.v[0]);
        push(triangle.position[2], triangle.u[1]);
        push(triangle.normal[0], triangle.v[1]);
        push(triangle.normal[1], triangle.u[2]);
        push(triangle.normal[2], triangle.v[2]);
    }
    std::vector<float> bvhTexels;
    bvhTexels.reserve(meshNodes.size() * 8);
    for (const BvhNode &node : meshNodes)
    {
        float leftChannel = static_cast<float>(node.left);
        if (node.right >= 0)
        {
            const int axis = splitAxis(meshNodes[static_cast<size_t>(node.left)], meshNodes[static_cast<size_t>(node.right)]);
            leftChannel = static_cast<float>(node.left + axis * 1048576);
        }
        bvhTexels.push_back(static_cast<float>(node.boundsMin.x));
        bvhTexels.push_back(static_cast<float>(node.boundsMin.y));
        bvhTexels.push_back(static_cast<float>(node.boundsMin.z));
        bvhTexels.push_back(leftChannel);
        bvhTexels.push_back(static_cast<float>(node.boundsMax.x));
        bvhTexels.push_back(static_cast<float>(node.boundsMax.y));
        bvhTexels.push_back(static_cast<float>(node.boundsMax.z));
        bvhTexels.push_back(static_cast<float>(node.right));
    }

    if (glActiveTextureFn != nullptr)
        glActiveTextureFn(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (width != width_ || height != height_ || attached_ != texture || linear_ != linearOutput)
    {
        const GLenum internal = linearOutput ? GL_RGBA32F : GL_RGBA8;
        const GLenum type = linearOutput ? GL_FLOAT : GL_UNSIGNED_BYTE;
        glTexImage2D(GL_TEXTURE_2D, 0, internal, width, height, 0, GL_RGBA, type, nullptr);
        glBindFramebufferFn(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2DFn(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        if (glCheckFramebufferStatusFn(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            glBindFramebufferFn(GL_FRAMEBUFFER, 0);
            failure_ = "Could not attach the render target";
            return -1;
        }
        width_ = width;
        height_ = height;
        attached_ = texture;
        linear_ = linearOutput;
    }
    else
    {
        glBindFramebufferFn(GL_FRAMEBUFFER, fbo_);
    }

    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    std::vector<float> shadowClip;
    if (reuse)
        shadowClip = storedShadowClip_;
    else
    {
        shadowClip.assign(static_cast<size_t>(kGpuMaxLights) * 16, 0.0f);
        for (int light = 0; light < kGpuMaxLights; ++light)
        {
            shadowClip[static_cast<size_t>(light) * 16 + 0] = 1.0f;
            shadowClip[static_cast<size_t>(light) * 16 + 5] = 1.0f;
            shadowClip[static_cast<size_t>(light) * 16 + 10] = 1.0f;
            shadowClip[static_cast<size_t>(light) * 16 + 15] = 1.0f;
        }
    }
    setReuseShadowMaps(reuse);
    if (meshCount > 0)
        rasterizeMeshes(camera, scene, meshVertices, meshMin, meshMax, meshCount, width, height, shadowClip, grid, sampleIndex);
    if (!reuse)
    {
        storedShadowClip_ = shadowClip;
        storedFingerprint_ = fingerprint;
        haveFingerprint_ = true;
    }
    glBindFramebufferFn(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);

    const unsigned activeProgram = program_;
    glUseProgramFn(activeProgram);
    glBindVertexArrayFn(vao_);

    auto location = [&](const char *name) {
        return glGetUniformLocationFn(activeProgram, name);
    };
    auto uniform3 = [&](const char *name, const Vec3 &value) {
        glUniform3fFn(location(name), static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z));
    };

    uniform3("uOrigin", camera.origin());
    uniform3("uLowerLeft", camera.lowerLeft());
    uniform3("uHorizontal", camera.horizontal());
    uniform3("uVertical", camera.vertical());
    uniform3("uCamRight", camera.rightAxis());
    uniform3("uCamUp", camera.upAxis());
    uniform3("uView", camera.viewDirection());
    glUniform1fFn(location("uAperture"), static_cast<float>(camera.aperture()));
    glUniform1fFn(location("uFocus"), static_cast<float>(camera.focusDistance()));
    uniform3("uAmbient", scene.ambient());
    uniform3("uHorizon", scene.horizon());
    uniform3("uFogColor", scene.fogColor());
    glUniform1fFn(location("uFogDensity"), static_cast<float>(scene.fogDensity()));
    uniform3("uZenith", scene.zenith());
    glUniform1iFn(location("uWidth"), width);
    glUniform1iFn(location("uHeight"), height);
    const bool fold = sampleIndex >= 0;
    glUniform1iFn(location("uSamples"), grid);
    glUniform1iFn(location("uSampleOffset"), fold ? sampleIndex : 0);
    glUniform1iFn(location("uSampleBatch"), fold ? 1 : 0);
    glUniform1iFn(location("uDepth"), maxDepth);
    glUniform1iFn(location("uSelected"), selectedObject);
    glUniform1iFn(location("uLinear"), (linearOutput || fold) ? 1 : 0);
    if (!reuse)
    {
    glUniform1iFn(location("uHasGlass"), hasGlass);
    glUniform1iFn(location("uSphereCount"), static_cast<int>(sphereIds.size()));
    if (!mirrorIds.empty())
        glUniform1ivFn(location("uMirrorIndex"), static_cast<GLsizei>(mirrorIds.size()), mirrorIds.data());
    glUniform1iFn(location("uPlaneCount"), static_cast<int>(planeIds.size()));
    glUniform1iFn(location("uLightCount"), lightCount);
    if (!sphereIds.empty())
    {
        glUniform4fvFn(location("uSphereGeom"), static_cast<GLsizei>(sphereIds.size()), sphereGeom.data());
        glUniform4fvFn(location("uSphereAlbedo"), static_cast<GLsizei>(sphereIds.size()), sphereAlbedo.data());
        glUniform4fvFn(location("uSphereMat"), static_cast<GLsizei>(sphereIds.size()), sphereMat.data());
        glUniform4fvFn(location("uSphereOpt"), static_cast<GLsizei>(sphereIds.size()), sphereOpt.data());
        glUniform1ivFn(location("uSphereId"), static_cast<GLsizei>(sphereIds.size()), sphereIds.data());
    }
    if (!planeIds.empty())
    {
        glUniform4fvFn(location("uPlanePoint"), static_cast<GLsizei>(planeIds.size()), planePoint.data());
        glUniform4fvFn(location("uPlaneNormal"), static_cast<GLsizei>(planeIds.size()), planeNormal.data());
        glUniform4fvFn(location("uPlaneAlbedo"), static_cast<GLsizei>(planeIds.size()), planeAlbedo.data());
        glUniform4fvFn(location("uPlaneMat"), static_cast<GLsizei>(planeIds.size()), planeMat.data());
        glUniform4fvFn(location("uPlaneOpt"), static_cast<GLsizei>(planeIds.size()), planeOpt.data());
        glUniform4fvFn(location("uPlaneChecker"), static_cast<GLsizei>(planeIds.size()), planeChecker.data());
        glUniform1ivFn(location("uPlaneId"), static_cast<GLsizei>(planeIds.size()), planeIds.data());
    }
    if (lightCount > 0)
    {
        glUniform4fvFn(location("uLightPos"), lightCount, lightPos.data());
        glUniform4fvFn(location("uLightColor"), lightCount, lightColor.data());
        glUniform4fvFn(location("uLightAux"), lightCount, lightAux.data());
        glUniform4fvFn(location("uLightSpot"), lightCount, lightSpot.data());
    }
    glUniform1iFn(location("uMeshCount"), meshCount);
    }
    glUniform1iFn(location("uMirrorCount"), mirrorBounces ? static_cast<int>(mirrorIds.size()) : 0);
    glUniformMatrix4fvFn(location("uShadowMatrix"), kGpuMaxLights, GL_FALSE, shadowClip.data());
    glUniform1iFn(location("uMeshTraceLimit"), engineSettings().meshTraceLimit);
    {
        const EngineSettings &settings = engineSettings();
        int jobStack = settings.jobStackLimit;
        if (jobStack < 1)
            jobStack = 1;
        if (jobStack > kGlslJobStackMax)
            jobStack = kGlslJobStackMax;
        int traceCap = settings.traceLimit;
        if (traceCap < 1)
            traceCap = 1;
        if (traceCap > kGlslTraceLimitMax)
            traceCap = kGlslTraceLimitMax;
        int meshStack = settings.meshStackLimit;
        if (meshStack < 2)
            meshStack = 2;
        if (meshStack > kGlslMeshStackMax)
            meshStack = kGlslMeshStackMax;
        glUniform1iFn(location("uJobStackLimit"), jobStack);
        glUniform1iFn(location("uTraceLimit"), traceCap);
        glUniform1iFn(location("uMeshStackLimit"), meshStack);
    }
    glUniform1fFn(location("uExposure"), static_cast<float>(scene.exposure()));
    glUniform1fFn(location("uTime"), static_cast<float>(timeSeconds));
    const int particleCount = std::min(16, static_cast<int>(scene.particles().size()));
    glUniform1iFn(location("uParticleCount"), particleCount);
    float particlePos[16 * 4] = {};
    float particleColor[16 * 4] = {};
    for (int index = 0; index < particleCount; ++index)
    {
        const Particle &particle = scene.particles()[static_cast<size_t>(index)];
        const float fade = static_cast<float>(std::clamp(particle.life / 0.45, 0.0, 1.0));
        particlePos[index * 4] = static_cast<float>(particle.position.x);
        particlePos[index * 4 + 1] = static_cast<float>(particle.position.y);
        particlePos[index * 4 + 2] = static_cast<float>(particle.position.z);
        particlePos[index * 4 + 3] = static_cast<float>(particle.size) * fade;
        particleColor[index * 4] = static_cast<float>(particle.color.x);
        particleColor[index * 4 + 1] = static_cast<float>(particle.color.y);
        particleColor[index * 4 + 2] = static_cast<float>(particle.color.z);
        particleColor[index * 4 + 3] = fade;
    }
    if (particleCount > 0)
    {
        glUniform4fvFn(location("uParticlePos"), particleCount, particlePos);
        glUniform4fvFn(location("uParticleColor"), particleCount, particleColor);
    }

    std::string albedoKey;
    for (const std::string &path : texturePaths)
    {
        const ImageCache &cached = cachedImage(path);
        albedoKey += path;
        albedoKey.push_back('|');
        albedoKey += std::to_string(cached.bytes);
        albedoKey.push_back('|');
        if (!cached.missing)
            albedoKey += std::to_string(static_cast<long long>(cached.stamp.time_since_epoch().count()));
    }
    const bool newAlbedo = albedoArray_ == 0;
    if (newAlbedo)
        glGenTextures(1, &albedoArray_);
    glActiveTextureFn(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, albedoArray_);
    if (newAlbedo || albedoKey != albedoKey_)
    {
        configureTexture(GL_TEXTURE_2D_ARRAY, GL_LINEAR);
        if (texturePaths.empty())
        {
            const unsigned char white[4] = {255, 255, 255, 255};
            glTexImage3DFn(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        }
        else
        {
            const int layers = static_cast<int>(texturePaths.size());
            std::vector<unsigned char> pixels(static_cast<size_t>(kAlbedoEdge) * kAlbedoEdge * 4 * static_cast<size_t>(layers), 255);
            for (int layer = 0; layer < layers; ++layer)
            {
                const ImageCache &cached = cachedImage(texturePaths[static_cast<size_t>(layer)]);
                std::vector<unsigned char> scaled(static_cast<size_t>(kAlbedoEdge) * kAlbedoEdge * 4, 255);
                if (!cached.missing && cached.image.width > 0)
                    scaled = scaledAlbedo(cached.image);
                const size_t offset = static_cast<size_t>(layer) * scaled.size();
                std::copy(scaled.begin(), scaled.end(), pixels.begin() + static_cast<std::ptrdiff_t>(offset));
            }
            glTexImage3DFn(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, kAlbedoEdge, kAlbedoEdge, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        albedoKey_ = albedoKey;
    }

    std::string normalKey;
    for (const std::string &path : normalPaths)
    {
        const ImageCache &cached = cachedImage(path);
        normalKey += path;
        normalKey.push_back('|');
        normalKey += std::to_string(cached.bytes);
        normalKey.push_back('|');
        if (!cached.missing)
            normalKey += std::to_string(static_cast<long long>(cached.stamp.time_since_epoch().count()));
    }
    const bool newNormal = normalArray_ == 0;
    if (newNormal)
        glGenTextures(1, &normalArray_);
    if (materialTex_ == 0)
        glGenTextures(1, &materialTex_);
    glActiveTextureFn(GL_TEXTURE0 + 9);
    glBindTexture(GL_TEXTURE_2D, materialTex_);
    configureTexture(GL_TEXTURE_2D, GL_NEAREST);
    if (!reuse)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kGpuMaxMeshes, 6, 0, GL_RGBA, GL_FLOAT, materialTex.data());
    if (instanceTex_ == 0)
        glGenTextures(1, &instanceTex_);
    glActiveTextureFn(GL_TEXTURE0 + 10);
    glBindTexture(GL_TEXTURE_2D, instanceTex_);
    configureTexture(GL_TEXTURE_2D, GL_NEAREST);
    if (!reuse)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kGpuMaxMeshes, kMeshRecordRows, 0, GL_RGBA, GL_FLOAT, instanceTex.data());

    glActiveTextureFn(GL_TEXTURE0 + 8);
    glBindTexture(GL_TEXTURE_2D_ARRAY, normalArray_);
    if (newNormal || normalKey != normalKey_)
    {
        configureTexture(GL_TEXTURE_2D_ARRAY, GL_LINEAR);
        if (normalPaths.empty())
        {
            const unsigned char flat[4] = {128, 128, 255, 255};
            glTexImage3DFn(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, flat);
        }
        else
        {
            const int layers = static_cast<int>(normalPaths.size());
            std::vector<unsigned char> pixels(static_cast<size_t>(kAlbedoEdge) * kAlbedoEdge * 4 * static_cast<size_t>(layers), 255);
            for (int layer = 0; layer < layers; ++layer)
            {
                const ImageCache &cached = cachedImage(normalPaths[static_cast<size_t>(layer)]);
                std::vector<unsigned char> scaled(static_cast<size_t>(kAlbedoEdge) * kAlbedoEdge * 4, 255);
                if (!cached.missing && cached.image.width > 0)
                    scaled = scaledAlbedo(cached.image);
                else
                {
                    for (size_t texel = 0; texel < scaled.size(); texel += 4)
                    {
                        scaled[texel] = 128;
                        scaled[texel + 1] = 128;
                        scaled[texel + 2] = 255;
                    }
                }
                const size_t offset = static_cast<size_t>(layer) * scaled.size();
                std::copy(scaled.begin(), scaled.end(), pixels.begin() + static_cast<std::ptrdiff_t>(offset));
            }
            glTexImage3DFn(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, kAlbedoEdge, kAlbedoEdge, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        normalKey_ = normalKey;
    }
    glActiveTextureFn(GL_TEXTURE0);

    bool hasEnv = false;
    std::string envKey = scene.environment();
    const LoadedImage *envImage = nullptr;
    if (!envKey.empty())
    {
        const ImageCache &cached = cachedImage(envKey);
        if (!cached.missing && cached.image.width > 0 && cached.image.height > 0)
        {
            hasEnv = true;
            envImage = &cached.image;
            envKey.push_back('|');
            envKey += std::to_string(cached.bytes);
            envKey.push_back('|');
            envKey += std::to_string(static_cast<long long>(cached.stamp.time_since_epoch().count()));
        }
    }
    const bool newEnv = envTexture_ == 0;
    if (newEnv)
        glGenTextures(1, &envTexture_);
    glActiveTextureFn(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, envTexture_);
    if (newEnv || envKey != envKey_)
    {
        configureTexture(GL_TEXTURE_2D, GL_LINEAR);
        if (!hasEnv || envImage == nullptr)
        {
            const unsigned char black[4] = {0, 0, 0, 255};
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
        }
        else if (envImage->hdr)
        {
            std::vector<unsigned short> half(static_cast<size_t>(envImage->width) * static_cast<size_t>(envImage->height) * 4);
            for (int y = 0; y < envImage->height; ++y)
            {
                const int sourceY = envImage->height - 1 - y;
                for (int x = 0; x < envImage->width; ++x)
                {
                    const size_t from = (static_cast<size_t>(sourceY) * static_cast<size_t>(envImage->width) + static_cast<size_t>(x)) * 4;
                    const size_t to = (static_cast<size_t>(y) * static_cast<size_t>(envImage->width) + static_cast<size_t>(x)) * 4;
                    for (int channel = 0; channel < 4; ++channel)
                        half[to + static_cast<size_t>(channel)] = floatToHalf(envImage->rgba32[from + static_cast<size_t>(channel)]);
                }
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, envImage->width, envImage->height, 0, GL_RGBA, GL_HALF_FLOAT, half.data());
        }
        else
        {
            std::vector<unsigned char> flipped(envImage->rgba8.size());
            const size_t row = static_cast<size_t>(envImage->width) * 4;
            for (int y = 0; y < envImage->height; ++y)
            {
                const int sourceY = envImage->height - 1 - y;
                std::memcpy(flipped.data() + static_cast<size_t>(y) * row, envImage->rgba8.data() + static_cast<size_t>(sourceY) * row, row);
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, envImage->width, envImage->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
        }
        envKey_ = envKey;
    }

    glActiveTextureFn(GL_TEXTURE0 + 2);
    uploadDataTexture(triTexture_, triangleTexels, triCache_, triHeight_, storedTriHash_, storedTriCount_);
    glActiveTextureFn(GL_TEXTURE0 + 3);
    uploadDataTexture(bvhTexture_, bvhTexels, bvhCache_, bvhHeight_, storedBvhHash_, storedBvhCount_);

    glActiveTextureFn(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_2D, meshPosTex_);
    glActiveTextureFn(GL_TEXTURE0 + 5);
    glBindTexture(GL_TEXTURE_2D, meshNormTex_);
    glActiveTextureFn(GL_TEXTURE0 + 6);
    glBindTexture(GL_TEXTURE_2D, meshUvTex_);
    glActiveTextureFn(GL_TEXTURE0 + 7);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadowTex_);
    glUniform1iFn(location("uAlbedo"), 0);
    glUniform1iFn(location("uNormal"), 8);
    glUniform1iFn(location("uMaterial"), 9);
    glUniform1iFn(location("uInstances"), 10);
    glUniform1iFn(location("uEnv"), 1);
    glUniform1iFn(location("uTris"), 2);
    glUniform1iFn(location("uBvh"), 3);
    glUniform1iFn(location("uMeshPos"), 4);
    glUniform1iFn(location("uMeshNorm"), 5);
    glUniform1iFn(location("uMeshUv"), 6);
    glUniform1iFn(location("uShadowMap"), 7);
    glUniform1iFn(location("uHasEnv"), hasEnv ? 1 : 0);

    if (fold)
    {
        auto allocate = [&](unsigned &target) {
            if (target == 0)
                glGenTextures(1, &target);
            glBindTexture(GL_TEXTURE_2D, target);
            configureTexture(GL_TEXTURE_2D, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        };
        if (sampleW_ != width || sampleH_ != height || sampleTex_ == 0)
        {
            allocate(sampleTex_);
            allocate(accumTex_[0]);
            allocate(accumTex_[1]);
            sampleW_ = width;
            sampleH_ = height;
            accumCount_ = 0;
            accumFront_ = 0;
        }
        glBindFramebufferFn(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2DFn(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sampleTex_, 0);
        const GLenum draw = GL_COLOR_ATTACHMENT0;
        glDrawBuffersFn(1, &draw);
        glViewport(0, 0, width, height);
    }

    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (fold)
    {
        const int previous = sampleIndex == accumCount_ ? accumCount_ : 0;
        foldSample(texture, width, height, previous, linearOutput);
        accumCount_ = previous + 1;
    }
    if (!linearOutput)
        applyBloom(texture, width, height);
    glActiveTextureFn(GL_TEXTURE0);
    glBindFramebufferFn(GL_FRAMEBUFFER, 0);
    glBindVertexArrayFn(0);
    glUseProgramFn(0);
    glFinish();

    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count());
}


#endif
