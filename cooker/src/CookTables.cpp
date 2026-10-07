#include "CookMesh.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef RAYTRACER_FLATC
#define RAYTRACER_FLATC "flatc"
#endif
#ifndef RAYTRACER_SCHEMAS
#define RAYTRACER_SCHEMAS "schemas"
#endif

bool cookCatalogJson(const std::filesystem::path &jsonPath, const std::filesystem::path &output, std::string &error)
{
    std::error_code ec;
    if (!std::filesystem::exists(jsonPath, ec))
    {
        error = "missing catalog json";
        return false;
    }
    const std::filesystem::path schema = std::filesystem::path(RAYTRACER_SCHEMAS) / "content.fbs";
    if (!std::filesystem::exists(schema, ec))
    {
        error = "missing schema: " + schema.string();
        return false;
    }
    const std::filesystem::path outDir = output.parent_path().empty() ? std::filesystem::path(".") : output.parent_path();
    std::filesystem::create_directories(outDir, ec);

    const std::string flatc = RAYTRACER_FLATC;
    // Windows `system` → `cmd /c`: a leading quote makes cmd strip the first and last quote.
    // Wrap the whole line in an extra pair so paths with spaces survive.
    const std::string inner = "\"" + flatc + "\" -b -o \"" + outDir.string() + "\" \"" + schema.string() + "\" \""
        + jsonPath.string() + "\"";
#if defined(_WIN32)
    const std::string cmd = "\"" + inner + "\"";
#else
    const std::string &cmd = inner;
#endif
    const int code = std::system(cmd.c_str());
    if (code != 0)
    {
        error = "flatc failed (" + std::to_string(code) + ") for catalog JSON";
        return false;
    }

    const std::filesystem::path flatcOut = outDir / (jsonPath.stem().string() + ".bin");
    if (!std::filesystem::exists(flatcOut, ec))
    {
        error = "flatc did not write " + flatcOut.string();
        return false;
    }
    if (std::filesystem::weakly_canonical(flatcOut, ec) != std::filesystem::weakly_canonical(output, ec))
    {
        std::filesystem::remove(output, ec);
        std::filesystem::rename(flatcOut, output, ec);
        if (ec)
        {
            std::filesystem::copy_file(flatcOut, output, std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::remove(flatcOut, ec);
            if (ec)
            {
                error = "could not move catalog to " + output.string();
                return false;
            }
        }
    }
    return true;
}
