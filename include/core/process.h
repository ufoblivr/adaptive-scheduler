#pragma once

#include "state.h"

#include <limits>
#include <memory>
#include <string>

namespace adaptive_sched
{

    inline constexpr Tick NOT_STARTED = std::numeric_limits<Tick>::max();

    // ---------------------------------------------------------------------------
    // Process — the simulated PCB.
    //
    // Timing convention: a process that executes "during tick t" occupies the
    // interval [t, t+1). A process whose last unit of work runs in tick t has
    // completion_time = t + 1. This matches the textbook Gantt-chart convention,
    // so the classic worked examples reproduce exactly (see tests/).
    //
    // The simulator is single-threaded, so the PCB is a plain struct.
    // ---------------------------------------------------------------------------
    struct Process
    {
        // Identity / workload description
        PID pid = 0;
        std::string name;
        Tick arrival_time = 0;
        Tick burst_time = 0;
        int base_priority = 0; // lower number = more urgent
        bool is_interactive = false;

        // Dynamic scheduling state
        Tick remaining_time = 0;
        int priority = 0;        // effective priority (aging may lower it)
        int queue_level = 0;     // MLFQ level, 0 = highest
        Tick slice_used = 0;     // ticks executed since last dispatch
        Tick ready_since = 0;    // when it last entered a ready queue
        Tick aging_credit = 0;   // waiting ticks accumulated toward next aging boost
        ProcessState state = ProcessState::NEW;

        // Outcome
        Tick start_time = NOT_STARTED; // first tick it actually executed
        Tick completion_time = 0;
        Tick waiting_time = 0;    // turnaround - burst
        Tick turnaround_time = 0; // completion - arrival
        Tick response_time = 0;   // start - arrival
        uint32_t dispatches = 0;  // times it was switched onto the CPU
        uint32_t preemptions = 0; // times it was involuntarily removed
        int completed_under = -1; // SchedulerPolicy active at completion (meta-schedulers)

        Process() = default;
        Process(PID p, std::string n, Tick arrival, Tick burst, int prio = 0, bool interactive = false)
            : pid(p), name(std::move(n)), arrival_time(arrival), burst_time(burst),
              base_priority(prio), is_interactive(interactive),
              remaining_time(burst), priority(prio) {}

        bool started() const { return start_time != NOT_STARTED; }

        void finish(Tick now)
        {
            completion_time = now;
            turnaround_time = completion_time - arrival_time;
            waiting_time = turnaround_time - burst_time;
            response_time = start_time - arrival_time;
            state = ProcessState::TERMINATED;
        }

        // Slowdown (a.k.a. normalised turnaround): 1.0 means it never waited.
        double slowdown() const
        {
            return burst_time ? static_cast<double>(turnaround_time) / static_cast<double>(burst_time) : 1.0;
        }
    };

    using ProcessPtr = std::shared_ptr<Process>;

} // namespace adaptive_sched
