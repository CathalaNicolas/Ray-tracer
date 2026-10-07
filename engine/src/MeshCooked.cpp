#include "MeshCooked.hpp"
#include "Content.hpp"

#include <zstd.h>

#include <cctype>
#include <cstring>
#include <fstream>

namespace
{

void appendU32(std::vector<std::uint8_t> &out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

bool readU32(const std::uint8_t *&cursor, const std::uint8_t *end, std::uint32_t &value)
{
    if (end - cursor < 4)
        return false;
    value = static_cast<std::uint32_t>(cursor[0]) | (static_cast<std::uint32_t>(cursor[1]) << 8)
        | (static_cast<std::uint32_t>(cursor[2]) << 16) | (static_cast<std::uint32_t>(cursor[3]) << 24);
    cursor += 4;
    return true;
}

bool readF32(const std::uint8_t *&cursor, const std::uint8_t *end, float &value)
{
    std::uint32_t bits = 0;
    if (!readU32(cursor, end, bits))
        return false;
    std::memcpy(&value, &bits, sizeof(bits));
    return true;
}

void appendF32(std::vector<std::uint8_t> &out, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(out, bits);
}

std::string pathUtf8(const std::filesystem::path &path)
{
    const std::u8string bytes = path.u8string();
    return std::string(bytes.begin(), bytes.end());
}

bool appendVertices(std::vector<std::uint8_t> &payload, const std::vector<Vec3> &positions,
    const std::vector<Vec3> &normals, const std::vector<float> &u, const std::vector<float> &v, std::string &error)
{
    if (positions.empty() || positions.size() > (1u << 24))
    {
        error = "invalid vertex count";
        return false;
    }
    if (normals.size() != positions.size() || u.size() != positions.size() || v.size() != positions.size())
    {
        error = "vertex attribute size mismatch";
        return false;
    }
    appendU32(payload, static_cast<std::uint32_t>(positions.size()));
    for (const Vec3 &position : positions)
    {
        appendF32(payload, static_cast<float>(position.x));
        appendF32(payload, static_cast<float>(position.y));
        appendF32(payload, static_cast<float>(position.z));
    }
    for (const Vec3 &normal : normals)
    {
        appendF32(payload, static_cast<float>(normal.x));
        appendF32(payload, static_cast<float>(normal.y));
        appendF32(payload, static_cast<float>(normal.z));
    }
    for (size_t i = 0; i < positions.size(); ++i)
    {
        appendF32(payload, u[i]);
        appendF32(payload, v[i]);
    }
    return true;
}

bool appendIndices(std::vector<std::uint8_t> &payload, const std::vector<std::uint32_t> &indices, std::uint32_t vertexCount,
    std::string &error)
{
    if (indices.empty() || (indices.size() % 3) != 0 || indices.size() / 3 > (1u << 24))
    {
        error = "invalid index count";
        return false;
    }
    appendU32(payload, static_cast<std::uint32_t>(indices.size()));
    for (std::uint32_t index : indices)
    {
        if (index >= vertexCount)
        {
            error = "index out of range";
            return false;
        }
        appendU32(payload, index);
    }
    return true;
}

bool readVertices(const std::uint8_t *&p, const std::uint8_t *pe, std::vector<Vec3> &positions,
    std::vector<Vec3> &normals, std::vector<float> &u, std::vector<float> &v, std::string &error)
{
    std::uint32_t vertexCount = 0;
    if (!readU32(p, pe, vertexCount) || vertexCount == 0 || vertexCount > (1u << 24))
    {
        error = "unsupported RTM vertex count";
        return false;
    }
    positions.resize(vertexCount);
    normals.resize(vertexCount);
    u.resize(vertexCount);
    v.resize(vertexCount);
    for (std::uint32_t i = 0; i < vertexCount; ++i)
    {
        float x = 0;
        float y = 0;
        float z = 0;
        if (!readF32(p, pe, x) || !readF32(p, pe, y) || !readF32(p, pe, z))
        {
            error = "truncated RTM positions";
            return false;
        }
        positions[i] = Vec3(x, y, z);
    }
    for (std::uint32_t i = 0; i < vertexCount; ++i)
    {
        float x = 0;
        float y = 0;
        float z = 0;
        if (!readF32(p, pe, x) || !readF32(p, pe, y) || !readF32(p, pe, z))
        {
            error = "truncated RTM normals";
            return false;
        }
        normals[i] = Vec3(x, y, z);
    }
    for (std::uint32_t i = 0; i < vertexCount; ++i)
        if (!readF32(p, pe, u[i]) || !readF32(p, pe, v[i]))
        {
            error = "truncated RTM uvs";
            return false;
        }
    return true;
}

bool readIndices(const std::uint8_t *&p, const std::uint8_t *pe, std::vector<std::uint32_t> &indices,
    std::uint32_t vertexCount, std::string &error)
{
    std::uint32_t indexCount = 0;
    if (!readU32(p, pe, indexCount) || indexCount == 0 || (indexCount % 3) != 0 || indexCount / 3 > (1u << 24))
    {
        error = "unsupported RTM index count";
        return false;
    }
    indices.resize(indexCount);
    for (std::uint32_t i = 0; i < indexCount; ++i)
    {
        if (!readU32(p, pe, indices[i]) || indices[i] >= vertexCount)
        {
            error = "invalid RTM index";
            return false;
        }
    }
    return true;
}

constexpr std::uint32_t kRtmMaxString = 65535;

void appendString(std::vector<std::uint8_t> &payload, const std::string &text)
{
    appendU32(payload, static_cast<std::uint32_t>(text.size()));
    payload.insert(payload.end(), text.begin(), text.end());
}

bool readString(const std::uint8_t *&p, const std::uint8_t *pe, std::string &text, std::string &error)
{
    std::uint32_t length = 0;
    if (!readU32(p, pe, length) || length > kRtmMaxString
        || static_cast<std::size_t>(pe - p) < length)
    {
        error = "invalid RTM string";
        return false;
    }
    text.assign(reinterpret_cast<const char *>(p), length);
    p += length;
    return true;
}

bool appendMaterials(std::vector<std::uint8_t> &payload, const MeshGeometry &geometry, std::string &error)
{
    if (geometry.materials.size() > 1024 || geometry.submeshes.size() > (1u << 16))
    {
        error = "too many materials or submeshes";
        return false;
    }
    appendU32(payload, static_cast<std::uint32_t>(geometry.materials.size()));
    for (const MeshMaterialRef &material : geometry.materials)
    {
        appendString(payload, material.name);
        appendString(payload, material.albedoMap);
        appendString(payload, material.normalMap);
        appendF32(payload, static_cast<float>(material.albedo.x));
        appendF32(payload, static_cast<float>(material.albedo.y));
        appendF32(payload, static_cast<float>(material.albedo.z));
    }
    appendU32(payload, static_cast<std::uint32_t>(geometry.submeshes.size()));
    const std::uint32_t indexCount = static_cast<std::uint32_t>(geometry.indices.size());
    for (const MeshSubmesh &submesh : geometry.submeshes)
    {
        if (submesh.materialIndex >= geometry.materials.size() && !geometry.materials.empty())
        {
            error = "submesh material out of range";
            return false;
        }
        if (submesh.indexOffset + submesh.indexCount > indexCount || (submesh.indexCount % 3) != 0)
        {
            error = "invalid submesh range";
            return false;
        }
        appendU32(payload, submesh.materialIndex);
        appendU32(payload, submesh.indexOffset);
        appendU32(payload, submesh.indexCount);
    }
    return true;
}

bool readMaterials(const std::uint8_t *&p, const std::uint8_t *pe, MeshGeometry &geometry, std::string &error)
{
    std::uint32_t materialCount = 0;
    if (!readU32(p, pe, materialCount) || materialCount > 1024)
    {
        error = "invalid material count";
        return false;
    }
    geometry.materials.resize(materialCount);
    for (MeshMaterialRef &material : geometry.materials)
    {
        float ax = 0;
        float ay = 0;
        float az = 0;
        if (!readString(p, pe, material.name, error) || !readString(p, pe, material.albedoMap, error)
            || !readString(p, pe, material.normalMap, error) || !readF32(p, pe, ax) || !readF32(p, pe, ay)
            || !readF32(p, pe, az))
            return false;
        material.albedo = Vec3(ax, ay, az);
    }
    std::uint32_t submeshCount = 0;
    if (!readU32(p, pe, submeshCount) || submeshCount > (1u << 16))
    {
        error = "invalid submesh count";
        return false;
    }
    geometry.submeshes.resize(submeshCount);
    const std::uint32_t indexCount = static_cast<std::uint32_t>(geometry.indices.size());
    for (MeshSubmesh &submesh : geometry.submeshes)
    {
        if (!readU32(p, pe, submesh.materialIndex) || !readU32(p, pe, submesh.indexOffset)
            || !readU32(p, pe, submesh.indexCount))
        {
            error = "truncated submesh";
            return false;
        }
        if ((!geometry.materials.empty() && submesh.materialIndex >= geometry.materials.size())
            || submesh.indexOffset + submesh.indexCount > indexCount || (submesh.indexCount % 3) != 0)
        {
            error = "invalid submesh range";
            return false;
        }
    }
    return true;
}

// Legacy v1/v2 lod: vertexCount, indexCount, then attributes, then indices (often identity soup).
bool readLegacyLod(const std::uint8_t *&p, const std::uint8_t *pe, std::vector<Vec3> &positions,
    std::vector<Vec3> &normals, std::vector<float> &u, std::vector<float> &v, std::vector<std::uint32_t> &indices,
    std::string &error)
{
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    if (!readU32(p, pe, vertexCount) || !readU32(p, pe, indexCount) || vertexCount == 0 || indexCount == 0
        || (indexCount % 3) != 0 || indexCount / 3 > (1u << 24))
    {
        error = "unsupported RTM lod";
        return false;
    }
    positions.resize(vertexCount);
    normals.resize(vertexCount);
    u.resize(vertexCount);
    v.resize(vertexCount);
    for (std::uint32_t i = 0; i < vertexCount; ++i)
    {
        float x = 0;
        float y = 0;
        float z = 0;
        if (!readF32(p, pe, x) || !readF32(p, pe, y) || !readF32(p, pe, z))
        {
            error = "truncated RTM positions";
            return false;
        }
        positions[i] = Vec3(x, y, z);
    }
    for (std::uint32_t i = 0; i < vertexCount; ++i)
    {
        float x = 0;
        float y = 0;
        float z = 0;
        if (!readF32(p, pe, x) || !readF32(p, pe, y) || !readF32(p, pe, z))
        {
            error = "truncated RTM normals";
            return false;
        }
        normals[i] = Vec3(x, y, z);
    }
    for (std::uint32_t i = 0; i < vertexCount; ++i)
        if (!readF32(p, pe, u[i]) || !readF32(p, pe, v[i]))
        {
            error = "truncated RTM uvs";
            return false;
        }
    indices.resize(indexCount);
    for (std::uint32_t i = 0; i < indexCount; ++i)
        if (!readU32(p, pe, indices[i]) || indices[i] >= vertexCount)
        {
            error = "invalid RTM index";
            return false;
        }
    return true;
}

} // namespace

bool isRtmPath(const std::filesystem::path &path)
{
    std::string ext = path.extension().string();
    for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".rtm";
}

bool encodeMeshGeometry(const MeshGeometry &geometry, std::vector<std::uint8_t> &out, std::string &error)
{
    if (geometry.positions.empty() || geometry.indices.size() < 3)
    {
        error = "empty mesh";
        return false;
    }
    std::vector<std::uint8_t> payload;
    appendU32(payload, kRtmPayloadVersion);
    if (!appendVertices(payload, geometry.positions, geometry.normals, geometry.u, geometry.v, error))
        return false;
    const std::uint32_t vertexCount = static_cast<std::uint32_t>(geometry.positions.size());
    std::uint32_t lodCount = 1;
    for (const MeshLod &lod : geometry.extraLods)
    {
        if (lod.indices.empty())
            continue;
        // v3 stores shared-vertex LODs only (cooker). Own-vertex LODs are skipped.
        if (!lod.positions.empty())
            continue;
        ++lodCount;
    }
    appendU32(payload, lodCount);
    if (!appendIndices(payload, geometry.indices, vertexCount, error))
        return false;
    for (const MeshLod &lod : geometry.extraLods)
    {
        if (lod.indices.empty() || !lod.positions.empty())
            continue;
        if (!appendIndices(payload, lod.indices, vertexCount, error))
            return false;
    }
    if (!appendMaterials(payload, geometry, error))
        return false;

    const size_t bound = ZSTD_compressBound(payload.size());
    std::vector<std::uint8_t> packed(bound);
    const size_t packedSize = ZSTD_compress(packed.data(), packed.size(), payload.data(), payload.size(), 5);
    if (ZSTD_isError(packedSize))
    {
        error = std::string("zstd compress: ") + ZSTD_getErrorName(packedSize);
        return false;
    }
    packed.resize(packedSize);

    out.clear();
    out.insert(out.end(), kRtmMagic, kRtmMagic + 4);
    appendU32(out, static_cast<std::uint32_t>(payload.size()));
    appendU32(out, static_cast<std::uint32_t>(packed.size()));
    out.insert(out.end(), packed.begin(), packed.end());
    return true;
}

bool decodeMeshGeometry(const std::vector<std::uint8_t> &bytes, MeshGeometry &geometry, std::string &error)
{
    if (bytes.size() < 12 || std::memcmp(bytes.data(), kRtmMagic, 4) != 0)
    {
        error = "not an RTM1 mesh";
        return false;
    }
    const std::uint8_t *cursor = bytes.data() + 4;
    const std::uint8_t *end = bytes.data() + bytes.size();
    std::uint32_t payloadSize = 0;
    std::uint32_t packedSize = 0;
    if (!readU32(cursor, end, payloadSize) || !readU32(cursor, end, packedSize))
    {
        error = "truncated RTM header";
        return false;
    }
    if (packedSize != static_cast<std::uint32_t>(end - cursor) || payloadSize == 0 || payloadSize > (64u << 20))
    {
        error = "invalid RTM sizes";
        return false;
    }
    std::vector<std::uint8_t> payload(payloadSize);
    const size_t got = ZSTD_decompress(payload.data(), payload.size(), cursor, packedSize);
    if (ZSTD_isError(got) || got != payloadSize)
    {
        error = "zstd decompress failed";
        return false;
    }
    const std::uint8_t *p = payload.data();
    const std::uint8_t *pe = payload.data() + payload.size();
    std::uint32_t version = 0;
    if (!readU32(p, pe, version) || (version != 1 && version != 2 && version != 3 && version != 4))
    {
        error = "unsupported RTM payload";
        return false;
    }
    geometry = MeshGeometry();
    if (version == 3 || version == 4)
    {
        if (!readVertices(p, pe, geometry.positions, geometry.normals, geometry.u, geometry.v, error))
            return false;
        std::uint32_t lodCount = 0;
        if (!readU32(p, pe, lodCount) || lodCount == 0 || lodCount > 8)
        {
            error = "invalid lod count";
            return false;
        }
        const std::uint32_t vertexCount = static_cast<std::uint32_t>(geometry.positions.size());
        if (!readIndices(p, pe, geometry.indices, vertexCount, error))
            return false;
        geometry.extraLods.resize(lodCount - 1);
        for (std::uint32_t i = 1; i < lodCount; ++i)
        {
            if (!readIndices(p, pe, geometry.extraLods[i - 1].indices, vertexCount, error))
                return false;
        }
        if (version == 4 && !readMaterials(p, pe, geometry, error))
            return false;
    }
    else if (version == 1)
    {
        if (!readLegacyLod(p, pe, geometry.positions, geometry.normals, geometry.u, geometry.v, geometry.indices, error))
            return false;
    }
    else
    {
        std::uint32_t lodCount = 0;
        if (!readU32(p, pe, lodCount) || lodCount == 0 || lodCount > 8)
        {
            error = "invalid lod count";
            return false;
        }
        if (!readLegacyLod(p, pe, geometry.positions, geometry.normals, geometry.u, geometry.v, geometry.indices, error))
            return false;
        geometry.extraLods.resize(lodCount - 1);
        for (std::uint32_t i = 1; i < lodCount; ++i)
        {
            MeshLod &lod = geometry.extraLods[i - 1];
            if (!readLegacyLod(p, pe, lod.positions, lod.normals, lod.u, lod.v, lod.indices, error))
                return false;
        }
    }
    finishGeometry(geometry);
    return true;
}

bool writeRtmFile(const std::filesystem::path &path, const MeshGeometry &geometry, std::string &error)
{
    std::vector<std::uint8_t> bytes;
    if (!encodeMeshGeometry(geometry, bytes, error))
        return false;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        error = "could not write " + pathUtf8(path);
        return false;
    }
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

std::shared_ptr<MeshGeometry> loadRtmFile(const std::filesystem::path &path, std::string &error)
{
    std::vector<std::uint8_t> bytes;
    const std::string virtualPath = path.generic_string();
    if (contentExists(virtualPath) || contentExists(cookedMeshVirtualPath(path)))
    {
        const std::string use = contentExists(virtualPath) ? virtualPath : cookedMeshVirtualPath(path);
        if (!contentRead(use, bytes, error))
            return nullptr;
    }
    else
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            error = "could not open " + pathUtf8(path);
            return nullptr;
        }
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    auto geometry = std::make_shared<MeshGeometry>();
    if (!decodeMeshGeometry(bytes, *geometry, error))
        return nullptr;
    geometry->sourcePath = pathUtf8(path);
    finishGeometry(*geometry);
    return geometry;
}
