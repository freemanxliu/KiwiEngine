#include "Core/RenderingThread.h"

#include "RHI/LockedRHIDevice.h"

#include <future>
#include <memory>
#include <utility>

#if defined(__APPLE__)
#include <pthread.h>
// Worker threads have no run loop, so each task drains its own autorelease pool.
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void* Pool);
#endif

namespace Kiwi
{

namespace
{
    RenderingThread* GRenderingThread = nullptr;
}

const char* GetThreadingModeName(EThreadingMode Mode)
{
    switch (Mode)
    {
    case EThreadingMode::SingleThreaded:     return "SingleThreaded";
    case EThreadingMode::RenderThread:       return "RenderThread";
    case EThreadingMode::RenderAndRHIThread: return "RenderAndRHIThread";
    }
    return "Unknown";
}

RenderingThread* GetRenderingThread()
{
    return GRenderingThread;
}

void SetRenderingThread(RenderingThread* Thread)
{
    GRenderingThread = Thread;
}

void EnqueueRenderCommand(std::function<void()> Task)
{
    if (GRenderingThread)
        GRenderingThread->EnqueueRenderCommand(std::move(Task));
    else
        Task();
}

void FlushRenderingCommands()
{
    if (GRenderingThread)
        GRenderingThread->FlushRenderingCommands();
}

// ---- TaskThread ----

void TaskThread::Start(const char* InName)
{
    Name = InName;
    bStopRequested = false;
    Thread = std::thread([this] { Run(); });
    ThreadId = Thread.get_id();
}

void TaskThread::Stop()
{
    if (!Thread.joinable())
        return;
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        bStopRequested = true;
    }
    Condition.notify_all();
    Thread.join();
    ThreadId = {};
}

void TaskThread::Enqueue(std::function<void()> Task)
{
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        Tasks.push_back(std::move(Task));
    }
    Condition.notify_one();
}

void TaskThread::Flush()
{
    if (!Thread.joinable() || IsCurrentThread())
        return;
    auto Done = std::make_shared<std::promise<void>>();
    std::future<void> Finished = Done->get_future();
    Enqueue([Done] { Done->set_value(); });
    Finished.wait();
}

void TaskThread::Run()
{
#if defined(__APPLE__)
    pthread_setname_np(Name);
#endif
    for (;;)
    {
        std::function<void()> Task;
        {
            std::unique_lock<std::mutex> Lock(Mutex);
            Condition.wait(Lock, [this] { return bStopRequested || !Tasks.empty(); });
            if (Tasks.empty())
                return;
            Task = std::move(Tasks.front());
            Tasks.pop_front();
        }
#if defined(__APPLE__)
        void* Pool = objc_autoreleasePoolPush();
        Task();
        objc_autoreleasePoolPop(Pool);
#else
        Task();
#endif
    }
}

// ---- RenderingThread ----

void RenderingThread::Start(EThreadingMode InMode, RHICommandContext* InContext, RHIDevice* InBackendDevice)
{
    Mode = InMode;
    Context = InContext;
    BackendDevice = InBackendDevice;
    GameFramesEnded = 0;
    RenderFramesCompleted = 0;

    if (HasRHIThread())
    {
        CommandList.SetRecording(BackendDevice);
        RHIWorker.Start("Kiwi RHIThread");
    }
    else
    {
        CommandList.SetImmediate(Context, BackendDevice);
    }

    if (HasRenderThread())
    {
        RenderWorker.Start("Kiwi RenderThread");
        RenderWorker.Enqueue([this] { RHICommandList::SetCurrent(&CommandList); });
    }
    else
    {
        RHICommandList::SetCurrent(&CommandList);
    }
    SetRenderingThread(this);
}

void RenderingThread::Stop()
{
    if (GRenderingThread != this)
        return;
    FlushRenderingCommands();
    if (HasRenderThread())
        RenderWorker.Enqueue([] { RHICommandList::SetCurrent(nullptr); });
    else
        RHICommandList::SetCurrent(nullptr);
    RenderWorker.Stop();
    RHIWorker.Stop();
    SetRenderingThread(nullptr);
}

bool RenderingThread::IsInRenderingThread() const
{
    return HasRenderThread() ? RenderWorker.IsCurrentThread() : true;
}

void RenderingThread::EnqueueRenderCommand(std::function<void()> Task)
{
    if (!HasRenderThread())
    {
        Task();
        return;
    }
    if (HasRHIThread())
    {
        RenderWorker.Enqueue(std::move(Task));
        return;
    }
    // Without an RHI thread the render thread drives the backend context itself.
    RenderWorker.Enqueue([Task = std::move(Task)] {
        std::lock_guard<std::recursive_mutex> Guard(GetRHILock());
        Task();
    });
}

void RenderingThread::FlushRenderingCommands()
{
    if (HasRenderThread())
        RenderWorker.Flush();
    if (HasRHIThread())
        RHIWorker.Flush();
}

void RenderingThread::FrameEndSync()
{
    if (!HasRenderThread())
        return;
    uint64_t Frame = 0;
    {
        std::lock_guard<std::mutex> Lock(FrameMutex);
        Frame = ++GameFramesEnded;
    }
    RenderWorker.Enqueue([this, Frame] {
        {
            std::lock_guard<std::mutex> Lock(FrameMutex);
            RenderFramesCompleted = Frame;
        }
        FrameCondition.notify_all();
    });
    std::unique_lock<std::mutex> Lock(FrameMutex);
    FrameCondition.wait(Lock, [this, Frame] { return RenderFramesCompleted + 1 >= Frame; });
}

void RenderingThread::SubmitCommandList(RHISwapChain* SwapChain)
{
    if (!HasRHIThread())
        return;
    RHIWorker.Flush();
    auto Commands = std::make_shared<RHICommandBuffer>(CommandList.TakeCommands());
    RHIWorker.Enqueue([this, Commands, SwapChain] { ExecuteOnRHIThread(std::move(*Commands), SwapChain); });
}

void RenderingThread::WaitForRHIThread()
{
    if (HasRHIThread())
        RHIWorker.Flush();
}

void RenderingThread::ExecuteOnRHIThread(RHICommandBuffer Commands, RHISwapChain* SwapChain)
{
    std::lock_guard<std::recursive_mutex> Guard(GetRHILock());
    RHIExecuteContext Exec;
    Exec.Ctx = Context;
    Exec.Device = BackendDevice;
    Exec.SwapChain = SwapChain;
    Commands.Execute(Exec);
}

} // namespace Kiwi
