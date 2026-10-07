#pragma once

#include <memory>
#include <string>

// Gameplay role beside the shape. The scene file still stores the tag string;
// makeRole builds the matching role. Play dispatches on RoleKind, not strings.
enum class RoleKind
{
    Solid,
    Player,
    Pickup,
    Trigger,
    Use,
    Goal,
    Hazard,
    SpawnPoint,
    Spawner,
    Other
};

class Role
{
public:
    virtual ~Role() = default;
    virtual RoleKind kind() const = 0;
    virtual const char *fileTag() const = 0;
    virtual bool passThroughSolid() const { return false; }
    virtual bool passThroughLook() const { return false; }
    virtual bool meshBlocks() const { return false; }
    virtual bool carriesPlayer() const { return false; }
    virtual bool watchesOverlap() const { return false; }
    virtual std::shared_ptr<Role> clone() const = 0;
};

std::shared_ptr<Role> makeRole(const std::string &tag);
