#include "Catalog.hpp"
#include "Content.hpp"
#include "Hash.hpp"
#include "TextureCooked.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

void appendU32(std::vector<std::uint8_t> &out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

// Minimal uncompressed RGBA8 DDS (no DirectXTex).
bool writeRgba8Dds(const std::filesystem::path &path, int width, int height, const std::uint8_t *rgba)
{
    std::vector<std::uint8_t> bytes;
    appendU32(bytes, 0x20534444u); // DDS "
    appendU32(bytes, 124); // header size
    appendU32(bytes, 0x1007u); // caps | height | width | pixelformat
    appendU32(bytes, static_cast<std::uint32_t>(height));
    appendU32(bytes, static_cast<std::uint32_t>(width));
    appendU32(bytes, static_cast<std::uint32_t>(width * 4));
    appendU32(bytes, 0);
    appendU32(bytes, 1); // mip count
    for (int i = 0; i < 11; ++i)
        appendU32(bytes, 0);
    appendU32(bytes, 32); // pf size
    appendU32(bytes, 0x41u); // RGB | alphapixels
    appendU32(bytes, 0); // fourCC
    appendU32(bytes, 32);
    appendU32(bytes, 0x000000ffu);
    appendU32(bytes, 0x0000ff00u);
    appendU32(bytes, 0x00ff0000u);
    appendU32(bytes, 0xff000000u);
    appendU32(bytes, 0x1000u); // caps
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    bytes.insert(bytes.end(), rgba, rgba + pixels);
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

// Minimal BC3/DXT5 4x4 DDS (one opaque red-ish block).
bool writeBc3Dds(const std::filesystem::path &path)
{
    std::vector<std::uint8_t> bytes;
    appendU32(bytes, 0x20534444u);
    appendU32(bytes, 124);
    appendU32(bytes, 0x1007u);
    appendU32(bytes, 4);
    appendU32(bytes, 4);
    appendU32(bytes, 16);
    appendU32(bytes, 0);
    appendU32(bytes, 1);
    for (int i = 0; i < 11; ++i)
        appendU32(bytes, 0);
    appendU32(bytes, 32);
    appendU32(bytes, 0x4u); // FOURCC
    appendU32(bytes, 0x35545844u); // DXT5
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    appendU32(bytes, 0x1000u);
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    // Alpha block + color block (16 bytes)
    const std::uint8_t block[16] = {0xff, 0xff, 0, 0, 0, 0, 0, 0, 0x00, 0xf8, 0x00, 0xf8, 0, 0, 0, 0};
    bytes.insert(bytes.end(), block, block + 16);
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

} // namespace

TEST_CASE("sha256 of abc")
{
    std::string hex;
    std::string error;
    REQUIRE(sha256Bytes("abc", 3, hex, error));
    CHECK(hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("catalog round-trips through FlatBuffers")
{
    Catalog catalog;
    catalog.items.push_back(CatalogItem{1, "Coin"});
    catalog.creatures.push_back(CatalogCreature{1, "Wolf", 30});
    catalog.zones.push_back(CatalogZone{1, "Demo", 1});
    std::vector<std::uint8_t> bytes;
    std::string error;
    REQUIRE(encodeCatalog(catalog, bytes, error));
    Catalog loaded;
    REQUIRE(loadCatalogBytes(bytes.data(), bytes.size(), loaded, error));
    REQUIRE(loaded.items.size() == 1);
    CHECK(loaded.items[0].name == "Coin");
    REQUIRE(loaded.creatures.size() == 1);
    CHECK(loaded.creatures[0].health == 30);
    REQUIRE(loaded.zones.size() == 1);
    CHECK(loaded.zones[0].mapId == 1);
}

TEST_CASE("uncompressed DDS loads rgba8")
{
    std::vector<std::uint8_t> rgba(16 * 16 * 4, 0);
    for (int i = 0; i < 16 * 16; ++i)
    {
        rgba[static_cast<std::size_t>(i) * 4 + 0] = 220;
        rgba[static_cast<std::size_t>(i) * 4 + 3] = 255;
    }
    const auto path = std::filesystem::temp_directory_path() / "raytracer-tex-rgba.dds";
    REQUIRE(writeRgba8Dds(path, 16, 16, rgba.data()));
    LoadedImage image;
    std::string error;
    REQUIRE_MESSAGE(loadDdsFile(path, image, error), error);
    CHECK(image.width == 16);
    CHECK(image.height == 16);
    CHECK_FALSE(image.compressed);
    CHECK(image.rgba8.size() >= 16 * 16 * 4);
    CHECK(image.rgba8[0] == 220);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_CASE("BC3 DDS loads compressed mips")
{
    const auto path = std::filesystem::temp_directory_path() / "raytracer-tex-bc3.dds";
    REQUIRE(writeBc3Dds(path));
    LoadedImage image;
    std::string error;
    REQUIRE_MESSAGE(loadDdsFile(path, image, error), error);
    CHECK(image.width == 4);
    CHECK(image.height == 4);
    CHECK(image.compressed);
    CHECK(image.format == GpuTexFormat::Bc3);
    REQUIRE(image.mips.size() == 1);
    CHECK(image.mips[0].bytes.size() == 16);
    CHECK(image.rgba8.empty());
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_CASE("cooked mesh path mirrors the source path")
{
    CHECK(cookedMeshVirtualPath("assets/door.obj") == "cooked/assets/door.rtm");
    CHECK(cookedMeshVirtualPath("assets/props/tree.obj") == "cooked/assets/props/tree.rtm");
    CHECK(cookedMeshVirtualPath("cooked/assets/door.rtm") == "cooked/assets/door.rtm");
}

TEST_CASE("PhysFS reads a cooked virtual path")
{
    const auto root = std::filesystem::temp_directory_path() / "raytracer-content-root";
    const auto cooked = root / "cooked";
    std::error_code ec;
    std::filesystem::create_directories(cooked, ec);
    {
        std::ofstream out(cooked / "probe.bin", std::ios::binary);
        out << "rtm-probe";
    }
    REQUIRE(contentInit(root));
    CHECK(contentExists("cooked/probe.bin"));
    std::vector<std::uint8_t> bytes;
    std::string error;
    REQUIRE(contentRead("cooked/probe.bin", bytes, error));
    CHECK(std::string(bytes.begin(), bytes.end()) == "rtm-probe");
    contentInit(".");
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("contentInit loads cooked catalog.bin")
{
    const auto root = std::filesystem::temp_directory_path() / "raytracer-catalog-root";
    const auto cooked = root / "cooked";
    std::error_code ec;
    std::filesystem::create_directories(cooked, ec);
    Catalog catalog;
    catalog.items.push_back(CatalogItem{9, "Gem"});
    catalog.creatures.push_back(CatalogCreature{2, "Boar", 12});
    catalog.zones.push_back(CatalogZone{3, "Woods", 7});
    std::vector<std::uint8_t> bytes;
    std::string error;
    REQUIRE(encodeCatalog(catalog, bytes, error));
    {
        std::ofstream out(cooked / "catalog.bin", std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    REQUIRE(contentInit(root));
    CHECK(contentCatalogLoaded());
    REQUIRE(contentCatalog().items.size() == 1);
    CHECK(contentCatalog().items[0].name == "Gem");
    REQUIRE(contentCatalog().creatures.size() == 1);
    CHECK(contentCatalog().creatures[0].health == 12);
    contentInit(".");
    std::filesystem::remove_all(root, ec);
}
