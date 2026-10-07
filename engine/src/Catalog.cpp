#include "Catalog.hpp"

#include "content_generated.h"

bool loadCatalogBytes(const std::uint8_t *data, std::size_t size, Catalog &catalog, std::string &error)
{
    catalog = {};
    flatbuffers::Verifier verifier(data, size);
    if (!content::VerifyCatalogBuffer(verifier))
    {
        error = "catalog buffer failed verify";
        return false;
    }
    const content::Catalog *root = content::GetCatalog(data);
    if (root == nullptr)
    {
        error = "empty catalog";
        return false;
    }
    if (root->items() != nullptr)
    {
        for (const content::Item *item : *root->items())
        {
            CatalogItem row;
            row.id = item->id();
            if (item->name() != nullptr)
                row.name = item->name()->str();
            catalog.items.push_back(std::move(row));
        }
    }
    if (root->creatures() != nullptr)
    {
        for (const content::CreatureTemplate *creature : *root->creatures())
        {
            CatalogCreature row;
            row.id = creature->id();
            row.health = creature->health();
            if (creature->name() != nullptr)
                row.name = creature->name()->str();
            catalog.creatures.push_back(std::move(row));
        }
    }
    if (root->zones() != nullptr)
    {
        for (const content::Zone *zone : *root->zones())
        {
            CatalogZone row;
            row.id = zone->id();
            row.mapId = zone->map_id();
            if (zone->name() != nullptr)
                row.name = zone->name()->str();
            catalog.zones.push_back(std::move(row));
        }
    }
    return true;
}

bool encodeCatalog(const Catalog &catalog, std::vector<std::uint8_t> &out, std::string &error)
{
    (void)error;
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<content::Item>> items;
    items.reserve(catalog.items.size());
    for (const CatalogItem &item : catalog.items)
        items.push_back(content::CreateItem(builder, item.id, builder.CreateString(item.name)));
    std::vector<flatbuffers::Offset<content::CreatureTemplate>> creatures;
    creatures.reserve(catalog.creatures.size());
    for (const CatalogCreature &creature : catalog.creatures)
        creatures.push_back(
            content::CreateCreatureTemplate(builder, creature.id, builder.CreateString(creature.name), creature.health));
    std::vector<flatbuffers::Offset<content::Zone>> zones;
    zones.reserve(catalog.zones.size());
    for (const CatalogZone &zone : catalog.zones)
        zones.push_back(content::CreateZone(builder, zone.id, builder.CreateString(zone.name), zone.mapId));
    const auto catalogOff = content::CreateCatalog(builder, builder.CreateVector(items), builder.CreateVector(creatures),
        builder.CreateVector(zones));
    content::FinishCatalogBuffer(builder, catalogOff);
    out.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
    return true;
}
