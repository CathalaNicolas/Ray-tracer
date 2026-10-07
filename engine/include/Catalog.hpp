#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct CatalogItem
{
    std::uint64_t id = 0;
    std::string name;
};

struct CatalogCreature
{
    std::uint64_t id = 0;
    std::string name;
    std::int32_t health = 0;
};

struct CatalogZone
{
    std::uint64_t id = 0;
    std::string name;
    std::uint64_t mapId = 0;
};

struct Catalog
{
    std::vector<CatalogItem> items;
    std::vector<CatalogCreature> creatures;
    std::vector<CatalogZone> zones;
};

bool loadCatalogBytes(const std::uint8_t *data, std::size_t size, Catalog &catalog, std::string &error);
bool encodeCatalog(const Catalog &catalog, std::vector<std::uint8_t> &out, std::string &error);
