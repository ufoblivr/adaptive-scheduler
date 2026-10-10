#pragma once

#include "../scheduler/scheduler.h"

#include <array>
#include <vector>

namespace adaptive_sched
{

    struct CPUConfig
    {
        Tick context_switch_overhead = 1; // ticks lost every time a different process is dispatched
        Tick max_ticks = 0;               // 0 = run until every process completes
        bool record_timeline = false;     // keep a Gantt-style segment list
    };

    struct TimelineSegment
    {
        Tick start = 0;
        Tick end = 0; // exclusive
        TickKind kind = TickKind::IDLE;
        PID pid = 0; // 0 when idle
        SchedulerPolicy policy = SchedulerPolicy::FCFS;
    };

    struct RunCounters
    {
        Tick makespan = 0;     // ticks simulated
        Tick busy_ticks = 0;   // ticks spent executing processes
        Tick switch_ticks = 0; // ticks lost to context-switch overhead
        Tick idle_ticks = 0;   // ticks with nothing to run
        uint64_t context_switches = 0;
        uint64_t preemptions = 0;
        std::array<Tick, 8> ticks_by_policy{}; // indexed by SchedulerPolicy
    };

    // ---------------------------------------------------------------------------
    // CPUEngine — the deterministic, single-threaded execution loop.
    //
    // A discrete-time simulation has a single logical clock, so there is no
    // concurrency to exploit inside one run: every tick depends on the previous
    // one. Keeping the loop single-threaded makes runs bit-for-bit reproducible
    // (same input → same output), which is what makes benchmark comparisons
    // meaningful. Parallelism lives one level up: the benchmark runs many
    // independent simulations at once.
    //
    // Context-switch cost: whenever the CPU is handed to a process other than
    // the one that ran last, the CPU spends `context_switch_overhead` ticks doing
    // no useful work. Re-dispatching the same process (e.g. RR quantum expiry
    // with an empty queue) is free.
    // ---------------------------------------------------------------------------
    class CPUEngine
    {
    public:
        CPUEngine(IScheduler &scheduler, CPUConfig cfg = {});

        // Simulate until every process completes (or max_ticks). Returns the
        // processes in completion order.
        std::vector<ProcessPtr> run(std::vector<ProcessPtr> processes);

        const RunCounters &counters() const { return counters_; }
        const std::vector<TimelineSegment> &timeline() const { return timeline_; }

    private:
        ProcessPtr dispatch(ProcessPtr next);
        void record(Tick t, TickKind kind, const Process *p);

        IScheduler &sched_;
        CPUConfig cfg_;
        RunCounters counters_;
        std::vector<TimelineSegment> timeline_;

        ProcessPtr last_ran_;
        Tick switch_left_ = 0;
    };

} // namespace adaptive_sched
