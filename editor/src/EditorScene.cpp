#include "EditorInternal.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_map>

#include "Mesh.hpp"
#include "Plane.hpp"
#include "Sphere.hpp"
#include "DebugDraw.hpp"

namespace ed
{

bool objectChosen(const ViewState &view, EntityId id)
{
    if (id == kInvalidEntityId)
        return false;
    if (id == view.selectedObject)
        return true;
    return std::find(view.alsoSelected.begin(), view.alsoSelected.end(), id) != view.alsoSelected.end();
}

void chooseObject(ViewState &view, EntityId id, bool extend)
{
    view.selectedLight = -1;
    if (!extend || id == kInvalidEntityId)
    {
        view.alsoSelected.clear();
        view.selectedObject = id;
        return;
    }
    if (view.selectedObject == kInvalidEntityId)
    {
        view.selectedObject = id;
        return;
    }
    if (id == view.selectedObject)
    {
        if (view.alsoSelected.empty())
            view.selectedObject = kInvalidEntityId;
        else
        {
            view.selectedObject = view.alsoSelected.back();
            view.alsoSelected.pop_back();
        }
        return;
    }
    const auto found = std::find(view.alsoSelected.begin(), view.alsoSelected.end(), id);
    if (found != view.alsoSelected.end())
        view.alsoSelected.erase(found);
    else
        view.alsoSelected.push_back(id);
}

Vec3 worldCenterOf(Object *object)
{
    return object != nullptr ? object->worldPosition() : Vec3();
}

bool materialScrolling(const Scene &scene)
{
    for (const auto &object : scene.objects())
    {
        const Material material = object->material();
        if (material.uvScrollU != 0.0 || material.uvScrollV != 0.0)
            return true;
    }
    return false;
}

bool refreshAssets(Scene &scene)
{
    using Clock = std::chrono::steady_clock;
    static Clock::time_point nextStat{};
    const auto now = Clock::now();
    if (now < nextStat)
        return false;
    nextStat = now + std::chrono::milliseconds(250);

    bool changed = false;
    static std::unordered_map<std::string, std::pair<std::int64_t, std::uintmax_t>> images;
    auto watch = [&](const std::string &path) {
        if (path.empty())
            return;
        std::error_code error;
        const auto file = std::filesystem::u8path(path);
        const auto stamp = std::filesystem::last_write_time(file, error);
        const auto bytes = std::filesystem::file_size(file, error);
        if (error)
            return;
        const auto ticks = static_cast<std::int64_t>(stamp.time_since_epoch().count());
        auto found = images.find(path);
        if (found == images.end())
        {
            images.emplace(path, std::make_pair(ticks, bytes));
            return;
        }
        if (found->second.first != ticks || found->second.second != bytes)
        {
            found->second = {ticks, bytes};
            changed = true;
        }
    };
    watch(scene.environment());
    for (const auto &object : scene.objects())
    {
        if (object->isMesh())
        {
            std::string error;
            if (object->refreshFromDisk(error))
                changed = true;
        }
        const Material material = object->material();
        watch(material.albedoMap);
        watch(material.normalMap);
    }
    return changed;
}

void setWorldCenter(Object *object, const Vec3 &world)
{
    if (object != nullptr)
        object->setWorldPosition(world);
}

void duplicateSelection(Scene &scene, ViewState &view)
{
    std::vector<EntityId> ids;
    if (view.selectedObject != kInvalidEntityId)
        ids.push_back(view.selectedObject);
    ids.insert(ids.end(), view.alsoSelected.begin(), view.alsoSelected.end());
    if (ids.empty())
        return;
    view.alsoSelected.clear();
    view.selectedObject = kInvalidEntityId;
    for (EntityId id : ids)
    {
        Object *object = scene.find(id);
        if (object == nullptr)
            continue;
        const EntityId added = object->clone(scene);
        Object *copy = scene.find(added);
        const Vec3 nudge(0.55, 0.0, 0.55);
        copy->setLocalPosition(copy->localPosition() + nudge);
        if (!copy->name().empty())
            copy->name() += " copy";
        if (view.selectedObject == kInvalidEntityId)
            view.selectedObject = added;
        else
            view.alsoSelected.push_back(added);
    }
    view.selectedLight = -1;
}

void deleteSelection(Scene &scene, ViewState &view)
{
    if (view.selectedObject != kInvalidEntityId || !view.alsoSelected.empty())
    {
        std::vector<EntityId> ids = view.alsoSelected;
        if (view.selectedObject != kInvalidEntityId)
            ids.push_back(view.selectedObject);
        for (EntityId id : ids)
            scene.remove(id);
        view.selectedObject = kInvalidEntityId;
        view.alsoSelected.clear();
    }
    else if (view.selectedLight >= 0 && static_cast<size_t>(view.selectedLight) < scene.lights().size())
    {
        scene.removeLight(static_cast<size_t>(view.selectedLight));
        view.selectedLight = -1;
    }
    else if (view.selectedCamera >= 0 && view.selectedCamera < static_cast<int>(scene.shots().size()))
    {
        scene.shots().erase(scene.shots().begin() + view.selectedCamera);
        view.selectedCamera = -1;
        view.lookFrom = view.editorFrom;
        view.lookAt = view.editorAt;
        view.fov = view.editorFov;
    }
}

EntityId addSphere(Scene &scene)
{
    static const Vec3 colors[] = {
        Vec3(0.2, 0.45, 0.9),
        Vec3(0.9, 0.35, 0.15),
        Vec3(0.2, 0.75, 0.35),
        Vec3(0.7, 0.3, 0.8)};
    int count = 0;
    for (const auto &object : scene.objects())
    {
        if (std::string(object->kind()) == "Sphere")
            ++count;
    }
    Vec3 color = colors[count % 4];
    Object *sphere = scene.addSphere(
        Vec3(-1.2 + count * 0.55, 0.45, 0.8),
        0.45,
        Material::makeDiffuse(color));
    const EntityId id = sphere->id();
    sphere->name() = "Sphere " + std::to_string(id);
    return id;
}

EntityId addPlane(Scene &scene)
{
    Material material = Material::makeDiffuse(Vec3(0.55, 0.55, 0.6));
    Object *plane = scene.addPlane(Vec3(0, 1.2, -1.5), Vec3(0, 0, 1), material);
    plane->name() = "Plane " + std::to_string(plane->id());
    return plane->id();
}

bool drawObjectSettings(Object &object, const Scene &scene)
{
    editName(object.id(), object.name());
    bool changed = false;
    {
        static EntityId tagId = kInvalidEntityId;
        static char tagBuffer[64] = {};
        if (tagId != object.id() || object.tag() != tagBuffer)
        {
            tagId = object.id();
            std::snprintf(tagBuffer, sizeof(tagBuffer), "%s", object.tag().c_str());
        }
        if (ImGui::InputText("Tag", tagBuffer, sizeof(tagBuffer)))
        {
            object.setTag(tagBuffer);
            changed = true;
        }
    }
    {
        int layer = object.layer() == 1 ? 1 : 0;
        const char *layers[] = {"Player and camera", "Player only"};
        if (ImGui::Combo("Layer", &layer, layers, 2))
        {
            object.layer() = layer;
            changed = true;
        }
    }
    ImGui::TextDisabled("%s", object.kind());
    {
        std::string parentLabel = "None";
        if (object.parentId() != 0)
        {
            if (const Object *parent = scene.find(object.parentId()))
                parentLabel = parent->name().empty() ? ("#" + std::to_string(parent->id())) : parent->name();
            else
                parentLabel = "#" + std::to_string(object.parentId());
        }
        if (ImGui::BeginCombo("Parent", parentLabel.c_str()))
        {
            if (ImGui::Selectable("None", object.parentId() == 0))
            {
                object.setParentId(0);
                object.notifyTransformChanged();
                changed = true;
            }
            for (const auto &other : scene.objects())
            {
                if (other->id() == object.id())
                    continue;
                std::string label = other->name().empty() ? ("#" + std::to_string(other->id())) : other->name();
                label += "##parent" + std::to_string(other->id());
                if (ImGui::Selectable(label.c_str(), object.parentId() == other->id()))
                {
                    object.setParentId(other->id());
                    object.notifyTransformChanged();
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    }
    if (object.isSphere())
    {
        Object *sphere = &object;
        Vec3 center = sphere->localCenter();
        if (editVec3("Position", center, 0.02f))
        {
            sphere->setCenter(center);
            changed = true;
        }
        double radius = sphere->radius();
        if (editDouble("Radius", radius, 0.01, 0.05, 8))
        {
            sphere->setRadius(radius);
            changed = true;
        }
    }
    else if (object.isPlane())
    {
        Object *plane = &object;
        Vec3 point = plane->localPoint();
        if (editVec3("Position", point, 0.02f))
        {
            plane->setPoint(point);
            changed = true;
        }

        static EntityId normalId = kInvalidEntityId;
        static Vec3 normalEdit;
        if (normalId != plane->id())
        {
            normalId = plane->id();
            normalEdit = plane->normal();
        }
        float normalFields[3] = {
            static_cast<float>(normalEdit.x),
            static_cast<float>(normalEdit.y),
            static_cast<float>(normalEdit.z)};
        if (ImGui::DragFloat3("Normal", normalFields, 0.01f))
        {
            normalEdit = Vec3(normalFields[0], normalFields[1], normalFields[2]);
            plane->setNormal(normalEdit);
            changed = true;
        }
        if (ImGui::IsItemDeactivated())
            normalEdit = plane->normal();

        bool checker = plane->checker();
        if (ImGui::Checkbox("Checker", &checker))
        {
            plane->setCheckerEnabled(checker);
            changed = true;
        }
        if (checker)
        {
            Vec3 checkerColor = plane->checkerAlbedo();
            if (editColor("Checker color", checkerColor))
            {
                plane->setCheckerAlbedo(checkerColor);
                changed = true;
            }
            double scale = plane->checkerScale();
            if (editDouble("Checker scale", scale, 0.01, 0.1, 8))
            {
                plane->setCheckerScale(scale);
                changed = true;
            }
        }
    }
    else if (object.isMesh())
    {
        Object *mesh = &object;
        Vec3 position = mesh->localPositionValue();
        if (editVec3("Position", position, 0.02f))
        {
            mesh->setPosition(position);
            changed = true;
        }
        double scale = mesh->scale();
        if (editDouble("Scale", scale, 0.01, 0.01, 20))
        {
            mesh->setScale(scale);
            changed = true;
        }
        Vec3 rotation = mesh->rotation();
        if (editVec3("Rotation", rotation, 0.4f))
        {
            mesh->setRotation(rotation);
            changed = true;
        }
        ImGui::Text("%d triangles", static_cast<int>(mesh->triangles().size()));
        if (!mesh->sourcePath().empty())
            ImGui::TextWrapped("%s", filenameOf(mesh->sourcePath()).c_str());
    }
    if (editVec3("Motion move", object.motion().move, 0.02f))
        changed = true;
    if (editVec3("Motion rotate", object.motion().rotate, 0.4f))
        changed = true;
    if (editDouble("Motion scale", object.motion().scale, 0.01, -5, 5))
        changed = true;
    if (editDouble("Motion period", object.motion().period, 0.05, 0.2, 30))
        changed = true;
    if (editDouble("Spawn every", object.spawnEvery(), 0.05, 0, 60))
        changed = true;
    {
        static EntityId actionId = kInvalidEntityId;
        static char actionBuffer[64] = {};
        if (actionId != object.id())
        {
            actionId = object.id();
            std::snprintf(actionBuffer, sizeof(actionBuffer), "%s", object.action().target.c_str());
        }
        if (ImGui::InputText("Use target", actionBuffer, sizeof(actionBuffer)))
        {
            object.action().target = actionBuffer;
            changed = true;
        }
    }
    if (editVec3("Use move", object.action().move, 0.02f))
        changed = true;
    if (editVec3("Use rotate", object.action().rotate, 1.0f))
        changed = true;
    if (editMaterial(object))
        changed = true;
    return changed;
}


double gizmoAxisT(const Ray &ray, const Vec3 &origin, const Vec3 &axis)
{
    const Vec3 delta = ray.origin - origin;
    const double dir2 = dot(ray.direction, ray.direction);
    const double along = dot(axis, ray.direction);
    const double denom = dir2 - along * along;
    if (std::abs(denom) < 1e-8)
        return 0;
    return (along * dot(ray.direction, delta) - dir2 * dot(axis, delta)) / denom;
}

Vec3 gizmoAxisDir(int axis)
{
    if (axis == 1)
        return Vec3(1, 0, 0);
    if (axis == 2)
        return Vec3(0, 1, 0);
    return Vec3(0, 0, 1);
}

bool handleViewMouse(Scene &scene, ViewState &view, float drawWidth, float drawHeight, double aspect)
{
    bool dirty = false;
    ImGuiIO &io = ImGui::GetIO();
    static bool orbiting = false;
    static bool panning = false;
    static float orbitTravel = 0;
    static int gizmoAxis = 0;
    static Vec3 gizmoGrab;
    static double gizmoT = 0;
    static Vec3 gizmoEuler;
    static Vec3 gizmoNormal;
    static double gizmoSize = 1;
    static float gizmoPixels = 0;
    static std::vector<std::pair<EntityId, Vec3>> gizmoGroup;

    if (view.playing)
    {
        orbiting = false;
        panning = false;
        gizmoAxis = 0;
        return false;
    }

    auto imageRay = [&]() {
        ImVec2 mouse = ImGui::GetMousePos();
        ImVec2 origin = ImGui::GetItemRectMin();
        const double u = std::clamp(static_cast<double>(mouse.x - origin.x) / drawWidth, 0.0, 1.0);
        const double v = std::clamp(static_cast<double>(mouse.y - origin.y) / drawHeight, 0.0, 1.0);
        Camera camera(view.lookFrom, view.lookAt, Vec3(0, 1, 0), view.fov, aspect);
        return camera.getRay(u, 1.0 - v);
    };

    if (gizmoAxis != 0)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            Object *object = scene.find(view.selectedObject);
            if (object != nullptr)
            {
                const Vec3 axis = gizmoAxisDir(gizmoAxis);
                if (view.gizmoMode == 0)
                {
                    const double t = gizmoAxisT(imageRay(), gizmoGrab, axis);
                    Vec3 world = gizmoGrab + axis * (t - gizmoT);
                    if (ImGui::GetIO().KeyCtrl)
                        world = snapVec(world);
                    const Vec3 delta = world - gizmoGrab;
                    object->bindScene(&scene);
                    setWorldCenter(object, world);
                    for (const auto &item : gizmoGroup)
                    {
                        if (item.first == object->id())
                            continue;
                        Object *other = scene.find(item.first);
                        if (other == nullptr)
                            continue;
                        other->bindScene(&scene);
                        setWorldCenter(other, item.second + delta);
                    }
                }
                else if (view.gizmoMode == 1)
                {
                    gizmoPixels += io.MouseDelta.x - io.MouseDelta.y;
                    const double degrees = static_cast<double>(gizmoPixels) * 0.4;
                    if (std::strcmp(object->kind(), "Mesh") == 0)
                    {
                        Vec3 rotation = gizmoEuler;
                        if (gizmoAxis == 1)
                            rotation.x = gizmoEuler.x + degrees;
                        else if (gizmoAxis == 2)
                            rotation.y = gizmoEuler.y + degrees;
                        else
                            rotation.z = gizmoEuler.z + degrees;
                        object->setLocalRotation(rotation);
                    }
                    else if (object->isPlane())
                    {
                        const double angle = degrees * kPi / 180.0;
                        const double c = std::cos(angle);
                        const double s = std::sin(angle);
                        const Vec3 turned = gizmoNormal * c + cross(axis, gizmoNormal) * s + axis * dot(axis, gizmoNormal) * (1.0 - c);
                        object->setNormal(turned);
                    }
                }
                else
                {
                    gizmoPixels += io.MouseDelta.x - io.MouseDelta.y;
                    const double size = std::max(0.01, gizmoSize * std::exp(static_cast<double>(gizmoPixels) * 0.01));
                    object->setLocalScale(size);
                }
                dirty = true;
            }
        }
        else
        {
            gizmoAxis = 0;
        }
        if (gizmoAxis != 0)
            return dirty;
    }

    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
    {
        dollyCamera(view.lookFrom, view.lookAt, std::pow(0.9, static_cast<double>(io.MouseWheel)));
        dirty = true;
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        ImVec2 mouse = ImGui::GetMousePos();
        ImVec2 origin = ImGui::GetItemRectMin();
        Camera camera(view.lookFrom, view.lookAt, Vec3(0, 1, 0), view.fov, aspect);
        const int axis = pickGizmo(scene, view.selectedObject, view.gizmoMode, camera, origin.x, origin.y, origin.x + drawWidth, origin.y + drawHeight, mouse.x, mouse.y);
        Object *object = axis == 0 ? nullptr : scene.find(view.selectedObject);
        if (object != nullptr)
        {
            pushEditorHistory(scene, view);
            gizmoAxis = axis;
            gizmoPixels = 0;
            gizmoGrab = object->worldPosition();
            gizmoEuler = object->localRotation();
            gizmoSize = object->localScale();
            if (object->isPlane())
                gizmoNormal = object->normal();
            gizmoT = gizmoAxisT(imageRay(), gizmoGrab, gizmoAxisDir(axis));
            gizmoGroup.clear();
            if (view.gizmoMode == 0)
            {
                gizmoGroup.push_back({object->id(), gizmoGrab});
                for (EntityId id : view.alsoSelected)
                {
                    Object *extra = scene.find(id);
                    if (extra == nullptr)
                        continue;
                    extra->bindScene(&scene);
                    gizmoGroup.push_back({id, worldCenterOf(extra)});
                }
            }
            return dirty;
        }
        orbiting = true;
        orbitTravel = 0;
    }
    if (orbiting)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            orbitTravel += std::fabs(io.MouseDelta.x) + std::fabs(io.MouseDelta.y);
            if (orbitTravel > 3.0f)
            {
                orbitCamera(view.lookFrom, view.lookAt, -io.MouseDelta.x * 0.005, -io.MouseDelta.y * 0.005);
                dirty = true;
            }
        }
        else
        {
            if (orbitTravel <= 3.0f)
            {
                ImVec2 mouse = ImGui::GetMousePos();
                ImVec2 origin = ImGui::GetItemRectMin();
                double u = std::clamp(static_cast<double>(mouse.x - origin.x) / drawWidth, 0.0, 1.0);
                double v = std::clamp(static_cast<double>(mouse.y - origin.y) / drawHeight, 0.0, 1.0);
                Camera camera(view.lookFrom, view.lookAt, Vec3(0, 1, 0), view.fov, aspect);
                HitRecord hit;
                if (scene.intersect(camera.getRay(u, 1.0 - v), kEpsilon, std::numeric_limits<double>::infinity(), hit))
                    chooseObject(view, hit.objectId, ImGui::GetIO().KeyShift);
                else
                    chooseObject(view, kInvalidEntityId, false);
                dirty = true;
            }
            orbiting = false;
        }
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) || ImGui::IsItemClicked(ImGuiMouseButton_Middle))
        panning = true;
    if (panning)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        {
            if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)
            {
                panCamera(view.lookFrom, view.lookAt, io.MouseDelta.x, io.MouseDelta.y);
                dirty = true;
            }
        }
        else
        {
            panning = false;
        }
    }
    return dirty;
}


}
