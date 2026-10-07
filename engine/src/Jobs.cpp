#include "Jobs.hpp"

#include <TaskScheduler.h>

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/Memory.h>

#ifndef JPH_DOUBLE_PRECISION
#error Jolt must be built with JPH_DOUBLE_PRECISION
#endif

#include <algorithm>
#include <memory>
#include <thread>
#include <utility>

namespace
{

enki::TaskScheduler &scheduler()
{
    static enki::TaskScheduler instance;
    return instance;
}

bool g_ready = false;
std::unique_ptr<JPH::JobSystemThreadPool> g_joltJobs;

} // namespace

namespace jobs
{

struct PinnedHandle::State
{
    explicit State(std::uint32_t threadNum, std::function<void()> fn)
        : task(threadNum, std::move(fn))
    {
    }

    enki::LambdaPinnedTask task;
};

void init()
{
    if (g_ready)
        return;
    scheduler().Initialize();
    JPH::RegisterDefaultAllocator();
    const int workers = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    g_joltJobs = std::make_unique<JPH::JobSystemThreadPool>(2048, 8, workers);
    g_ready = true;
}

void shutdown()
{
    if (!g_ready)
        return;
    g_joltJobs.reset();
    scheduler().WaitforAllAndShutdown();
    g_ready = false;
}

bool ready()
{
    return g_ready;
}

std::uint32_t threadCount()
{
    return g_ready ? scheduler().GetNumTaskThreads() : 0;
}

JPH::JobSystem *joltJobSystem()
{
    return g_joltJobs.get();
}

void parallelFor(std::uint32_t count, const std::function<void(std::uint32_t begin, std::uint32_t end)> &fn)
{
    if (!g_ready || count == 0 || !fn)
        return;
    if (count == 1)
    {
        fn(0, 1);
        return;
    }

    enki::TaskSet task(count, [&fn](enki::TaskSetPartition range, std::uint32_t) {
        fn(range.start, range.end);
    });
    scheduler().AddTaskSetToPipe(&task);
    scheduler().WaitforTask(&task);
}

void runPinned(const std::function<void()> &fn, std::uint32_t threadNum)
{
    PinnedHandle handle = schedulePinned(fn, threadNum);
    handle.wait();
}

PinnedHandle::PinnedHandle() = default;

PinnedHandle::PinnedHandle(PinnedHandle &&other) noexcept
    : state_(std::move(other.state_))
{
}

PinnedHandle &PinnedHandle::operator=(PinnedHandle &&other) noexcept
{
    if (this != &other)
    {
        if (state_ && !state_->task.GetIsComplete())
            wait();
        state_ = std::move(other.state_);
    }
    return *this;
}

PinnedHandle::~PinnedHandle()
{
    if (state_ && !state_->task.GetIsComplete())
        wait();
}

void PinnedHandle::wait()
{
    if (!state_ || !g_ready)
        return;
    // Pinned work on this thread needs RunPinnedTasks; WaitforTask covers the rest.
    while (!state_->task.GetIsComplete())
    {
        scheduler().RunPinnedTasks();
        if (state_->task.GetIsComplete())
            break;
        scheduler().WaitforTask(&state_->task);
    }
}

bool PinnedHandle::complete() const
{
    return !state_ || state_->task.GetIsComplete();
}

PinnedHandle schedulePinned(std::function<void()> fn, std::uint32_t threadNum)
{
    PinnedHandle handle;
    if (!g_ready || !fn)
        return handle;

    handle.state_ = std::make_unique<PinnedHandle::State>(threadNum, std::move(fn));
    scheduler().AddPinnedTask(&handle.state_->task);
    // If the pinned thread is the caller, pump once so the task can start.
    if (scheduler().GetThreadNum() == threadNum)
        scheduler().RunPinnedTasks();
    return handle;
}

} // namespace jobs
