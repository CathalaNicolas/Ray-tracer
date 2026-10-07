#include "Content.hpp"
#include "DemoScene.hpp"
#include "Editor.hpp"
#include "Jobs.hpp"
#include "Log.hpp"
#include "MeshImport.hpp"
#include "MeshLoad.hpp"
#include "RayTracer.hpp"
#include "SelfTest.hpp"

#include "stb/stb_image_write.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

struct Options
{
    int width = 640;
    int height = 480;
    int samples = 1;
    int depth = 3;
    bool window = true;
    bool selfTest = false;
    bool game = false;
    bool help = false;
    std::string ppm;
    std::string png;
};

void printUsage()
{
    std::cout
        << "Usage: raytracer [options]\n"
        << "  --width N       image width (default 640)\n"
        << "  --height N      image height (default 480)\n"
        << "  --samples N     stratified grid per pixel edge (default 1)\n"
        << "  --depth N       reflection depth (default 3)\n"
        << "  --ppm FILE      write a PPM image (with --no-window)\n"
        << "  --png FILE      write a PNG image (with --no-window)\n"
        << "  --no-window     render once without the editor\n"
        << "  --self-test     run engine checks and exit\n"
        << "  --game          play in a window without the editor\n";
}

bool parseArgs(int argc, char **argv, Options &options)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        auto requireValue = [&](const char *name) -> const char * {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value for " << name << "\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--help")
        {
            options.help = true;
            return true;
        }
        if (arg == "--self-test")
        {
            options.selfTest = true;
        }
        else if (arg == "--game")
        {
            options.game = true;
        }
        else if (arg == "--no-window")
        {
            options.window = false;
        }
        else if (arg == "--width")
        {
            const char *value = requireValue("--width");
            if (value == nullptr)
                return false;
            options.width = std::stoi(value);
        }
        else if (arg == "--height")
        {
            const char *value = requireValue("--height");
            if (value == nullptr)
                return false;
            options.height = std::stoi(value);
        }
        else if (arg == "--samples")
        {
            const char *value = requireValue("--samples");
            if (value == nullptr)
                return false;
            options.samples = std::stoi(value);
        }
        else if (arg == "--depth")
        {
            const char *value = requireValue("--depth");
            if (value == nullptr)
                return false;
            options.depth = std::stoi(value);
        }
        else if (arg == "--ppm")
        {
            const char *value = requireValue("--ppm");
            if (value == nullptr)
                return false;
            options.ppm = value;
        }
        else if (arg == "--png")
        {
            const char *value = requireValue("--png");
            if (value == nullptr)
                return false;
            options.png = value;
        }
        else
        {
            std::cerr << "Unknown option " << arg << "\n";
            printUsage();
            return false;
        }
    }

    if (options.width <= 0 || options.height <= 0 || options.samples <= 0 || options.depth < 0)
    {
        std::cerr << "Width, height, and samples must be positive\n";
        return false;
    }
    return true;
}

bool savePng(const Image &image, const std::string &path)
{
    std::vector<std::uint8_t> pixels = image.toRGBA();
    int stride = image.width() * 4;
    if (stbi_write_png(path.c_str(), image.width(), image.height(), 4, pixels.data(), stride) == 0)
    {
        std::cerr << "Could not write " << path << "\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    logging::init();
    jobs::init();
    contentInit(".");
    setMeshFileLoader(mesh_import::load);

    int exitCode = 0;
    Options options;
    try
    {
        if (!parseArgs(argc, argv, options))
            exitCode = 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        exitCode = 1;
    }

    if (exitCode == 0)
    {
        if (options.help)
        {
            printUsage();
        }
        else if (options.selfTest)
        {
            exitCode = runSelfTests();
        }
        else if (options.game)
        {
            spdlog::info("starting game window {}x{}", options.width, options.height);
#if defined(RAYTRACER_DILIGENT)
            // M4: Diligent is the primary interactive view; GL stays for --self-test / stills.
            exitCode = runEditorDiligent(options.width, options.height);
#else
            exitCode = runEditor(options.width, options.height, options.samples, options.depth, true);
#endif
        }
        else if (options.window)
        {
            spdlog::info("starting editor {}x{}", options.width, options.height);
#if defined(RAYTRACER_DILIGENT)
            exitCode = runEditorDiligent(options.width, options.height);
#else
            exitCode = runEditor(options.width, options.height, options.samples, options.depth, false);
#endif
        }
        else
        {
            Scene scene = createDemoScene();
            Camera camera = createDemoCamera(static_cast<double>(options.width) / options.height);
            Image image(options.width, options.height);

            RayTracer tracer;
            tracer.maxDepth = options.depth;
            tracer.sampleGrid = options.samples;

            auto started = std::chrono::steady_clock::now();
            tracer.render(scene, camera, image);
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
            std::cout << "Rendered " << options.width << "x" << options.height
                      << " in " << elapsed.count() << " ms\n";
            spdlog::info("offline render {}x{} in {} ms", options.width, options.height, elapsed.count());

            bool ok = true;
            if (!options.ppm.empty() && !image.writePPM(options.ppm))
            {
                std::cerr << "Could not write " << options.ppm << "\n";
                ok = false;
            }
            if (!options.png.empty() && !savePng(image, options.png))
                ok = false;

            exitCode = ok ? 0 : 1;
        }
    }

    contentShutdown();
    jobs::shutdown();
    logging::shutdown();
    return exitCode;
}
