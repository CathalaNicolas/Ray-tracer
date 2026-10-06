#include "EditorInternal.hpp"

#include "EngineSettings.hpp"
#include "Material.hpp"
#include "Sound.hpp"

#include <commdlg.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace ed
{

int vkFromName(const std::string &name)
{
    if (name == "Space")
        return VK_SPACE;
    if (name.size() == 1)
    {
        unsigned char letter = static_cast<unsigned char>(name[0]);
        if (letter >= 'a' && letter <= 'z')
            letter = static_cast<unsigned char>(letter - 'a' + 'A');
        if ((letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9'))
            return static_cast<int>(letter);
    }
    return 0;
}

std::string nameFromVk(int vk)
{
    if (vk == VK_SPACE)
        return "Space";
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
        return std::string(1, static_cast<char>(vk));
    return std::to_string(vk);
}

void assignBind(const std::string &action, int vk)
{
    if (vk == 0)
        return;
    if (action == "forward")
        gKeyForward = vk;
    else if (action == "back")
        gKeyBack = vk;
    else if (action == "left")
        gKeyLeft = vk;
    else if (action == "right")
        gKeyRight = vk;
    else if (action == "jump")
        gKeyJump = vk;
    else if (action == "jump2")
        gKeyJumpAlt = vk;
    else if (action == "use")
        gKeyUse = vk;
}

void loadEditorSettings()
{
    std::ifstream in("raytracer-settings.txt");
    std::string key;
    std::string value;
    while (in >> key >> value)
    {
        if (key.rfind("key_", 0) == 0)
        {
            assignBind(key.substr(4), vkFromName(value));
            continue;
        }
        if (key == "volume")
        {
            double number = 0;
            try
            {
                number = std::stod(value);
            }
            catch (...)
            {
                continue;
            }
            setMasterVolume(number);
            continue;
        }
        if (key == "width")
        {
            try
            {
                gSavedWidth = static_cast<int>(std::stod(value));
            }
            catch (...)
            {
            }
            continue;
        }
        if (key == "height")
        {
            try
            {
                gSavedHeight = static_cast<int>(std::stod(value));
            }
            catch (...)
            {
            }
            continue;
        }
        if (key == "samples")
        {
            try
            {
                gSavedSamples = static_cast<int>(std::stod(value));
            }
            catch (...)
            {
            }
            continue;
        }
        if (key == "bounces")
        {
            try
            {
                gSavedBounces = static_cast<int>(std::stod(value));
            }
            catch (...)
            {
            }
            continue;
        }
        applyEngineSetting(key, value);
    }
}

void saveEditorSettings(const ViewState &view)
{
    std::ofstream out("raytracer-settings.txt");
    out << "volume " << masterVolume() << '\n';
    out << "width " << view.width << '\n';
    out << "height " << view.height << '\n';
    out << "samples " << view.samples << '\n';
    out << "bounces " << view.depth << '\n';
    writeEngineSettings(out);
    out << "key_forward " << nameFromVk(gKeyForward) << '\n';
    out << "key_back " << nameFromVk(gKeyBack) << '\n';
    out << "key_left " << nameFromVk(gKeyLeft) << '\n';
    out << "key_right " << nameFromVk(gKeyRight) << '\n';
    out << "key_jump " << nameFromVk(gKeyJump) << '\n';
    out << "key_jump2 " << nameFromVk(gKeyJumpAlt) << '\n';
    out << "key_use " << nameFromVk(gKeyUse) << '\n';
}

bool assetIsMesh(const std::string &path)
{
    const auto ext = std::filesystem::path(path).extension().string();
    return ext == ".obj" || ext == ".fbx" || ext == ".OBJ" || ext == ".FBX";
}

bool assetIsImage(const std::string &path)
{
    const auto ext = std::filesystem::path(path).extension().string();
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".hdr" || ext == ".bmp" || ext == ".tga"
        || ext == ".PNG" || ext == ".JPG" || ext == ".JPEG";
}

Vec3 snapVec(const Vec3 &value)
{
    const double step = engineSettings().snap;
    if (step <= 0)
        return value;
    auto snap = [&](double component) {
        return std::round(component / step) * step;
    };
    return Vec3(snap(value.x), snap(value.y), snap(value.z));
}

bool editDouble(const char *label, double &value, double speed, double minValue, double maxValue)
{
    return ImGui::DragScalar(label, ImGuiDataType_Double, &value, static_cast<float>(speed), &minValue, &maxValue, "%.3f");
}

bool editVec3(const char *label, Vec3 &value, float speed)
{
    float fields[3] = {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z)};
    if (!ImGui::DragFloat3(label, fields, speed))
        return false;
    value = Vec3(fields[0], fields[1], fields[2]);
    const double step = engineSettings().snap;
    if (ImGui::GetIO().KeyCtrl && step > 0)
    {
        auto snap = [&](double component) {
            return std::round(component / step) * step;
        };
        value = Vec3(snap(value.x), snap(value.y), snap(value.z));
    }
    return true;
}

bool editColor(const char *label, Vec3 &value)
{
    float fields[3] = {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z)};
    if (!ImGui::ColorEdit3(label, fields))
        return false;
    value = Vec3(fields[0], fields[1], fields[2]);
    return true;
}

bool pickOpenFile(const wchar_t *filter, const wchar_t *extension, std::filesystem::path &path)
{
    wchar_t buffer[4096] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_hwnd;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = 4096;
    dialog.Flags = OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    dialog.lpstrDefExt = extension;
    if (::GetOpenFileNameW(&dialog) == FALSE)
        return false;
    path = buffer;
    return true;
}

bool pickSaveFile(const wchar_t *filter, const wchar_t *extension, std::filesystem::path &path)
{
    wchar_t buffer[4096] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_hwnd;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = 4096;
    dialog.Flags = OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    dialog.lpstrDefExt = extension;
    if (::GetSaveFileNameW(&dialog) == FALSE)
        return false;
    path = buffer;
    return true;
}

std::string filenameOf(const std::string &stored)
{
    return std::filesystem::path(stored).filename().string();
}

Camera viewCamera(const ViewState &view, double aspect)
{
    Camera camera(view.lookFrom, view.lookAt, Vec3(0, 1, 0), view.fov, aspect);
    camera.setAperture(view.aperture);
    double focus = view.focusDistance;
    if (focus <= 1e-4)
        focus = std::max(length(view.lookAt - view.lookFrom), 0.05);
    camera.setFocusDistance(focus);
    return camera;
}

bool editName(int id, std::string &name)
{
    static int editedId = -1;
    static char buffer[128] = {};
    if (editedId != id)
    {
        editedId = id;
        std::snprintf(buffer, sizeof(buffer), "%s", name.c_str());
    }
    if (!ImGui::InputText("Name", buffer, sizeof(buffer)))
        return false;
    name = buffer;
    return true;
}

bool editMaterial(Hittable &object)
{
    Material material = object.material();
    bool changed = false;
    changed |= editColor("Albedo", material.albedo);
    changed |= editDouble("Ambient", material.ambient, 0.01, 0, 2);
    changed |= editDouble("Diffuse", material.diffuse, 0.01, 0, 2);
    changed |= editDouble("Specular", material.specular, 0.01, 0, 2);
    changed |= editDouble("Shininess", material.shininess, 1, 1, 512);
    changed |= editDouble("Reflectivity", material.reflectivity, 0.01, 0, 1);
    changed |= editDouble("Transmission", material.transmission, 0.01, 0, 1);
    changed |= editDouble("IOR", material.ior, 0.01, 1, 3);
    changed |= editDouble("Emission", material.emission, 0.01, 0, 20);
    changed |= editDouble("UV scale", material.uvScale, 0.01, 0.01, 64);
    changed |= editDouble("UV scroll U", material.uvScrollU, 0.01, -8, 8);
    changed |= editDouble("UV scroll V", material.uvScrollV, 0.01, -8, 8);
    if (changed)
        object.setMaterial(material);
    return changed;
}

}
