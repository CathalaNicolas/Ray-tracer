#include "Role.hpp"

namespace
{

class SolidRole : public Role
{
public:
    explicit SolidRole(const char *tag, bool carry = false)
        : tag_(tag), carry_(carry)
    {
    }

    RoleKind kind() const override { return RoleKind::Solid; }
    const char *fileTag() const override { return tag_; }
    bool meshBlocks() const override { return true; }
    bool carriesPlayer() const override { return carry_; }
    std::shared_ptr<Role> clone() const override { return std::make_shared<SolidRole>(tag_, carry_); }

private:
    const char *tag_;
    bool carry_;
};

class PlayerRole : public Role
{
public:
    RoleKind kind() const override { return RoleKind::Player; }
    const char *fileTag() const override { return "player"; }
    bool passThroughSolid() const override { return true; }
    bool passThroughLook() const override { return true; }
    std::shared_ptr<Role> clone() const override { return std::make_shared<PlayerRole>(); }
};

class PickupRole : public Role
{
public:
    RoleKind kind() const override { return RoleKind::Pickup; }
    const char *fileTag() const override { return "pickup"; }
    bool passThroughSolid() const override { return true; }
    bool passThroughLook() const override { return true; }
    bool watchesOverlap() const override { return true; }
    std::shared_ptr<Role> clone() const override { return std::make_shared<PickupRole>(); }
};

class TriggerRole : public Role
{
public:
    RoleKind kind() const override { return RoleKind::Trigger; }
    const char *fileTag() const override { return "trigger"; }
    bool passThroughSolid() const override { return true; }
    bool passThroughLook() const override { return true; }
    bool watchesOverlap() const override { return true; }
    std::shared_ptr<Role> clone() const override { return std::make_shared<TriggerRole>(); }
};

class UseRole : public Role
{
public:
    RoleKind kind() const override { return RoleKind::Use; }
    const char *fileTag() const override { return "use"; }
    bool passThroughSolid() const override { return true; }
    bool watchesOverlap() const override { return true; }
    std::shared_ptr<Role> clone() const override { return std::make_shared<UseRole>(); }
};

class GoalRole : public Role
{
public:
    RoleKind kind() const override { return RoleKind::Goal; }
    const char *fileTag() const override { return "goal"; }
    bool passThroughSolid() const override { return true; }
    bool watchesOverlap() const override { return true; }
    std::shared_ptr<Role> clone() const override { return std::make_shared<GoalRole>(); }
};

class HazardRole : public Role
{
public:
    RoleKind kind() const override { return RoleKind::Hazard; }
    const char *fileTag() const override { return "hazard"; }
    bool passThroughSolid() const override { return true; }
    bool passThroughLook() const override { return true; }
    bool watchesOverlap() const override { return true; }
    std::shared_ptr<Role> clone() const override { return std::make_shared<HazardRole>(); }
};

class SpawnPointRole : public Role
{
public:
    RoleKind kind() const override { return RoleKind::SpawnPoint; }
    const char *fileTag() const override { return "spawn"; }
    bool passThroughSolid() const override { return true; }
    bool passThroughLook() const override { return true; }
    std::shared_ptr<Role> clone() const override { return std::make_shared<SpawnPointRole>(); }
};

class SpawnerRole : public Role
{
public:
    RoleKind kind() const override { return RoleKind::Spawner; }
    const char *fileTag() const override { return "spawner"; }
    bool passThroughSolid() const override { return true; }
    bool passThroughLook() const override { return true; }
    std::shared_ptr<Role> clone() const override { return std::make_shared<SpawnerRole>(); }
};

class OtherRole : public Role
{
public:
    explicit OtherRole(std::string tag) : tag_(std::move(tag)) {}

    RoleKind kind() const override { return RoleKind::Other; }
    const char *fileTag() const override { return tag_.c_str(); }
    std::shared_ptr<Role> clone() const override { return std::make_shared<OtherRole>(tag_); }

private:
    std::string tag_;
};

}

std::shared_ptr<Role> makeRole(const std::string &tag)
{
    if (tag.empty() || tag == "solid")
        return std::make_shared<SolidRole>(tag.empty() ? "" : "solid");
    if (tag == "platform")
        return std::make_shared<SolidRole>("platform", true);
    if (tag == "player")
        return std::make_shared<PlayerRole>();
    if (tag == "pickup")
        return std::make_shared<PickupRole>();
    if (tag == "trigger")
        return std::make_shared<TriggerRole>();
    if (tag == "use")
        return std::make_shared<UseRole>();
    if (tag == "goal")
        return std::make_shared<GoalRole>();
    if (tag == "hazard")
        return std::make_shared<HazardRole>();
    if (tag == "spawn")
        return std::make_shared<SpawnPointRole>();
    if (tag == "spawner")
        return std::make_shared<SpawnerRole>();
    return std::make_shared<OtherRole>(tag);
}
