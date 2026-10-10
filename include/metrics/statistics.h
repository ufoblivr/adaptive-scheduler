#pragma once

#include "../execution/cpu.h"

#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace adaptive_sched
{

    struct Summary
    {
        std::string scenario;
        std::string policy;
        std::size_t processes = 0;       // completed
        std::size_t processes_total = 0; // in the workload (differs only if --max-ticks cut the run short)

        // Time accounting (ticks)
        Tick makespan = 0, busy_ticks = 0, switch_ticks = 0, idle_ticks = 0;
        double cpu_utilization = 0;  // useful work / makespan
        double switch_overhead = 0;  // switch ticks / makespan
        double throughput = 0;       // completed processes per 1000 ticks

        // Per-process latency
        double avg_waiting = 0, p50_waiting = 0, p95_waiting = 0, p99_waiting = 0, max_waiting = 0;
        double avg_turnaround = 0;
        double avg_response = 0, p95_response = 0;
        double avg_response_interactive = 0; // NaN-free: 0 if none interactive
        double avg_slowdown = 0, p95_slowdown = 0;

        // Jain's fairness index over x_i = burst_i / turnaround_i (share of its
        // time in the system that each process spent running). 1 = perfectly even.
        double jain_fairness = 0;

        uint64_t context_switches = 0;
        uint64_t preemptions = 0;
        uint64_t policy_switches = 0;              // meta-schedulers only
        std::map<std::string, double> policy_share; // fraction of ticks under each policy
    };

    class Statistics
    {
    public:
        static Summary summarize(const std::string &scenario, const std::string &policy,
                                 const std::vector<ProcessPtr> &completed, const RunCounters &c,
                                 uint64_t policy_switches = 0);

        static double percentile(std::vector<double> v, double q); // q in [0,1], linear interpolation
        static double jain_index(const std::vector<double> &x);

        static void print_summary(std::ostream &os, const Summary &s);
        static void write_json(std::ostream &os, const Summary &s);

        static void export_process_csv(const std::string &path, const std::vector<ProcessPtr> &completed);
        static void export_timeline_csv(const std::string &path, const std::vector<TimelineSegment> &tl);
    };

} // namespace adaptive_sched
