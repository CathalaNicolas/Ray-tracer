#pragma once

#include "EngineSettings.hpp"
#include "Object.hpp"

namespace play_detail
{

inline double moveSpeed() { return engineSettings().moveSpeed; }
inline double fallY() { return engineSettings().fallY; }

// forCamera skips any layer other than 0. The player call leaves that false, so layer 1 still blocks.
inline bool isSolid(const Object &object, EntityId playerId, bool forCamera = false)
{
    return forCamera ? object.blocksCamera(playerId) : object.blocksPlayer(playerId);
}

}
