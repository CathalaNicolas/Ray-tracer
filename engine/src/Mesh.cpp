#include "Mesh.hpp"

#include "Collision.hpp"
#include "GpuContribute.hpp"
#include "SceneWrite.hpp"

#include <fbxsdk.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace
{

Vec3 min3(const Vec3 &a, const Vec3 &b)
{
    return Vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
}

Vec3 max3(const Vec3 &a, const Vec3 &b)
{
    return Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
}

Vec3 centroid(const MeshTri &triangle)
{
    return (triangle.position[0] + triangle.position[1] + triangle.position[2]) / 3.0;
}

double axisOf(const Vec3 &value, int axis)
{
    if (axis == 0)
        return value.x;
    if (axis == 1)
        return value.y;
    return value.z;
}

int buildRange(std::vector<MeshTri> &triangles, std::vector<BvhNode> &nodes, int begin, int end)
{
    const int self = static_cast<int>(nodes.size());
    nodes.push_back({});

    Vec3 boundsMin = triangles[static_cast<size_t>(begin)].position[0];
    Vec3 boundsMax = boundsMin;
    for (int index = begin; index < end; ++index)
    {
        for (int corner = 0; corner < 3; ++corner)
        {
            boundsMin = min3(boundsMin, triangles[static_cast<size_t>(index)].position[corner]);
            boundsMax = max3(boundsMax, triangles[static_cast<size_t>(index)].position[corner]);
        }
    }
    const Vec3 pad(1e-4, 1e-4, 1e-4);
    nodes[static_cast<size_t>(self)].boundsMin = boundsMin - pad;
    nodes[static_cast<size_t>(self)].boundsMax = boundsMax + pad;

    const int count = end - begin;
    if (count <= 4)
    {
        nodes[static_cast<size_t>(self)].left = begin;
        nodes[static_cast<size_t>(self)].right = -count;
        return self;
    }

    const Vec3 extent = boundsMax - boundsMin;
    int axis = 0;
    if (extent.y >= extent.x && extent.y >= extent.z)
        axis = 1;
    else if (extent.z >= extent.x && extent.z >= extent.y)
        axis = 2;
    const int mid = begin + count / 2;
    std::nth_element(
        triangles.begin() + begin,
        triangles.begin() + mid,
        triangles.begin() + end,
        [axis](const MeshTri &a, const MeshTri &b) {
            return axisOf(centroid(a), axis) < axisOf(centroid(b), axis);
        });

    const int left = buildRange(triangles, nodes, begin, mid);
    const int right = buildRange(triangles, nodes, mid, end);
    nodes[static_cast<size_t>(self)].left = left;
    nodes[static_cast<size_t>(self)].right = right;
    return self;
}

bool slab(const Vec3 &origin, const Vec3 &direction, const BvhNode &node, double tMin, double tMax)
{
    const double originAxis[3] = {origin.x, origin.y, origin.z};
    const double directionAxis[3] = {direction.x, direction.y, direction.z};
    const double minAxis[3] = {node.boundsMin.x, node.boundsMin.y, node.boundsMin.z};
    const double maxAxis[3] = {node.boundsMax.x, node.boundsMax.y, node.boundsMax.z};
    double enter = tMin;
    double exit = tMax;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(directionAxis[axis]) < 1e-12)
        {
            if (originAxis[axis] < minAxis[axis] || originAxis[axis] > maxAxis[axis])
                return false;
            continue;
        }
        const double inverse = 1.0 / directionAxis[axis];
        double t1 = (minAxis[axis] - originAxis[axis]) * inverse;
        double t2 = (maxAxis[axis] - originAxis[axis]) * inverse;
        if (t1 > t2)
            std::swap(t1, t2);
        enter = std::max(enter, t1);
        exit = std::min(exit, t2);
        if (enter > exit)
            return false;
    }
    return true;
}

bool hitTriangle(const MeshTri &triangle, const Ray &ray, const Vec3 &origin, const Vec3 &direction, double tMin, double tMax, HitRecord &hit)
{
    const Vec3 edge1 = triangle.position[1] - triangle.position[0];
    const Vec3 edge2 = triangle.position[2] - triangle.position[0];
    const Vec3 p = cross(direction, edge2);
    const double determinant = dot(edge1, p);
    if (std::abs(determinant) < 1e-8)
        return false;
    const double inverse = 1.0 / determinant;
    const Vec3 s = origin - triangle.position[0];
    const double u = dot(s, p) * inverse;
    if (u < 0 || u > 1)
        return false;
    const Vec3 q = cross(s, edge1);
    const double v = dot(direction, q) * inverse;
    if (v < 0 || u + v > 1)
        return false;
    const double t = dot(edge2, q) * inverse;
    if (t < tMin || t > tMax)
        return false;

    const double w = 1.0 - u - v;
    Vec3 normal = triangle.normal[0] * w + triangle.normal[1] * u + triangle.normal[2] * v;
    if (length(normal) <= 1e-8)
        normal = cross(edge1, edge2);
    normal = normalize(normal);
    if (dot(direction, normal) > 0)
        normal = -normal;

    hit.t = t;
    hit.point = ray.at(t);
    hit.normal = normal;
    return true;
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

void appendFbxMesh(FbxNode *node, std::vector<MeshTri> &triangles)
{
    FbxMesh *mesh = node->GetMesh();
    if (mesh != nullptr)
    {
        const FbxAMatrix global = node->EvaluateGlobalTransform();
        const FbxAMatrix normalMatrix = global.Inverse().Transpose();
        const bool hasUv = mesh->GetElementUVCount() > 0;
        const FbxString uvName = hasUv ? mesh->GetElementUV(0)->GetName() : FbxString();
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

bool loadObj(const std::filesystem::path &path, std::vector<MeshTri> &triangles, std::string &error)
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

void finishGeometry(MeshGeometry &geometry)
{
    geometry.root = buildBvh(geometry.triangles, geometry.nodes);
    if (geometry.root < 0)
        return;
    geometry.boundsMin = geometry.nodes[static_cast<size_t>(geometry.root)].boundsMin;
    geometry.boundsMax = geometry.nodes[static_cast<size_t>(geometry.root)].boundsMax;
}

std::string geometryKey(const std::filesystem::path &path)
{
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    std::string key = (error ? path : canonical).u8string();
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

} // namespace

int buildBvh(std::vector<MeshTri> &triangles, std::vector<BvhNode> &nodes)
{
    nodes.clear();
    if (triangles.empty())
        return -1;
    return buildRange(triangles, nodes, 0, static_cast<int>(triangles.size()));
}

const std::vector<MeshTri> &Mesh::triangles() const
{
    static const std::vector<MeshTri> empty;
    if (!geometry_)
        return empty;
    return geometry_->triangles;
}

void Mesh::setTriangles(std::vector<MeshTri> triangles)
{
    auto geometry = std::make_shared<MeshGeometry>();
    geometry->triangles = std::move(triangles);
    finishGeometry(*geometry);
    geometry_ = std::move(geometry);
}

bool Mesh::load(const std::filesystem::path &path, std::string &error)
{
    const std::string key = geometryKey(path);
    auto &cache = geometryCache();
    {
        const auto found = cache.find(key);
        if (found != cache.end())
        {
            geometry_ = found->second;
            sourcePath_ = path.u8string();
            std::error_code stampError;
            const auto stamp = std::filesystem::last_write_time(path, stampError);
            sourceStamp_ = stampError ? 0 : static_cast<std::int64_t>(stamp.time_since_epoch().count());
            sourceBytes_ = stampError ? 0 : std::filesystem::file_size(path, stampError);
            return true;
        }
    }

    std::string extension = path.extension().string();
    for (char &character : extension)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

    std::vector<MeshTri> triangles;
    const bool loaded = extension == ".fbx" ? loadFbx(path, triangles, error) : loadObj(path, triangles, error);
    if (!loaded)
        return false;

    fitToGround(triangles);
    auto geometry = std::make_shared<MeshGeometry>();
    geometry->triangles = std::move(triangles);
    geometry->sourcePath = path.u8string();
    finishGeometry(*geometry);
    sourcePath_ = geometry->sourcePath;
    geometry_ = std::move(geometry);
    cache.emplace(key, geometry_);
    eraseOlderGeometry(cache, key);
    std::error_code stampError;
    const auto stamp = std::filesystem::last_write_time(path, stampError);
    sourceStamp_ = stampError ? 0 : static_cast<std::int64_t>(stamp.time_since_epoch().count());
    sourceBytes_ = stampError ? 0 : std::filesystem::file_size(path, stampError);
    return true;
}

bool Mesh::refreshFromDisk(std::string &error)
{
    if (sourcePath_.empty())
        return false;
    const std::filesystem::path path = std::filesystem::u8path(sourcePath_);
    std::error_code stampError;
    const auto stamp = std::filesystem::last_write_time(path, stampError);
    const auto bytes = std::filesystem::file_size(path, stampError);
    if (stampError)
        return false;
    const auto ticks = static_cast<std::int64_t>(stamp.time_since_epoch().count());
    if (ticks == sourceStamp_ && bytes == sourceBytes_)
        return false;
    return load(path, error);
}

ParentFrame Hittable::parentFrame() const
{
    if (owner_ == nullptr || parentId == 0)
        return ParentFrame{};

    const std::uint32_t epoch = owner_->parentFrameEpoch();
    if (parentFrameStamp_ == epoch)
        return parentFrameCache_;

    ParentFrame frame;
    const Hittable *chain[64];
    int count = 0;
    int seen[64];
    int seenCount = 0;
    seen[seenCount++] = id;
    int next = parentId;
    while (next != 0 && count < 64 && seenCount < 64)
    {
        bool repeats = false;
        for (int index = 0; index < seenCount; ++index)
        {
            if (seen[index] == next)
            {
                repeats = true;
                break;
            }
        }
        if (repeats)
            break;
        seen[seenCount++] = next;
        const Hittable *parent = owner_->find(next);
        if (parent == nullptr)
            break;
        chain[count++] = parent;
        next = parent->parentId;
    }

    for (int index = count - 1; index >= 0; --index)
    {
        const Hittable *parent = chain[index];
        frame.origin = frame.point(parent->localPosition());
        parent->applyParentAxes(frame);
    }
    parentFrameCache_ = frame;
    parentFrameStamp_ = epoch;
    return frame;
}

void Hittable::notifyTransformChanged() const
{
    if (owner_ != nullptr)
        owner_->bumpParentFrames();
}

void Mesh::applyParentAxes(ParentFrame &frame) const
{
    const Vec3 turnedX = frame.direction(meshRotate(Vec3(1, 0, 0), rotation_));
    const Vec3 turnedY = frame.direction(meshRotate(Vec3(0, 1, 0), rotation_));
    const Vec3 turnedZ = frame.direction(meshRotate(Vec3(0, 0, 1), rotation_));
    frame.axisX = turnedX;
    frame.axisY = turnedY;
    frame.axisZ = turnedZ;
    frame.scale *= scale_;
}

void Mesh::worldAxes(Vec3 &axisX, Vec3 &axisY, Vec3 &axisZ) const
{
    const ParentFrame frame = parentFrame();
    axisX = frame.direction(meshRotate(Vec3(1, 0, 0), rotation_));
    axisY = frame.direction(meshRotate(Vec3(0, 1, 0), rotation_));
    axisZ = frame.direction(meshRotate(Vec3(0, 0, 1), rotation_));
}

bool Mesh::intersect(const Ray &ray, double tMin, double tMax, HitRecord &hit) const
{
    const double placedScale = worldScale();
    if (!geometry_ || geometry_->root < 0 || geometry_->triangles.empty() || placedScale <= 1e-8)
        return false;

    Vec3 axisX;
    Vec3 axisY;
    Vec3 axisZ;
    worldAxes(axisX, axisY, axisZ);
    const Vec3 delta = ray.origin - position();
    Vec3 origin(dot(delta, axisX), dot(delta, axisY), dot(delta, axisZ));
    Vec3 direction(dot(ray.direction, axisX), dot(ray.direction, axisY), dot(ray.direction, axisZ));
    origin = origin / placedScale;
    direction = direction / placedScale;
    int stack[64];
    int top = 0;
    stack[top++] = geometry_->root;
    bool found = false;
    double closest = tMax;

    while (top > 0)
    {
        const BvhNode &node = geometry_->nodes[static_cast<size_t>(stack[--top])];
        if (!slab(origin, direction, node, tMin, closest))
            continue;
        if (node.right < 0)
        {
            const int count = -node.right;
            for (int index = 0; index < count; ++index)
            {
                HitRecord candidate;
                if (hitTriangle(geometry_->triangles[static_cast<size_t>(node.left + index)], ray, origin, direction, tMin, closest, candidate))
                {
                    found = true;
                    closest = candidate.t;
                    hit = candidate;
                }
            }
        }
        else if (top < 62)
        {
            stack[top++] = node.left;
            stack[top++] = node.right;
        }
    }

    if (!found)
        return false;
    hit.normal = axisX * hit.normal.x + axisY * hit.normal.y + axisZ * hit.normal.z;
    if (length(hit.normal) > 1e-8)
        hit.normal = normalize(hit.normal);
    hit.material = material_;
    return true;
}

Hit Mesh::contactSphere(const Vec3 &center, double radius) const
{
    const double placedScale = worldScale();
    if (!geometry_ || geometry_->root < 0 || placedScale <= 1e-8)
        return {};
    Vec3 axisX;
    Vec3 axisY;
    Vec3 axisZ;
    worldAxes(axisX, axisY, axisZ);
    const Vec3 delta = center - position();
    const Vec3 localCenter(dot(delta, axisX), dot(delta, axisY), dot(delta, axisZ));
    Hit hit = sphereHitMesh(localCenter / placedScale, radius / placedScale, *geometry_);
    if (!hit.hit)
        return hit;
    hit.normal = axisX * hit.normal.x + axisY * hit.normal.y + axisZ * hit.normal.z;
    if (length(hit.normal) > 1e-8)
        hit.normal = normalize(hit.normal);
    hit.point = position() + (axisX * hit.point.x + axisY * hit.point.y + axisZ * hit.point.z) * placedScale;
    hit.penetration = static_cast<float>(static_cast<double>(hit.penetration) * scale_);
    return hit;
}

bool Mesh::blocksPlayer(int playerId) const
{
    if (rolePassThrough(playerId))
        return false;
    ensureRole();
    if (role)
        return role->meshBlocks();
    return tag == "solid" || tag == "platform" || tag.empty();
}

ColliderSketch Mesh::colliderSketch() const
{
    ColliderSketch sketch;
    sketch.center = position();
    if (!geometry_ || geometry_->root < 0)
    {
        sketch.kind = ColliderSketch::Kind::MeshPoint;
        return sketch;
    }
    sketch.kind = ColliderSketch::Kind::MeshBox;
    worldAxes(sketch.axisX, sketch.axisY, sketch.axisZ);
    sketch.localMin = geometry_->boundsMin;
    sketch.localMax = geometry_->boundsMax;
    sketch.radius = worldScale();
    return sketch;
}

void Mesh::writeScene(std::ostream &out) const
{
    out << "mesh \"" << scene_write::escapeName(name) << "\" "
        << position_.x << ' ' << position_.y << ' ' << position_.z << ' '
        << scale_ << ' ';
    scene_write::writeMaterial(out, material_);
    if (meshRotationActive(rotation_))
        out << " rot " << rotation_.x << ' ' << rotation_.y << ' ' << rotation_.z;
    std::string extension;
    if (sourcePath_.size() >= 4)
        extension = sourcePath_.substr(sourcePath_.size() - 4);
    for (char &character : extension)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    out << (extension == ".fbx" ? " fbx \"" : " obj \"") << scene_write::escapeName(sourcePath_) << '"';
    scene_write::writeTag(out, tag);
    scene_write::writeMotion(out, *this);
    scene_write::writeAction(out, *this);
    scene_write::writeParent(out, *this);
    out << '\n';
}

void Mesh::contributeGpu(gpu_detail::GpuContribute &sink) const
{
    sink.mesh(*this);
}

bool Mesh::copyShapeFrom(const Hittable &source)
{
    const auto *from = dynamic_cast<const Mesh *>(&source);
    if (from == nullptr)
        return false;
    scale_ = from->scale_;
    rotation_ = from->rotation_;
    if (!from->sourcePath_.empty() && sourcePath_ != from->sourcePath_)
    {
        std::string error;
        load(from->sourcePath_, error);
    }
    return true;
}

void Mesh::mixShapeHash(std::uint64_t &hash) const
{
    auto mixBits = [&](std::uint64_t value) {
        hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    };
    auto mixDouble = [&](double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        mixBits(bits);
    };
    auto mixVec = [&](const Vec3 &value) {
        mixDouble(value.x);
        mixDouble(value.y);
        mixDouble(value.z);
    };
    auto mixText = [&](const std::string &text) {
        for (unsigned char character : text)
            mixBits(character);
    };
    mixBits(3);
    mixVec(position());
    mixDouble(worldScale());
    mixVec(rotation_);
    mixDouble(scale_);
    mixText(sourcePath_);
    Vec3 axisX;
    Vec3 axisY;
    Vec3 axisZ;
    worldAxes(axisX, axisY, axisZ);
    mixVec(axisX);
    mixVec(axisY);
    mixVec(axisZ);
    const MeshGeometry *geometry = geometry_.get();
    mixBits(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(geometry)));
    if (geometry != nullptr)
    {
        mixBits(geometry->triangles.size());
        mixVec(geometry->boundsMin);
        mixVec(geometry->boundsMax);
    }
}

std::unique_ptr<Hittable> Mesh::clone() const
{
    auto copy = std::make_unique<Mesh>(*this);
    copyBaseTo(*copy);
    copy->bindScene(nullptr);
    return copy;
}
