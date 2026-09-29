#pragma once

#include "RHI/RHICommandList.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace Kiwi
{
    // [Rendering] ThreadingMode in DefaultEngine.ini.
    enum class EThreadingMode : uint8_t
    {
        SingleThreaded,    // game, render and RHI work all run inline on the main thread
        RenderThread,      // render thread records straight into the RHI context
        RenderAndRHIThread // render thread records a command list, the RHI thread replays it
    };

    const char* GetThreadingModeName(EThreadingMode Mode);

    // A worker thread that runs queued tasks in order.
    class TaskThread
    {
    public:
        void Start(const char* InName);
        void Stop();
        bool IsRunning() const { return Thread.joinable(); }
        bool IsCurrentThread() const { return std::this_thread::get_id() == ThreadId; }

        void Enqueue(std::function<void()> Task);
        // Blocks until every task queued before this call has finished.
        void Flush();

    private:
        void Run();

        const char* Name = "";
        std::thread Thread;
        std::thread::id ThreadId;
        std::mutex Mutex;
        std::condition_variable Condition;
        std::deque<std::function<void()>> Tasks;
        bool bStopRequested = false;
    };

    // Owns the render and RHI threads and the command list the renderer records into
    // (UE5 FRenderingThread, FRHIThread and the render command fences).
    class RenderingThread
    {
    public:
        void Start(EThreadingMode InMode, RHICommandContext* InContext, RHIDevice* InBackendDevice);
        // Finishes all queued work, then joins the threads.
        void Stop();

        EThreadingMode GetMode() const { return Mode; }
        bool HasRenderThread() const { return Mode != EThreadingMode::SingleThreaded; }
        bool HasRHIThread() const { return Mode == EThreadingMode::RenderAndRHIThread; }
        bool IsInRenderingThread() const;

        // ---- Game thread ----
        void EnqueueRenderCommand(std::function<void()> Task);
        // Waits until the render thread and the RHI thread have finished everything queued so far.
        // Required before the game thread creates or destroys resources the renderer uses.
        void FlushRenderingCommands();
        // Marks the end of a game frame. Returns once the render thread has finished the previous frame,
        // so the game thread runs at most one frame ahead (UE5 FFrameEndSync).
        void FrameEndSync();

        // ---- Render thread ----
        RHICommandList& GetCommandList() { return CommandList; }
        // Sends the recorded frame to the RHI thread after the previous frame has been replayed,
        // so the render thread also stays at most one frame ahead of the RHI thread.
        void SubmitCommandList(RHISwapChain* SwapChain);
        // Waits until the RHI thread has replayed every submitted frame. Required before the render thread
        // destroys or recreates resources that submitted frames may still reference.
        void WaitForRHIThread();

    private:
        void ExecuteOnRHIThread(RHICommandBuffer Commands, RHISwapChain* SwapChain);

        EThreadingMode Mode = EThreadingMode::SingleThreaded;
        RHICommandContext* Context = nullptr;
        RHIDevice* BackendDevice = nullptr;
        RHICommandList CommandList;
        TaskThread RenderWorker;
        TaskThread RHIWorker;

        std::mutex FrameMutex;
        std::condition_variable FrameCondition;
        uint64_t GameFramesEnded = 0;
        uint64_t RenderFramesCompleted = 0;
    };

    // The engine's rendering thread manager, set by Application while the RHI exists.
    RenderingThread* GetRenderingThread();
    void SetRenderingThread(RenderingThread* Thread);

    // Runs Task on the render thread, or inline when rendering is single-threaded (UE5 ENQUEUE_RENDER_COMMAND).
    void EnqueueRenderCommand(std::function<void()> Task);
    void FlushRenderingCommands();
}
