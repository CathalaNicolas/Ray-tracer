#include "DebugDraw.hpp"

#include "Constants.hpp"
#include "EngineSettings.hpp"
#include "Object.hpp"
#include "PlayDetail.hpp"
#include "SceneDebug.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{

struct ViewBasis
{
    Vec3 origin;
    Vec3 right;
    Vec3 up;
    Vec3 forward;
    double halfWidth = 1;
    double halfHeight = 1;
    float x0 = 0;
    float y0 = 0;
    float width = 1;
    float height = 1;
};

ViewBasis basisFrom(const Camera &camera, float x0, float y0, float x1, float y1)
{
    ViewBasis basis;
    basis.origin = camera.origin();
    basis.right = camera.rightAxis();
    basis.up = camera.upAxis();
    basis.forward = camera.viewDirection();
    basis.halfWidth = std::max(length(camera.horizontal()) * 0.5, 1e-8);
    basis.halfHeight = std::max(length(camera.vertical()) * 0.5, 1e-8);
    basis.x0 = x0;
    basis.y0 = y0;
    basis.width = std::max(x1 - x0, 1.0f);
    basis.height = std::max(y1 - y0, 1.0f);
    return basis;
}

bool project(const ViewBasis &basis, const Vec3 &world, ImVec2 &screen)
{
    const Vec3 offset = world - basis.origin;
    const double depth = dot(offset, basis.forward);
    if (depth <= 1e-4)
        return false;
    const double s = 0.5 + 0.5 * dot(offset, basis.right) / (depth * basis.halfWidth);
    const double t = 0.5 + 0.5 * dot(offset, basis.up) / (depth * basis.halfHeight);
    // gl_FragCoord.y 0 is the bottom of the texture and camera t=1. ImGui shows that texel at the top of the image.
    screen.x = basis.x0 + static_cast<float>(s) * basis.width;
    screen.y = basis.y0 + static_cast<float>(1.0 - t) * basis.height;
    return true;
}

float spherePixelRadius(const ViewBasis &basis, const Vec3 &center, double radius)
{
    const double depth = std::max(dot(center - basis.origin, basis.forward), 1e-4);
    return static_cast<float>((radius / depth) / basis.halfHeight * (static_cast<double>(basis.height) * 0.5));
}

void drawSphereCircle(ImDrawList *draw, const ViewBasis &basis, const Vec3 &center, double radius, ImU32 color)
{
    ImVec2 screen;
    if (!project(basis, center, screen))
        return;
    const float pixels = spherePixelRadius(basis, center, radius);
    if (pixels < 0.5f)
        return;
    draw->AddCircle(screen, pixels, color, 0, 1.5f);
}

// Same cutoff as project(). A clipped end sits just past it so project() accepts the point.
constexpr double kNearDepth = 1e-4;

double viewDepth(const ViewBasis &basis, const Vec3 &world)
{
    return dot(world - basis.origin, basis.forward);
}

bool clipEdgeToNear(const ViewBasis &basis, Vec3 &a, Vec3 &b)
{
    const double depthA = viewDepth(basis, a);
    const double depthB = viewDepth(basis, b);
    const bool frontA = depthA > kNearDepth;
    const bool frontB = depthB > kNearDepth;
    if (!frontA && !frontB)
        return false;
    if (frontA && frontB)
        return true;
    const double span = depthB - depthA;
    if (std::abs(span) <= 1e-18)
        return false;
    const double near = std::nextafter(kNearDepth, 1.0);
    double t = (near - depthA) / span;
    if (t < 0.0)
        t = 0.0;
    else if (t > 1.0)
        t = 1.0;
    const Vec3 clipped = a + (b - a) * t;
    if (!frontA)
        a = clipped;
    else
        b = clipped;
    return true;
}

void drawBoxEdge(ImDrawList *draw, const ViewBasis &basis, Vec3 a, Vec3 b, ImU32 color)
{
    if (!clipEdgeToNear(basis, a, b))
        return;
    ImVec2 screenA;
    ImVec2 screenB;
    if (!project(basis, a, screenA) || !project(basis, b, screenB))
        return;
    if (!std::isfinite(screenA.x) || !std::isfinite(screenA.y) || !std::isfinite(screenB.x) || !std::isfinite(screenB.y))
        return;
    draw->AddLine(screenA, screenB, color, 1.5f);
}

void drawMeshBox(ImDrawList *draw, const ViewBasis &basis, const ColliderSketch &sketch)
{
    if (sketch.kind == ColliderSketch::Kind::MeshPoint)
    {
        ImVec2 screen;
        if (!project(basis, sketch.center, screen))
            return;
        const float arm = 6.0f;
        draw->AddLine(ImVec2(screen.x - arm, screen.y), ImVec2(screen.x + arm, screen.y), IM_COL32(90, 170, 255, 255), 1.5f);
        draw->AddLine(ImVec2(screen.x, screen.y - arm), ImVec2(screen.x, screen.y + arm), IM_COL32(90, 170, 255, 255), 1.5f);
        return;
    }

    const Vec3 localMin = sketch.localMin;
    const Vec3 localMax = sketch.localMax;
    const double corners[8][3] = {
        {localMin.x, localMin.y, localMin.z},
        {localMax.x, localMin.y, localMin.z},
        {localMin.x, localMax.y, localMin.z},
        {localMax.x, localMax.y, localMin.z},
        {localMin.x, localMin.y, localMax.z},
        {localMax.x, localMin.y, localMax.z},
        {localMin.x, localMax.y, localMax.z},
        {localMax.x, localMax.y, localMax.z},
    };

    const double worldScale = sketch.radius;
    Vec3 world[8];
    for (int i = 0; i < 8; ++i)
    {
        const Vec3 local(corners[i][0], corners[i][1], corners[i][2]);
        world[i] = sketch.center + (sketch.axisX * local.x + sketch.axisY * local.y + sketch.axisZ * local.z) * worldScale;
    }

    const int edges[12][2] = {
        {0, 1}, {2, 3}, {4, 5}, {6, 7},
        {0, 2}, {1, 3}, {4, 6}, {5, 7},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };
    const ImU32 color = IM_COL32(90, 170, 255, 255);
    for (const auto &edge : edges)
        drawBoxEdge(draw, basis, world[edge[0]], world[edge[1]], color);
}

void drawPlanePatch(ImDrawList *draw, const ViewBasis &basis, const ColliderSketch &sketch)
{
    const Vec3 n = sketch.normal;
    const Vec3 helper = std::abs(n.y) < 0.9 ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    const Vec3 tangent = normalize(cross(helper, n));
    const Vec3 bitangent = cross(n, tangent);
    const Vec3 origin = sketch.center;
    constexpr double kHalf = 2.0;
    const Vec3 corner[4] = {
        origin + (tangent * -kHalf) + (bitangent * -kHalf),
        origin + (tangent * kHalf) + (bitangent * -kHalf),
        origin + (tangent * kHalf) + (bitangent * kHalf),
        origin + (tangent * -kHalf) + (bitangent * kHalf),
    };
    const ImU32 color = IM_COL32(230, 200, 70, 255);
    for (int i = 0; i < 4; ++i)
        drawBoxEdge(draw, basis, corner[i], corner[(i + 1) % 4], color);
    drawBoxEdge(draw, basis, origin, origin + n * 0.75, color);
}

double lightRange(const PointLight &light)
{
    const double falloff = light.falloff > 1e-4 ? light.falloff : 1e-4;
    return std::sqrt(3.0 / falloff);
}

void drawLightBound(ImDrawList *draw, const ViewBasis &basis, const PointLight &light)
{
    const ImU32 color = IM_COL32(255, 170, 50, 210);
    if (light.directional)
    {
        Vec3 dir = light.position;
        if (length(dir) < 1e-8)
            dir = Vec3(0, -1, 0);
        dir = normalize(dir);
        const Vec3 start = basis.origin + basis.forward * 2.0;
        drawBoxEdge(draw, basis, start, start + dir * 1.5, color);
        return;
    }
    const double range = lightRange(light);
    drawSphereCircle(draw, basis, light.position, range, color);
    if (light.radius > 0.01)
        drawSphereCircle(draw, basis, light.position, light.radius, IM_COL32(255, 90, 40, 255));
    if (light.spotOuter <= 0)
        return;
    Vec3 axis = light.spotDirection;
    if (length(axis) < 1e-8)
        return;
    axis = normalize(axis);
    const double rim = range * std::tan(light.spotOuter * kPi / 180.0);
    const Vec3 tip = light.position + axis * range;
    const Vec3 helper = std::abs(axis.y) < 0.9 ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    const Vec3 u = normalize(cross(helper, axis));
    const Vec3 v = cross(axis, u);
    Vec3 previous = tip + u * rim;
    for (int i = 1; i <= 8; ++i)
    {
        const double angle = (2.0 * kPi * i) / 8.0;
        const Vec3 point = tip + (u * std::cos(angle) + v * std::sin(angle)) * rim;
        drawBoxEdge(draw, basis, previous, point, color);
        if (i % 2 == 0)
            drawBoxEdge(draw, basis, light.position, point, color);
        previous = point;
    }
    drawBoxEdge(draw, basis, light.position, tip, color);
}

double viewAspect(const Camera &camera)
{
    const double height = length(camera.vertical());
    if (height <= 1e-12)
        return 1.0;
    return length(camera.horizontal()) / height;
}

bool isViewCamera(const Camera &view, const SceneCamera &shot)
{
    if (length(shot.lookFrom - view.origin()) > 1e-3)
        return false;
    Vec3 toward = shot.lookAt - shot.lookFrom;
    if (length(toward) <= 1e-8)
        return true;
    toward = normalize(toward);
    return length(toward - view.viewDirection()) <= 1e-3;
}

void drawFrustum(ImDrawList *draw, const ViewBasis &basis, const Camera &shot)
{
    const double nearDist = engineSettings().frustumNear;
    const double farDist = engineSettings().frustumFar;
    auto corner = [&](double s, double t, double dist) {
        return shot.origin() + (shot.lowerLeft() + shot.horizontal() * s + shot.vertical() * t - shot.origin()) * dist;
    };
    const double st[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    Vec3 nearCorner[4];
    Vec3 farCorner[4];
    for (int i = 0; i < 4; ++i)
    {
        nearCorner[i] = corner(st[i][0], st[i][1], nearDist);
        farCorner[i] = corner(st[i][0], st[i][1], farDist);
    }
    const ImU32 color = IM_COL32(176, 140, 255, 230);
    for (int i = 0; i < 4; ++i)
    {
        const int next = (i + 1) % 4;
        drawBoxEdge(draw, basis, nearCorner[i], nearCorner[next], color);
        drawBoxEdge(draw, basis, farCorner[i], farCorner[next], color);
        drawBoxEdge(draw, basis, nearCorner[i], farCorner[i], color);
    }
}

}

void drawColliders(const Scene &scene, const Camera &camera, EntityId playerId, float x0, float y0, float x1, float y1)
{
    if (x1 - x0 < 1.0f || y1 - y0 < 1.0f)
        return;
    const ViewBasis basis = basisFrom(camera, x0, y0, x1, y1);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    for (const auto &object : scene.objects())
    {
        const ColliderSketch sketch = object->colliderSketch();
        if (sketch.kind == ColliderSketch::Kind::PlanePatch)
        {
            drawPlanePatch(draw, basis, sketch);
            continue;
        }
        if (sketch.kind == ColliderSketch::Kind::Sphere)
        {
            const bool player = object->id() == playerId || sketch.player;
            if (player)
                drawSphereCircle(draw, basis, sketch.center, sketch.radius, IM_COL32(255, 176, 46, 255));
            else if (play_detail::isSolid(*object, playerId))
                drawSphereCircle(draw, basis, sketch.center, sketch.radius, IM_COL32(72, 220, 120, 255));
            continue;
        }
        if ((sketch.kind == ColliderSketch::Kind::MeshBox || sketch.kind == ColliderSketch::Kind::MeshPoint)
            && play_detail::isSolid(*object, playerId))
            drawMeshBox(draw, basis, sketch);
    }
    for (const PointLight &light : scene.lights())
        drawLightBound(draw, basis, light);
    // Named shots only. Drawing the view camera itself collapses to the image border.
    const double aspect = viewAspect(camera);
    for (const SceneCamera &shot : scene.shots())
    {
        if (isViewCamera(camera, shot))
            continue;
        drawFrustum(draw, basis, Camera(shot.lookFrom, shot.lookAt, Vec3(0, 1, 0), shot.fov, aspect));
    }
}

void drawWorldPrompt(const Camera &camera, const Vec3 &point, const char *text, float x0, float y0, float x1, float y1)
{
    if (text == nullptr || text[0] == '\0' || x1 - x0 < 1.0f || y1 - y0 < 1.0f)
        return;
    const ViewBasis basis = basisFrom(camera, x0, y0, x1, y1);
    ImVec2 screen;
    if (!project(basis, point, screen))
        return;
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pad(6.0f, 3.0f);
    const ImVec2 min(screen.x - size.x * 0.5f - pad.x, screen.y - size.y * 0.5f - pad.y);
    const ImVec2 max(screen.x + size.x * 0.5f + pad.x, screen.y + size.y * 0.5f + pad.y);
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(min, max, IM_COL32(16, 16, 18, 210), 4.0f);
    draw->AddText(ImVec2(screen.x - size.x * 0.5f, screen.y - size.y * 0.5f), IM_COL32(255, 255, 255, 255), text);
}

void drawBounceRays(const Scene &scene, const Camera &camera, float x0, float y0, float x1, float y1)
{
    if (x1 - x0 < 1.0f || y1 - y0 < 1.0f)
        return;
    std::string text;
    std::vector<BounceRay> rays;
    mirrorBounceDebug(scene, text, rays);
    const ViewBasis basis = basisFrom(camera, x0, y0, x1, y1);
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    for (const BounceRay &ray : rays)
    {
        ImVec2 a;
        ImVec2 b;
        if (!project(basis, ray.from, a) || !project(basis, ray.to, b))
            continue;
        const ImU32 color = ray.kept ? IM_COL32(80, 255, 140, 255) : IM_COL32(255, 70, 70, 220);
        draw->AddLine(a, b, color, ray.kept ? 2.0f : 1.0f);
    }
}

namespace
{

constexpr double kGizmoLength = 0.85;
constexpr float kGizmoPick = 8.0f;

Vec3 gizmoOrigin(const Object &object)
{
    return object.worldPosition();
}

bool gizmoUsable(const Object &object, int mode)
{
    // 1 = rotate (mesh Euler or plane normal tip), 2 = scale (mesh/sphere).
    if (mode == 1)
        return std::strcmp(object.kind(), "Mesh") == 0 || std::strcmp(object.kind(), "Plane") == 0;
    if (mode == 2)
        return object.bodyRadius() > 0 || std::strcmp(object.kind(), "Mesh") == 0;
    return true;
}

ImU32 axisColor(int axis)
{
    if (axis == 1)
        return IM_COL32(230, 70, 70, 255);
    if (axis == 2)
        return IM_COL32(70, 200, 90, 255);
    return IM_COL32(70, 140, 240, 255);
}

Vec3 axisDirection(int axis)
{
    if (axis == 1)
        return Vec3(1, 0, 0);
    if (axis == 2)
        return Vec3(0, 1, 0);
    return Vec3(0, 0, 1);
}

float segmentDistance(const ImVec2 &point, const ImVec2 &a, const ImVec2 &b)
{
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float length2 = abx * abx + aby * aby;
    float t = 0;
    if (length2 > 1e-6f)
        t = std::clamp(((point.x - a.x) * abx + (point.y - a.y) * aby) / length2, 0.0f, 1.0f);
    const float x = a.x + abx * t - point.x;
    const float y = a.y + aby * t - point.y;
    return std::sqrt(x * x + y * y);
}

bool closestHandle(const ViewBasis &basis, const Vec3 &origin, int mode, float mouseX, float mouseY, int &axis)
{
    float best = kGizmoPick;
    axis = 0;
    for (int candidate = 1; candidate <= 3; ++candidate)
    {
        const Vec3 direction = axisDirection(candidate);
        if (mode == 1)
        {
            const Vec3 side = candidate == 2 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
            const Vec3 bitangent = normalize(cross(direction, side));
            const Vec3 tangent = cross(bitangent, direction);
            ImVec2 previous;
            bool havePrevious = false;
            for (int step = 0; step <= 24; ++step)
            {
                const double angle = kPi * 2.0 * static_cast<double>(step) / 24.0;
                const Vec3 point = origin + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * (kGizmoLength * 0.75);
                ImVec2 screen;
                if (!project(basis, point, screen))
                {
                    havePrevious = false;
                    continue;
                }
                if (havePrevious)
                {
                    const float distance = segmentDistance(ImVec2(mouseX, mouseY), previous, screen);
                    if (distance < best)
                    {
                        best = distance;
                        axis = candidate;
                    }
                }
                previous = screen;
                havePrevious = true;
            }
            continue;
        }
        ImVec2 start;
        ImVec2 end;
        if (!project(basis, origin, start) || !project(basis, origin + direction * kGizmoLength, end))
            continue;
        const float distance = segmentDistance(ImVec2(mouseX, mouseY), start, end);
        if (distance < best)
        {
            best = distance;
            axis = candidate;
        }
    }
    return axis != 0;
}

void strokeGizmo(ImDrawList *draw, const ViewBasis &basis, const Vec3 &origin, int mode)
{
    for (int axis = 1; axis <= 3; ++axis)
    {
        const Vec3 direction = axisDirection(axis);
        const ImU32 color = axisColor(axis);
        if (mode == 1)
        {
            const Vec3 side = axis == 2 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
            const Vec3 bitangent = normalize(cross(direction, side));
            const Vec3 tangent = cross(bitangent, direction);
            ImVec2 previous;
            bool havePrevious = false;
            for (int step = 0; step <= 24; ++step)
            {
                const double angle = kPi * 2.0 * static_cast<double>(step) / 24.0;
                const Vec3 point = origin + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * (kGizmoLength * 0.75);
                ImVec2 screen;
                if (!project(basis, point, screen))
                {
                    havePrevious = false;
                    continue;
                }
                if (havePrevious)
                    draw->AddLine(previous, screen, color, 2.0f);
                previous = screen;
                havePrevious = true;
            }
            continue;
        }
        ImVec2 start;
        ImVec2 end;
        if (!project(basis, origin, start) || !project(basis, origin + direction * kGizmoLength, end))
            continue;
        draw->AddLine(start, end, color, 2.5f);
        draw->AddCircleFilled(end, 4.0f, color);
    }
}

}

void drawGizmo(const Scene &scene, EntityId objectId, int mode, const Camera &camera, float x0, float y0, float x1, float y1)
{
    const Object *object = scene.find(objectId);
    if (object == nullptr || !gizmoUsable(*object, mode) || x1 - x0 < 1.0f || y1 - y0 < 1.0f)
        return;
    strokeGizmo(ImGui::GetWindowDrawList(), basisFrom(camera, x0, y0, x1, y1), gizmoOrigin(*object), mode);
}

int pickGizmo(const Scene &scene, EntityId objectId, int mode, const Camera &camera, float x0, float y0, float x1, float y1, float mouseX, float mouseY)
{
    const Object *object = scene.find(objectId);
    if (object == nullptr || !gizmoUsable(*object, mode) || x1 - x0 < 1.0f || y1 - y0 < 1.0f)
        return 0;
    int axis = 0;
    closestHandle(basisFrom(camera, x0, y0, x1, y1), gizmoOrigin(*object), mode, mouseX, mouseY, axis);
    return axis;
}
