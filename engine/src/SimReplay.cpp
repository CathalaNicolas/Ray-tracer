#include "SimReplay.hpp"

#include "Scene.hpp"

#include <algorithm>

void replayCommands(Scene &scene, PlayState &state, const std::vector<Command> &commands)
{
    SimSession session;
    session.attach(scene, state);
    session.begin();
    if (commands.empty())
        return;
    SimTick last = 0;
    for (const Command &command : commands)
    {
        session.enqueue(command);
        last = std::max(last, command.tick);
    }
    const SimTick remaining = last > session.tick() ? last - session.tick() : 0;
    if (remaining > 0)
        session.take(static_cast<int>(remaining));
}

void replayFromBlob(Scene &scene, PlayState &state, const SimPlayBlob &blob, const std::vector<Command> &commands)
{
    applySimPlay(scene, state, blob);
    SimSession session;
    session.attach(scene, state);
    session.primeFromCurrent(blob.tick, blob.lastLook);
    SimTick last = blob.tick;
    for (const Command &command : commands)
    {
        if (command.tick <= blob.tick)
            continue;
        session.enqueue(command);
        last = std::max(last, command.tick);
    }
    const SimTick remaining = last > session.tick() ? last - session.tick() : 0;
    if (remaining > 0)
        session.take(static_cast<int>(remaining));
}
