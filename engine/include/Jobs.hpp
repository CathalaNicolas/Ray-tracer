#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace JPH
{
class JobSystem;
}

// Narrow process-lifetime wrapper over enkiTS.
namespace jobs
{

void init();
void shutdown();
bool ready();

// Jolt JobSystemThreadPool. Null until init(). PhysicsSystem::Update uses this.
JPH::JobSystem *joltJobSystem();

std::uint32_t threadCount();

// Split [0, count) across the worker pool and wait until every range finishes.
void parallelFor(std::uint32_t count, const std::function<void(std::uint32_t begin, std::uint32_t end)> &fn);

// Run fn on a pinned enkiTS thread and wait. threadNum 0 is the thread that called init
// (the usual main/app thread). Use this for work that must not land on an arbitrary worker
// (async file reads are the intended call pattern).
void runPinned(const std::function<void()> &fn, std::uint32_t threadNum = 0);

// Schedule a pinned task without waiting. Keep the handle alive until wait() returns.
class PinnedHandle
{
public:
    PinnedHandle();
    PinnedHandle(PinnedHandle &&) noexcept;
    PinnedHandle &operator=(PinnedHandle &&) noexcept;
    ~PinnedHandle();

    PinnedHandle(const PinnedHandle &) = delete;
    PinnedHandle &operator=(const PinnedHandle &) = delete;

    void wait();
    bool complete() const;

private:
    friend PinnedHandle schedulePinned(std::function<void()> fn, std::uint32_t threadNum);
    struct State;
    std::unique_ptr<State> state_;
};

PinnedHandle schedulePinned(std::function<void()> fn, std::uint32_t threadNum = 0);

} // namespace jobs
