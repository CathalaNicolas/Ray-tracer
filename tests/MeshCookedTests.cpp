#include "Content.hpp"
#include "CookSidecar.hpp"
#include "MeshCooked.hpp"
#include "MeshLoad.hpp"
#include "MeshObj.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

TEST_CASE("OBJ round-trips through RTM1+zstd indexed")
{
    std::string error;
    auto source = mesh_obj::load("assets/platform.obj", error);
    REQUIRE(source != nullptr);
    REQUIRE_FALSE(source->positions.empty());
    REQUIRE(source->indices.size() >= 3);
    REQUIRE_FALSE(source->triangles.empty());
    CHECK(source->positions.size() < source->indices.size());

    std::vector<std::uint8_t> bytes;
    REQUIRE(encodeMeshGeometry(*source, bytes, error));
    REQUIRE(bytes.size() >= 12);
    CHECK(bytes[0] == 'R');
    CHECK(bytes[1] == 'T');
    CHECK(bytes[2] == 'M');
    CHECK(bytes[3] == '1');

    MeshGeometry loaded;
    REQUIRE(decodeMeshGeometry(bytes, loaded, error));
    REQUIRE(loaded.positions.size() == source->positions.size());
    REQUIRE(loaded.indices.size() == source->indices.size());
    REQUIRE(loaded.triangles.size() == source->triangles.size());
    CHECK(loaded.extraLods.empty());
    CHECK(loaded.positions[0].x == doctest::Approx(source->positions[0].x).epsilon(1e-5));
    CHECK(loaded.positions[0].y == doctest::Approx(source->positions[0].y).epsilon(1e-5));
    CHECK(loaded.positions[0].z == doctest::Approx(source->positions[0].z).epsilon(1e-5));
    CHECK(loaded.indices[0] == source->indices[0]);
    CHECK(loaded.root >= 0);

    source->extraLods.push_back(MeshLod{});
    source->extraLods.back().indices = source->indices;
    REQUIRE(encodeMeshGeometry(*source, bytes, error));
    MeshGeometry withLods;
    REQUIRE(decodeMeshGeometry(bytes, withLods, error));
    REQUIRE(withLods.extraLods.size() == 1);
    CHECK(withLods.extraLods[0].indices.size() == source->indices.size());
    CHECK(withLods.extraLods[0].positions.empty());

    const auto temp = std::filesystem::temp_directory_path() / "raytracer-platform-test.rtm";
    REQUIRE(writeRtmFile(temp, *source, error));
    auto fromDisk = loadMeshFile(temp, error);
    REQUIRE(fromDisk != nullptr);
    CHECK(fromDisk->positions.size() == source->positions.size());
    CHECK(fromDisk->indices.size() == source->indices.size());
    std::error_code ec;
    std::filesystem::remove(temp, ec);
}

TEST_CASE("shipping mode refuses source meshes without a matching cooked file")
{
    setAllowSourceMeshes(false);
    std::string error;
    auto loaded = loadMeshFile("assets/platform.obj", error);
    CHECK(loaded == nullptr);
    CHECK(error.find("cook") != std::string::npos);
    setAllowSourceMeshes(true);
    loaded = loadMeshFile("assets/platform.obj", error);
    REQUIRE(loaded != nullptr);
}
