#include "SceneDebug.hpp"

#include "Camera.hpp"
#include "Constants.hpp"
#include "Mesh.hpp"
#include "Plane.hpp"
#include "Scene.hpp"
#include "Ray.hpp"
#include "Sphere.hpp"
#include "GpuRenderDetail.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace
{

void worldBox(const Mesh &mesh, Vec3 &boundsMin, Vec3 &boundsMax)
{
    boundsMin = Vec3(1e30, 1e30, 1e30);
    boundsMax = Vec3(-1e30, -1e30, -1e30);
    const MeshGeometry *geometry = mesh.geometry().get();
    if (geometry == nullptr || geometry->triangles.empty())
        return;
    Vec3 axisX;
    Vec3 axisY;
    Vec3 axisZ;
    mesh.worldAxes(axisX, axisY, axisZ);
    const double worldScale = mesh.worldScale();
    for (int corner = 0; corner < 8; ++corner)
    {
        const Vec3 local(
            (corner & 1) != 0 ? geometry->boundsMax.x : geometry->boundsMin.x,
            (corner & 2) != 0 ? geometry->boundsMax.y : geometry->boundsMin.y,
            (corner & 4) != 0 ? geometry->boundsMax.z : geometry->boundsMin.z);
        const Vec3 world = mesh.position() + (axisX * local.x + axisY * local.y + axisZ * local.z) * worldScale;
        boundsMin.x = std::min(boundsMin.x, world.x);
        boundsMin.y = std::min(boundsMin.y, world.y);
        boundsMin.z = std::min(boundsMin.z, world.z);
        boundsMax.x = std::max(boundsMax.x, world.x);
        boundsMax.y = std::max(boundsMax.y, world.y);
        boundsMax.z = std::max(boundsMax.z, world.z);
    }
    boundsMin = boundsMin - Vec3(1e-3, 1e-3, 1e-3);
    boundsMax = boundsMax + Vec3(1e-3, 1e-3, 1e-3);
}

std::string vecText(const Vec3 &value)
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(4);
    out << value.x << " " << value.y << " " << value.z;
    return out.str();
}

void writeMaterial(std::ostringstream &out, const Material &material)
{
    out << "  material albedo " << vecText(material.albedo)
        << " ambient " << material.ambient
        << " diffuse " << material.diffuse
        << " specular " << material.specular
        << " shininess " << material.shininess
        << " reflect " << material.reflectivity
        << " transmit " << material.transmission
        << " ior " << material.ior
        << " emission " << material.emission
        << " rough " << material.roughness
        << " uv " << material.uvScale << " " << material.uvScrollU << " " << material.uvScrollV;
    if (!material.albedoMap.empty())
        out << " albedoMap \"" << material.albedoMap << "\"";
    if (!material.normalMap.empty())
        out << " normalMap \"" << material.normalMap << "\"";
    out << "\n";
}

} // namespace

namespace
{

struct BounceLight
{
    Vec3 position;
    Vec3 color;
    double intensity = 1;
    double falloff = 0.2;
    int emitterId = 0;
    bool directional = false;
    std::string name;
};

void gatherBounceLights(const Scene &scene, std::vector<BounceLight> &lights)
{
    for (const PointLight &light : scene.lights())
    {
        if (static_cast<int>(lights.size()) >= 8)
            return;
        BounceLight item;
        item.position = light.position;
        item.color = light.color;
        item.intensity = light.intensity;
        item.falloff = light.falloff;
        item.directional = light.directional;
        item.name = light.name.empty() ? "light" : light.name;
        lights.push_back(item);
    }
    for (const auto &object : scene.objects())
    {
        if (static_cast<int>(lights.size()) >= 8)
            return;
        const Material material = object->material();
        if (material.emission <= 0.01)
            continue;
        BounceLight item;
        item.color = material.albedo;
        item.intensity = material.emission;
        item.falloff = 0.2;
        item.emitterId = object->id;
        item.name = object->name;
        item.position = object->worldPosition();
        lights.push_back(item);
    }
}

const char *bounceBlocker(const Scene &scene, const Vec3 &from, const Vec3 &to, int skipId, int skipMirror)
{
    const Vec3 delta = to - from;
    const double dist = length(delta);
    if (dist <= 1e-4)
        return "";
    const Vec3 dir = delta / dist;
    const Ray ray(from + dir * 1e-4, dir);
    double nearest = dist - 1e-4;
    const char *name = "";
    for (const auto &object : scene.objects())
    {
        if (object->id == skipId || object->id == skipMirror)
            continue;
        HitRecord hit;
        if (object->intersect(ray, 1e-4, nearest, hit))
        {
            nearest = hit.t;
            name = object->name.c_str();
        }
    }
    return name;
}

} // namespace

void mirrorBounceDebug(const Scene &scene, std::string &text, std::vector<BounceRay> &rays)
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(4);
    std::vector<BounceLight> lights;
    gatherBounceLights(scene, lights);
    const Plane *floor = nullptr;
    for (const auto &object : scene.objects())
    {
        if (const auto *plane = dynamic_cast<const Plane *>(object.get()))
        {
            floor = plane;
            break;
        }
    }
    out << "mirror bounce\n";
    int mirrors = 0;
    for (const auto &object : scene.objects())
    {
        const auto *sphere = dynamic_cast<const Sphere *>(object.get());
        if (sphere == nullptr)
            continue;
        const Material material = sphere->material();
        if (material.reflectivity <= 0.35 || material.transmission > 0.001)
            continue;
        if (mirrors >= 8)
            break;
        ++mirrors;
        const Vec3 center = sphere->center();
        const double radius = sphere->worldRadius();
        out << "mirror \"" << sphere->name << "\" id " << sphere->id
            << " center " << vecText(center) << " radius " << radius
            << " reflect " << material.reflectivity
            << " albedo " << vecText(material.albedo) << "\n";
        struct Receiver
        {
            std::string name;
            Vec3 point;
            Vec3 normal;
            Vec3 albedo;
            double diffuse = 1;
            int id = 0;
        };
        std::vector<Receiver> receivers;
        if (floor != nullptr)
        {
            const Vec3 normal = normalize(floor->normal());
            const double y = floor->point().y;
            const double xs[3] = {center.x - 1.2, center.x, center.x + 1.2};
            const double zs[2] = {center.z - 1.2, center.z + 1.2};
            for (double x : xs)
                for (double z : zs)
                {
                    Receiver receiver;
                    receiver.name = floor->name;
                    receiver.point = Vec3(x, y, z) + normal * 0.001;
                    receiver.normal = normal;
                    receiver.albedo = floor->material().albedo;
                    receiver.diffuse = floor->material().diffuse;
                    receiver.id = floor->id;
                    receivers.push_back(receiver);
                }
        }
        int sphereReceivers = 0;
        for (const auto &other : scene.objects())
        {
            if (sphereReceivers >= 6)
                break;
            const auto *target = dynamic_cast<const Sphere *>(other.get());
            if (target == nullptr || target->id == sphere->id)
                continue;
            const Vec3 delta = target->center() - center;
            const double dist = length(delta);
            if (dist <= 1e-6)
                continue;
            Receiver receiver;
            receiver.name = target->name;
            receiver.normal = delta / dist * -1.0;
            receiver.point = target->center() + receiver.normal * target->worldRadius();
            receiver.albedo = target->material().albedo;
            receiver.diffuse = target->material().diffuse;
            receiver.id = target->id;
            receivers.push_back(receiver);
            ++sphereReceivers;
        }
        for (const BounceLight &light : lights)
        {
            if (light.emitterId > 0 && light.emitterId == sphere->id)
            {
                out << "  light \"" << light.name << "\" skipped self emission\n";
                continue;
            }
            out << "  light \"" << light.name << "\" pos " << vecText(light.position)
                << " color " << vecText(light.color)
                << " intensity " << light.intensity << "\n";
            for (const Receiver &receiver : receivers)
            {
                const Vec3 fromCenter = receiver.point - center;
                if (dot(fromCenter, fromCenter) <= radius * radius)
                {
                    out << "    " << receiver.name << " inside\n";
                    continue;
                }
                const Vec3 lightDirIn = light.directional ? normalize(light.position) : normalize(light.position - center);
                Vec3 n = normalize(normalize(fromCenter) + lightDirIn);
                const char *fail = "";
                for (int step = 0; step < 8; ++step)
                {
                    const Vec3 q = n * radius;
                    Vec3 toPoint = fromCenter - q;
                    const Vec3 toLight = light.directional ? lightDirIn : (light.position - center) - q;
                    const double pointLen = length(toPoint);
                    const double lightLen = light.directional ? 1.0 : length(toLight);
                    if (pointLen <= 1e-4 || lightLen <= 1e-4)
                    {
                        fail = "degenerate";
                        break;
                    }
                    toPoint = toPoint / pointLen;
                    const Vec3 toLightN = toLight / lightLen;
                    const Vec3 bisector = toPoint + toLightN;
                    if (dot(bisector, bisector) <= 1e-6)
                    {
                        fail = "opposite";
                        break;
                    }
                    n = normalize(bisector);
                }
                if (fail[0] != '\0')
                {
                    out << "    " << receiver.name << " " << fail << "\n";
                    continue;
                }
                const Vec3 Q = center + n * radius;
                Vec3 toPoint = receiver.point - Q;
                const double pointDist = length(toPoint);
                if (pointDist <= 1e-4)
                {
                    out << "    " << receiver.name << " on surface\n";
                    continue;
                }
                toPoint = toPoint / pointDist;
                const Vec3 incident = light.directional ? lightDirIn * -1.0 : normalize(Q - light.position);
                const Vec3 reflected = incident - n * (2.0 * dot(n, incident));
                const double align = dot(reflected, toPoint);
                const double cosIn = std::max(dot(n, incident * -1.0), 0.0);
                const double nDotL = dot(receiver.normal, toPoint * -1.0);
                double attenuation = light.intensity * cosIn;
                if (!light.directional)
                    attenuation = light.intensity * cosIn / (1.0 + light.falloff * pointDist * pointDist);
                const Vec3 color = material.albedo * material.reflectivity * receiver.albedo * receiver.diffuse * light.color * attenuation * std::max(nDotL, 0.0);
                const char *blocker = "";
                if (align >= 0.995 && nDotL > 0.0 && attenuation * material.reflectivity > 0.002)
                {
                    blocker = bounceBlocker(scene, receiver.point, Q, receiver.id, sphere->id);
                    if (blocker[0] == '\0' && !light.directional)
                        blocker = bounceBlocker(scene, Q, light.position, sphere->id, light.emitterId);
                }
                const bool kept = align >= 0.995 && nDotL > 0.0 && attenuation * material.reflectivity > 0.002 && blocker[0] == '\0';
                const char *reason = kept ? "kept" : (align < 0.995 ? "align" : (nDotL <= 0.0 ? "faces away" : (attenuation * material.reflectivity <= 0.002 ? "dim" : "blocked")));
                out << "    " << receiver.name << " " << reason
                    << " Q " << vecText(Q)
                    << " align " << align
                    << " nDotL " << nDotL
                    << " att " << attenuation
                    << " rgb " << vecText(color);
                if (blocker[0] != '\0')
                    out << " by \"" << blocker << "\"";
                out << "\n";
                const double shown = color.x + color.y + color.z;
                if (shown > 0.02)
                {
                    BounceRay leg;
                    leg.from = light.directional ? Q + lightDirIn * 1.5 : light.position;
                    leg.to = Q;
                    leg.kept = kept;
                    rays.push_back(leg);
                    leg.from = Q;
                    leg.to = receiver.point;
                    rays.push_back(leg);
                }
            }
        }
    }
    if (mirrors == 0)
        out << "  none\n";
    text += out.str();
}

PointShadowFit fitPointShadow(const Vec3 &eye, const Vec3 &center, double radius, const std::vector<float> &meshMin, const std::vector<float> &meshMax, int meshCount)
{
    PointShadowFit fit;
    fit.target = center;
    double nearest = 1e9;
    Vec3 aim(0, 0, 0);
    int aimCount = 0;
    std::vector<Vec3> boxCenters(static_cast<size_t>(std::max(meshCount, 0)));
    std::vector<double> boxDist(static_cast<size_t>(std::max(meshCount, 0)), 1e9);
    for (int mesh = 0; mesh < meshCount; ++mesh)
    {
        const size_t slot = static_cast<size_t>(mesh) * 4;
        if (slot + 2 >= meshMin.size() || slot + 2 >= meshMax.size())
            break;
        const Vec3 boxMin(meshMin[slot], meshMin[slot + 1], meshMin[slot + 2]);
        const Vec3 boxMax(meshMax[slot], meshMax[slot + 1], meshMax[slot + 2]);
        const Vec3 boxCenter = (boxMin + boxMax) * 0.5;
        const double dist = length(boxCenter - eye);
        boxCenters[static_cast<size_t>(mesh)] = boxCenter;
        boxDist[static_cast<size_t>(mesh)] = dist;
        if (dist > 1e-4 && dist < nearest)
        {
            nearest = dist;
            fit.nearestMesh = mesh;
        }
    }
    const bool closeLight = nearest < 3.0;
    for (int mesh = 0; mesh < meshCount; ++mesh)
    {
        const double dist = boxDist[static_cast<size_t>(mesh)];
        if (dist <= 1e-4)
            continue;
        if (closeLight && dist > 3.0)
            continue;
        aim = aim + boxCenters[static_cast<size_t>(mesh)];
        ++aimCount;
    }
    if (aimCount > 0)
        fit.target = aim / static_cast<double>(aimCount);
    if (length(fit.target - eye) < 1e-4 && fit.nearestMesh >= 0)
        fit.target = boxCenters[static_cast<size_t>(fit.nearestMesh)];
    if (length(fit.target - eye) < 1e-4)
        fit.target = eye + Vec3(0, 0, -1);
    Vec3 forward = normalize(fit.target - eye);
    Vec3 up = std::abs(forward.y) > 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    const Vec3 side = normalize(cross(forward, up));
    up = cross(side, forward);
    fit.up = up;
    double minDepth = 1e9;
    double maxDepth = 0.05;
    double slope = 0.05;
    bool any = false;
    for (int mesh = 0; mesh < meshCount; ++mesh)
    {
        if (closeLight && boxDist[static_cast<size_t>(mesh)] > 3.0)
            continue;
        const size_t slot = static_cast<size_t>(mesh) * 4;
        if (slot + 2 >= meshMin.size() || slot + 2 >= meshMax.size())
            break;
        const double xs[2] = {meshMin[slot], meshMax[slot]};
        const double ys[2] = {meshMin[slot + 1], meshMax[slot + 1]};
        const double zs[2] = {meshMin[slot + 2], meshMax[slot + 2]};
        for (double x : xs)
        {
            for (double y : ys)
            {
                for (double z : zs)
                {
                    const Vec3 delta = Vec3(x, y, z) - eye;
                    const double depth = dot(delta, forward);
                    if (depth <= 0.02)
                        continue;
                    any = true;
                    minDepth = std::min(minDepth, depth);
                    maxDepth = std::max(maxDepth, depth);
                    slope = std::max(slope, std::abs(dot(delta, side)) / depth);
                    slope = std::max(slope, std::abs(dot(delta, up)) / depth);
                }
            }
        }
    }
    if (!any)
    {
        const double dist = std::max(length(center - eye), 0.05);
        fit.target = center;
        fit.fov = 2.0 * std::atan(radius / dist);
        if (fit.fov > 2.6)
            fit.fov = 2.6;
        if (fit.fov < 0.05)
            fit.fov = 0.05;
        fit.nearPlane = std::max(0.05, dist - radius);
        fit.farPlane = dist + radius + 2.0;
        if (fit.nearPlane >= fit.farPlane)
            fit.farPlane = fit.nearPlane + 1.0;
        Vec3 toCenter = center - eye;
        fit.up = std::abs(toCenter.y) > 0.9 * dist ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
        fit.fromCorners = false;
        return fit;
    }
    fit.fromCorners = true;
    fit.fov = 2.0 * std::atan(slope * 1.2);
    if (fit.fov > 3.0)
        fit.fov = 3.0;
    if (fit.fov < 0.05)
        fit.fov = 0.05;
    fit.nearPlane = std::max(0.02, minDepth * 0.5);
    fit.farPlane = std::max(maxDepth * 1.25 + 0.5, 40.0);
    if (fit.nearPlane >= fit.farPlane)
        fit.farPlane = fit.nearPlane + 1.0;
    return fit;
}

std::string sceneDebugText(const Scene &scene, const Camera &camera, const Vec3 &lookFrom, const Vec3 &lookAt, double fovDegrees, double aperture, double focusDistance, int width, int height, int samples, int depth, int selectedObject, int selectedLight)
{
    std::vector<const Mesh *> meshes;
    std::vector<float> meshMin;
    std::vector<float> meshMax;
    Vec3 boundsMin(1e9, 1e9, 1e9);
    Vec3 boundsMax(-1e9, -1e9, -1e9);
    for (const auto &object : scene.objects())
    {
        const auto *mesh = dynamic_cast<const Mesh *>(object.get());
        if (mesh == nullptr || mesh->triangles().empty())
            continue;
        Vec3 boxMin;
        Vec3 boxMax;
        worldBox(*mesh, boxMin, boxMax);
        meshes.push_back(mesh);
        meshMin.push_back(static_cast<float>(boxMin.x));
        meshMin.push_back(static_cast<float>(boxMin.y));
        meshMin.push_back(static_cast<float>(boxMin.z));
        meshMin.push_back(0.0f);
        meshMax.push_back(static_cast<float>(boxMax.x));
        meshMax.push_back(static_cast<float>(boxMax.y));
        meshMax.push_back(static_cast<float>(boxMax.z));
        meshMax.push_back(0.0f);
        boundsMin.x = std::min(boundsMin.x, boxMin.x);
        boundsMin.y = std::min(boundsMin.y, boxMin.y);
        boundsMin.z = std::min(boundsMin.z, boxMin.z);
        boundsMax.x = std::max(boundsMax.x, boxMax.x);
        boundsMax.y = std::max(boundsMax.y, boxMax.y);
        boundsMax.z = std::max(boundsMax.z, boxMax.z);
    }
    const Vec3 center = meshes.empty() ? Vec3(0, 0, 0) : (boundsMin + boundsMax) * 0.5;
    const double radius = meshes.empty() ? 0.5 : std::max(length(boundsMax - boundsMin) * 0.5, 0.5) * 1.8;

    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(4);
    out << "scene debug\n";
    out << "camera from " << vecText(lookFrom) << " at " << vecText(lookAt)
        << " fov " << fovDegrees << " aperture " << aperture << " focus " << focusDistance
        << " size " << width << " " << height << " samples " << samples << " bounces " << depth << "\n";
    out << "camera origin " << vecText(camera.origin()) << " view " << vecText(camera.viewDirection()) << "\n";
    out << "selected object " << selectedObject << " selected light " << selectedLight << "\n";
    out << "ambient " << vecText(scene.ambient()) << "\n";
    out << "shadow map 1024 reused " << (gpu_detail::reuseShadowMaps() ? "yes" : "no")
        << " bias max(0.004, 0.012*(1-facing)) point map 6 faces of 90 degrees\n";
    out << "mesh bounds center " << vecText(center) << " fit radius " << radius << " mesh count " << meshes.size() << "\n";

    out << "objects " << scene.objects().size() << "\n";
    for (const auto &object : scene.objects())
    {
        out << "object " << object->id << " \"" << object->name << "\" " << object->kind()
            << " tag \"" << object->tag << "\" layer " << object->layer
            << " parent " << object->parentId;
        if (!object->prefab.empty())
            out << " prefab \"" << object->prefab << "\"";
        if (!object->instanceOf.empty())
            out << " instance \"" << object->instanceOf << "\"";
        if (object->id == selectedObject)
            out << " SELECTED";
        out << "\n";
        if (const auto *sphere = dynamic_cast<const Sphere *>(object.get()))
            out << "  sphere center " << vecText(sphere->center()) << " radius " << sphere->worldRadius() << "\n";
        else if (const auto *plane = dynamic_cast<const Plane *>(object.get()))
        {
            out << "  plane point " << vecText(plane->point()) << " normal " << vecText(plane->normal());
            if (plane->checker())
                out << " checker " << vecText(plane->checkerAlbedo()) << " scale " << plane->checkerScale();
            out << "\n";
        }
        else if (const auto *mesh = dynamic_cast<const Mesh *>(object.get()))
        {
            Vec3 boxMin;
            Vec3 boxMax;
            worldBox(*mesh, boxMin, boxMax);
            out << "  mesh pos " << vecText(mesh->position())
                << " rot " << vecText(mesh->rotation())
                << " scale " << mesh->worldScale()
                << " tris " << mesh->triangles().size()
                << " path \"" << mesh->sourcePath() << "\"\n";
            out << "  world box " << vecText(boxMin) << " .. " << vecText(boxMax) << "\n";
        }
        const Motion &motion = object->motion;
        if (motion.active())
            out << "  motion move " << vecText(motion.move) << " rotate " << vecText(motion.rotate)
                << " scale " << motion.scale << " period " << motion.period << "\n";
        if (object->action.armed())
            out << "  action target \"" << object->action.target << "\" move " << vecText(object->action.move)
                << " rotate " << vecText(object->action.rotate) << "\n";
        writeMaterial(out, object->material());
    }

    out << "lights " << scene.lights().size() << "\n";
    int lightIndex = 0;
    for (const PointLight &light : scene.lights())
    {
        out << "light " << lightIndex << " \"" << light.name << "\" pos " << vecText(light.position)
            << " color " << vecText(light.color)
            << " intensity " << light.intensity
            << " falloff " << light.falloff
            << " radius " << light.radius
            << " directional " << (light.directional ? 1 : 0);
        if (light.spotOuter > 0)
            out << " spot " << vecText(light.spotDirection) << " outer " << light.spotOuter << " inner " << light.spotInner;
        if (lightIndex == selectedLight)
            out << " SELECTED";
        out << "\n";
        if (!light.directional && !meshes.empty() && lightIndex < 8)
        {
            const PointShadowFit fit = fitPointShadow(light.position, center, radius, meshMin, meshMax, static_cast<int>(meshes.size()));
            const char *lookName = fit.nearestMesh >= 0 && fit.nearestMesh < static_cast<int>(meshes.size()) ? meshes[static_cast<size_t>(fit.nearestMesh)]->name.c_str() : "";
            out << "  shadow look \"" << lookName << "\" mesh " << fit.nearestMesh
                << " target " << vecText(fit.target)
                << " fovDeg " << (fit.fov * 180.0 / kPi)
                << " near " << fit.nearPlane
                << " far " << fit.farPlane
                << " corners " << (fit.fromCorners ? "yes" : "no") << "\n";
            const Vec3 forward = normalize(fit.target - light.position);
            for (int mesh = 0; mesh < static_cast<int>(meshes.size()); ++mesh)
            {
                const size_t slot = static_cast<size_t>(mesh) * 4;
                const Vec3 boxMin(meshMin[slot], meshMin[slot + 1], meshMin[slot + 2]);
                const Vec3 boxMax(meshMax[slot], meshMax[slot + 1], meshMax[slot + 2]);
                double minDepth = 1e9;
                double maxDepth = -1e9;
                int behind = 0;
                int front = 0;
                const double xs[2] = {boxMin.x, boxMax.x};
                const double ys[2] = {boxMin.y, boxMax.y};
                const double zs[2] = {boxMin.z, boxMax.z};
                for (double x : xs)
                    for (double y : ys)
                        for (double z : zs)
                        {
                            const double along = dot(Vec3(x, y, z) - light.position, forward);
                            minDepth = std::min(minDepth, along);
                            maxDepth = std::max(maxDepth, along);
                            if (along <= 0.02)
                                ++behind;
                            else
                                ++front;
                        }
                out << "  caster " << mesh << " \"" << meshes[static_cast<size_t>(mesh)]->name
                    << "\" depth " << minDepth << " .. " << maxDepth
                    << " corners front " << front << " behind " << behind << "\n";
            }
        }
        ++lightIndex;
    }

    int emissionIndex = lightIndex;
    for (const auto &object : scene.objects())
    {
        if (emissionIndex >= 8)
            break;
        const Material material = object->material();
        if (material.emission <= 0.01)
            continue;
        const Vec3 eye = object->worldPosition();
        out << "emission light " << emissionIndex << " object " << object->id << " \"" << object->name
            << "\" pos " << vecText(eye) << " emission " << material.emission << "\n";
        if (!meshes.empty())
        {
            const PointShadowFit fit = fitPointShadow(eye, center, radius, meshMin, meshMax, static_cast<int>(meshes.size()));
            const char *lookName = fit.nearestMesh >= 0 && fit.nearestMesh < static_cast<int>(meshes.size()) ? meshes[static_cast<size_t>(fit.nearestMesh)]->name.c_str() : "";
            out << "  shadow look \"" << lookName << "\" mesh " << fit.nearestMesh
                << " target " << vecText(fit.target)
                << " fovDeg " << (fit.fov * 180.0 / kPi)
                << " near " << fit.nearPlane
                << " far " << fit.farPlane
                << " corners " << (fit.fromCorners ? "yes" : "no") << "\n";
            const Vec3 forward = normalize(fit.target - eye);
            for (int mesh = 0; mesh < static_cast<int>(meshes.size()); ++mesh)
            {
                const size_t slot = static_cast<size_t>(mesh) * 4;
                const Vec3 boxMin(meshMin[slot], meshMin[slot + 1], meshMin[slot + 2]);
                const Vec3 boxMax(meshMax[slot], meshMax[slot + 1], meshMax[slot + 2]);
                double minDepth = 1e9;
                double maxDepth = -1e9;
                int behind = 0;
                int front = 0;
                const double xs[2] = {boxMin.x, boxMax.x};
                const double ys[2] = {boxMin.y, boxMax.y};
                const double zs[2] = {boxMin.z, boxMax.z};
                for (double x : xs)
                    for (double y : ys)
                        for (double z : zs)
                        {
                            const double along = dot(Vec3(x, y, z) - eye, forward);
                            minDepth = std::min(minDepth, along);
                            maxDepth = std::max(maxDepth, along);
                            if (along <= 0.02)
                                ++behind;
                            else
                                ++front;
                        }
                out << "  caster " << mesh << " \"" << meshes[static_cast<size_t>(mesh)]->name
                    << "\" depth " << minDepth << " .. " << maxDepth
                    << " corners front " << front << " behind " << behind << "\n";
            }
        }
        ++emissionIndex;
    }

    std::string bounce;
    std::vector<BounceRay> bounceRays;
    mirrorBounceDebug(scene, bounce, bounceRays);
    out << bounce;
    out << "cameras " << scene.shots().size() << "\n";
    for (const SceneCamera &shot : scene.shots())
        out << "shot \"" << shot.name << "\" from " << vecText(shot.lookFrom) << " at " << vecText(shot.lookAt) << " fov " << shot.fov << "\n";
    return out.str();
}
