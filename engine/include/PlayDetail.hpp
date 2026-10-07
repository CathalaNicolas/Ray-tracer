#pragma once

#include "Collision.hpp"
#include "EngineSettings.hpp"
#include "Hittable.hpp"

namespace play_detail
{

inline double moveSpeed() { return engineSettings().moveSpeed; }
inline double gravity() { return engineSettings().gravity; }
inline double jumpSpeed() { return engineSettings().jumpSpeed; }
inline double groundProbe() { return engineSettings().groundProbe; }
inline double stepHeight() { return engineSettings().stepHeight; }
inline double fallY() { return engineSettings().fallY; }

// forCamera skips any layer other than 0. The player call leaves that false, so layer 1 still blocks.
inline bool isSolid(const Hittable &object, int playerId, bool forCamera = false)
{
    return forCamera ? object.blocksCamera(playerId) : object.blocksPlayer(playerId);
}

inline Hit contact(const Hittable &object, const Vec3 &center, double radius)
{
    return object.contactSphere(center, radius);
}

}
