#pragma once

#include <cstdint>

// Stable id for saves, prefabs, play, and the editor. 0 is invalid.
using EntityId = std::uint64_t;

inline constexpr EntityId kInvalidEntityId = 0;
inline constexpr EntityId kEntityIdPrefixMask = 0xFFFFFFFF00000000ull;
inline constexpr EntityId kEntityIdCounterMask = 0x00000000FFFFFFFFull;
// Reserved for streamed / coordinate-keyed terrain tiles. Scene session prefixes never use this.
inline constexpr EntityId kTerrainTileEntityPrefix = 0xFFFFFFFE00000000ull;

inline EntityId entityIdPrefix(EntityId id)
{
    return id & kEntityIdPrefixMask;
}

inline EntityId entityIdCounter(EntityId id)
{
    return id & kEntityIdCounterMask;
}

inline EntityId makeEntityId(EntityId prefix, EntityId counter)
{
    return (prefix & kEntityIdPrefixMask) | (counter & kEntityIdCounterMask);
}

// Stable id from map tile indices (16 bits each). Not issued by Scene::create.
inline EntityId terrainTileEntityId(int tileX, int tileZ)
{
    const auto x = static_cast<std::uint32_t>(tileX) & 0xFFFFu;
    const auto z = static_cast<std::uint32_t>(tileZ) & 0xFFFFu;
    return makeEntityId(kTerrainTileEntityPrefix, (static_cast<EntityId>(x) << 16) | z);
}
