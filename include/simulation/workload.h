#pragma once

#include "../core/process.h"

#include <cstdint>
#include <string>
#include <vector>

namespace adaptive_sched
{

    struct ProcessDescriptor
    {
        PID pid = 0;
        std::string name;
        Tick arrival = 0;
        Tick burst = 1;
        int priority = 5;
        bool interactive = false;
    };

    // How CPU-burst lengths are drawn within one phase
    enum class BurstDist
    {
        NORMAL,    // mean a, stddev b               (homogeneous)
        UNIFORM,   // integer uniform [a, b]
        LOGNORMAL, // mean a, coefficient of variation b (heavy tail)
        BIMODAL    // short U[a,b] with prob p_short, long U[c,d] otherwise
    };

    // One stationary segment of a workload
    struct Phase
    {
        std::string label;
        int count = 100;      // processes in this phase
        double load = 0.9;    // target offered load ρ = arrival rate × mean burst
        BurstDist dist = BurstDist::NORMAL;
        double a = 10, b = 3, c = 0, d = 0;
        double p_short = 0.0;
        Tick burst_min = 1, burst_max = 1000;
        Tick interactive_max_burst = 0; // bursts ≤ this are flagged interactive (0 = none)
    };

    struct WorkloadProfile
    {
        std::string name;
        std::string description;
        std::vector<Phase> phases;
    };

    class WorkloadLoader
    {
    public:
        // Built-in profiles: interactive, cpu_bound, batch, mixed, heavy_tail,
        // stress, phased. Throws on an unknown name.
        static WorkloadProfile profile(const std::string &name);
        static std::vector<std::string> profile_names();

        // Poisson arrivals at the phase's target load; seeded → reproducible.
        // count_override > 0 rescales every phase proportionally;
        // load_override > 0 replaces every phase's load.
        static std::vector<ProcessDescriptor> generate(const WorkloadProfile &profile, uint32_t seed,
                                                       int count_override = 0, double load_override = 0.0);

        // CSV format: pid,name,arrival,burst,priority,interactive
        static std::vector<ProcessDescriptor> from_csv(const std::string &path);
        static void to_csv(const std::vector<ProcessDescriptor> &descs, const std::string &path);

        static std::vector<ProcessPtr> instantiate(const std::vector<ProcessDescriptor> &descs);
    };

} // namespace adaptive_sched
