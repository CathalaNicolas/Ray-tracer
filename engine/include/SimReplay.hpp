#pragma once

#include "SimChannel.hpp"
#include "SimSerialize.hpp"

// Headless replay: enqueue the recorded command list and take one tick at a
// time. Wall-clock hitch leftover is not part of the log (live play drops it).

void replayCommands(Scene &scene, PlayState &state, const std::vector<Command> &commands);

// Apply a captured sim blob, then replay commands with tick > blob.tick.
void replayFromBlob(Scene &scene, PlayState &state, const SimPlayBlob &blob, const std::vector<Command> &commands);
