#pragma once

#include "../metrics/statistics.h"
#include "../scheduler/scheduler.h"
#include "workload.h"

#include <memory>
#include <ostream>
#include <string>

namespace adaptive_sched
{

    struct SimConfig
    {
        // Workload
        std::string workload = "mixed"; // built-in profile name
        std::string csv_path;           // overrides `workload` if set
        int count = 0;                  // 0 = profile default
        double load = 0.0;              // 0 = profile default
        uint32_t seed = 42;

        // Policy
        SchedulerPolicy policy = SchedulerPolicy::ADAPTIVE;
        Tick rr_quantum = 20;
        Tick aging_interval = 100;
        Tick context_switch_overhead = 1;
        Tick max_ticks = 0;

        // Bandit
        std::string bandit_model;  // Q-table file to load (and save when training)
        bool bandit_train = false;

        // Output
        std::string output_dir = "output";
        bool export_csv = false;
        bool export_timeline = false;
        bool json = false;   // print one-line JSON summary to stdout
        bool quiet = false;
        bool trace = false;  // print the meta-scheduler decision trace
        std::string dump_workload; // write the generated workload CSV here and exit
    };

    std::unique_ptr<IScheduler> make_scheduler(const SimConfig &cfg);

    // Runs one complete, deterministic simulation.
    class SimulationController
    {
    public:
        explicit SimulationController(SimConfig cfg) : cfg_(std::move(cfg)) {}

        // Returns the run summary; human-readable output goes to `out`.
        Summary run(std::ostream &out);

        const std::vector<ProcessPtr> &completed() const { return completed_; }

    private:
        SimConfig cfg_;
        std::vector<ProcessPtr> completed_;
    };

} // namespace adaptive_sched
