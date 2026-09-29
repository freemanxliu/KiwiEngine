#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace Kiwi
{

// ============================================================
// Pass Timer — High-resolution CPU timing for render passes
// ============================================================

struct PassTimingEntry
{
    std::string Name;
    double      TimeMs = 0.0;   // Last measured time in milliseconds
};

class PassTimer
{
public:
    void Begin(const std::string& name)
    {
        CurrentName = name;
        StartTime = Clock::now();
    }

    void End()
    {
        double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - StartTime).count();

        for (auto& entry : Entries)
        {
            if (entry.Name == CurrentName)
            {
                // Smooth with exponential moving average (alpha=0.1)
                entry.TimeMs = entry.TimeMs * 0.9 + elapsed * 0.1;
                return;
            }
        }
        Entries.push_back({ CurrentName, elapsed });
    }

    void BeginFrame()
    {
        FrameTotalMs = 0.0;
    }

    void EndFrame()
    {
        FrameTotalMs = 0.0;
        for (auto& e : Entries)
            FrameTotalMs += e.TimeMs;
    }

    const std::vector<PassTimingEntry>& GetEntries() const { return Entries; }
    double GetFrameTotalMs() const { return FrameTotalMs; }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point StartTime = {};
    std::string   CurrentName;
    std::vector<PassTimingEntry> Entries;
    double FrameTotalMs = 0.0;
};

} // namespace Kiwi
