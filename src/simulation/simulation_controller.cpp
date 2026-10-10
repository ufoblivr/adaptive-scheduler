#include "../../include/simulation/simulation_controller.h"

#include "../../include/execution/cpu.h"
#include "../../include/scheduler/adaptive.h"
#include "../../include/scheduler/bandit.h"
#include "../../include/scheduler/fcfs.h"
#include "../../include/scheduler/mlfq.h"
#include "../../include/scheduler/priority.h"
#include "../../include/scheduler/rr.h"
#include "../../include/scheduler/sjf.h"

#include <filesystem>
#include <stdexcept>

namespace adaptive_sched
{

    std::unique_ptr<IScheduler> make_scheduler(const SimConfig &cfg)
    {
        MetaScheduler::Config meta;
        meta.rr_quantum = cfg.rr_quantum;
        meta.aging_interval = cfg.aging_interval;

        switch (cfg.policy)
        {
        case SchedulerPolicy::FCFS:
            return std::make_unique<FCFSScheduler>();
        case SchedulerPolicy::SJF:
            return std::make_unique<SJFScheduler>(SJFScheduler::Mode::NON_PREEMPTIVE);
        case SchedulerPolicy::SRTF:
            return std::make_unique<SJFScheduler>(SJFScheduler::Mode::SRTF);
        case SchedulerPolicy::ROUND_ROBIN:
            return std::make_unique<RRScheduler>(cfg.rr_quantum);
        case SchedulerPolicy::PRIORITY:
            return std::make_unique<PriorityScheduler>(cfg.aging_interval);
        case SchedulerPolicy::MLFQ:
            return std::make_unique<MLFQScheduler>();
        case SchedulerPolicy::ADAPTIVE:
            return std::make_unique<AdaptiveScheduler>(meta, AdaptiveScheduler::Rules{});
        case SchedulerPolicy::BANDIT:
        {
            meta.cooldown = 400;
            BanditScheduler::Params bp;
            bp.learn = cfg.bandit_train;
            bp.seed = cfg.seed * 7919u + 17u;
            auto b = std::make_unique<BanditScheduler>(meta, bp);
            if (!cfg.bandit_model.empty() && !b->load_model(cfg.bandit_model) && !cfg.bandit_train)
                throw std::runtime_error("Cannot read bandit model: " + cfg.bandit_model +
                                         " (train one with --bandit-train)");
            return b;
        }
        }
        throw std::invalid_argument("unknown policy");
    }

    Summary SimulationController::run(std::ostream &out)
    {
        std::vector<ProcessDescriptor> descs;
        std::string scenario;
        if (!cfg_.csv_path.empty())
        {
            descs = WorkloadLoader::from_csv(cfg_.csv_path);
            if (cfg_.count > 0 && static_cast<std::size_t>(cfg_.count) < descs.size())
                descs.resize(static_cast<std::size_t>(cfg_.count));
            scenario = std::filesystem::path(cfg_.csv_path).stem().string();
        }
        else
        {
            descs = WorkloadLoader::generate(WorkloadLoader::profile(cfg_.workload), cfg_.seed, cfg_.count, cfg_.load);
            scenario = cfg_.workload;
        }

        if (!cfg_.dump_workload.empty())
        {
            WorkloadLoader::to_csv(descs, cfg_.dump_workload);
            if (!cfg_.quiet)
                out << "Wrote " << descs.size() << " processes to " << cfg_.dump_workload << "\n";
            Summary s;
            s.scenario = scenario;
            s.processes_total = descs.size();
            return s;
        }

        auto sched = make_scheduler(cfg_);

        CPUConfig cpu;
        cpu.context_switch_overhead = cfg_.context_switch_overhead;
        cpu.max_ticks = cfg_.max_ticks;
        cpu.record_timeline = cfg_.export_timeline;
        CPUEngine engine(*sched, cpu);
        completed_ = engine.run(WorkloadLoader::instantiate(descs));

        auto *meta = dynamic_cast<MetaScheduler *>(sched.get());
        uint64_t switches = meta ? meta->switch_history().size() : 0;
        Summary s = Statistics::summarize(scenario, sched->name(), completed_, engine.counters(), switches);
        s.processes_total = descs.size();

        if (auto *bandit = dynamic_cast<BanditScheduler *>(sched.get()); bandit && cfg_.bandit_train && !cfg_.bandit_model.empty())
            bandit->save_model(cfg_.bandit_model);

        if (!cfg_.quiet)
        {
            Statistics::print_summary(out, s);
            if (meta && !meta->switch_history().empty())
            {
                out << "\nPolicy switches:\n";
                for (const auto &r : meta->switch_history())
                    out << "  t=" << r.at << "  " << to_string(r.from) << " -> " << to_string(r.to)
                        << "  [" << to_string(r.type) << "]  " << r.reason << "\n";
            }
        }
        if (meta && cfg_.trace)
        {
            out << "\n" << meta->decision_trace(1000);
            if (auto *bandit = dynamic_cast<BanditScheduler *>(meta))
                out << "\nQ-table:\n" << bandit->q_table();
        }

        if (cfg_.export_csv || cfg_.export_timeline)
            std::filesystem::create_directories(cfg_.output_dir);
        if (cfg_.export_csv)
            Statistics::export_process_csv(cfg_.output_dir + "/metrics.csv", completed_);
        if (cfg_.export_timeline)
            Statistics::export_timeline_csv(cfg_.output_dir + "/timeline.csv", engine.timeline());

        return s;
    }

} // namespace adaptive_sched
