#include "CookDeps.hpp"
#include "CookMesh.hpp"
#include "Terrain.hpp"

#include "Log.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    logging::init();
    bool ifNewer = false;
    bool dirMode = false;
    bool normalMap = false;
    bool hole = false;
    std::string mode = "mesh";
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--if-newer")
            ifNewer = true;
        else if (arg == "--dir")
            dirMode = true;
        else if (arg == "--normal")
            normalMap = true;
        else if (arg == "--hole")
            hole = true;
        else if (arg == "mesh" || arg == "image" || arg == "tables" || arg == "pack" || arg == "manifest"
            || arg == "terrain")
            mode = arg;
        else
            args.push_back(arg);
    }
    if (mode == "mesh" && args.size() != 2)
    {
        std::cerr << "usage:\n"
                  << "  cooker [--if-newer] [--dir] [mesh] <source> <output.rtm>\n"
                  << "  cooker image [--normal] <source.png> <output.dds>\n"
                  << "  cooker tables <catalog.json> <catalog.bin>\n"
                  << "  cooker pack <cooked-dir> <pak0.zip>\n"
                  << "  cooker manifest <cooked-dir>\n"
                  << "  cooker terrain [--hole] <output.rtt>\n";
        logging::shutdown();
        return 1;
    }
    std::string error;
    int code = 0;
    if (mode == "image")
    {
        if (args.size() != 2 || !cookImageToDds(args[0], args[1], normalMap, error))
            code = 1;
        else
            std::cout << "wrote " << args[1] << "\n";
    }
    else if (mode == "tables")
    {
        if (args.size() != 2 || !cookCatalogJson(args[0], args[1], error))
            code = 1;
        else
            std::cout << "wrote " << args[1] << "\n";
    }
    else if (mode == "pack")
    {
        if (args.size() != 2 || !packCookedZip(args[0], args[1], error))
            code = 1;
        else
            std::cout << "wrote " << args[1] << "\n";
    }
    else if (mode == "manifest")
    {
        if (args.size() != 1 || !cookWriteManifest(args[0], error))
            code = 1;
        else
            std::cout << "wrote " << args[0] << "/manifest.txt\n";
    }
    else if (mode == "terrain")
    {
        if (args.size() != 1 || !writeTerrainFile(args[0], makeHillTerrain(hole), error))
            code = 1;
        else
            std::cout << "wrote " << args[0] << "\n";
    }
    else if (dirMode)
    {
        const int count = cookDirectoryToRtm(args[0], args[1], ifNewer, error);
        if (count < 0)
            code = 1;
        else
            std::cout << "cooked " << count << " meshes to " << args[1] << "\n";
    }
    else if (!cookSourceToRtm(args[0], args[1], error, ifNewer))
        code = 1;
    else
        std::cout << "wrote " << args[1] << "\n";
    if (code != 0)
        std::cerr << error << "\n";
    logging::shutdown();
    return code;
}
