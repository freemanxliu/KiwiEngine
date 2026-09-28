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
        m_CurrentName = name;
        m_StartTime = Clock::now();
    }

    void End()
    {
        double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - m_StartTime).count();

        for (auto& entry : m_Entries)
        {
            if (entry.Name == m_CurrentName)
            {
                // Smooth with exponential moving average (alpha=0.1)
                entry.TimeMs = entry.TimeMs * 0.9 + elapsed * 0.1;
                return;
            }
        }
        m_Entries.push_back({ m_CurrentName, elapsed });
    }

    void BeginFrame()
    {
        m_FrameTotalMs = 0.0;
    }

    void EndFrame()
    {
        m_FrameTotalMs = 0.0;
        for (auto& e : m_Entries)
            m_FrameTotalMs += e.TimeMs;
    }

    const std::vector<PassTimingEntry>& GetEntries() const { return m_Entries; }
    double GetFrameTotalMs() const { return m_FrameTotalMs; }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_StartTime = {};
    std::string   m_CurrentName;
    std::vector<PassTimingEntry> m_Entries;
    double m_FrameTotalMs = 0.0;
};

} // namespace Kiwi
