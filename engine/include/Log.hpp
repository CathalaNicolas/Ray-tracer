#pragma once

// Narrow process-lifetime wrapper over spdlog (console + file).
namespace logging
{

void init();
void shutdown();

} // namespace logging
