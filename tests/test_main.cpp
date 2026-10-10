// Self-contained unit tests (no external framework needed): build target
// `sched_tests`, run via `ctest` or directly.

#include "../include/execution/cpu.h"
#include "../include/metrics/statistics.h"
#include "../include/scheduler/adaptive.h"
#include "../include/scheduler/bandit.h"
#include "../include/scheduler/fcfs.h"
#include "../include/scheduler/mlfq.h"
#include "../include/scheduler/priority.h"
#include "../include/scheduler/rr.h"
#include "../include/scheduler/sjf.h"
#include "../include/simulation/simulation_controller.h"
#include "../include/simulation/workload.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace adaptive_sched;

// ── minimal test harness ─────────────────────────────────────────────────────
namespace
{
    struct TestCase
    {
        std::string name;
        std::function<void()> fn;
    };
    std::vector<TestCase> &registry()
    {
        static std::vector<TestCase> r;
        return r;
    }
    int g_failures = 0;
    struct Registrar
    {
        Registrar(const char *n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
    };
} // namespace

#define TEST(name)                                     \
    static void name();                                \
    static Registrar reg_##name(#name, name);          \
    static void name()

#define CHECK(cond)                                                                  \
    do                                                                               \
    {                                                                                \
        if (!(cond))                                                                 \
        {                                                                            \
            ++g_failures;                                                            \
            std::cerr << "    FAILED " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        }                                                                            \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                    \
    do                                                                                           \
    {                                                                                            \
        double _a = (a), _b = (b);                                                               \
        if (std::fabs(_a - _b) > (eps))                                                          \
        {                                                                                        \
            ++g_failures;                                                                        \
            std::cerr << "    FAILED " << __FILE__ << ":" << __LINE__ << "  " #a " = " << _a     \
                      << ", expected " << _b << "\n";                                            \
        }                                                                                        \
    } while (0)

// ── helpers ──────────────────────────────────────────────────────────────────
namespace
{
    struct P
    {
        Tick arrival, burst;
        int priority = 5;
    };

    struct Result
    {
        std::vector<ProcessPtr> done; // sorted by pid
        RunCounters c;
        double avg_wait() const
        {
            double s = 0;
            for (auto &p : done)
                s += static_cast<double>(p->waiting_time);
            return s / static_cast<double>(done.size());
        }
        const Process &pid(PID id) const { return *done[id - 1]; }
    };

    Result simulate(IScheduler &s, const std::vector<P> &ps, Tick overhead = 0)
    {
        std::vector<ProcessPtr> procs;
        PID id = 0;
        for (const auto &p : ps)
        {
            ++id;
            procs.push_back(std::make_shared<Process>(id, "P" + std::to_string(id), p.arrival, p.burst, p.priority));
        }
        CPUConfig cfg;
        cfg.context_switch_overhead = overhead;
        CPUEngine e(s, cfg);
        Result r;
        r.done = e.run(procs);
        std::sort(r.done.begin(), r.done.end(), [](auto &a, auto &b)
                  { return a->pid < b->pid; });
        r.c = e.counters();
        return r;
    }

    SimConfig quiet_cfg(const std::string &workload, SchedulerPolicy pol, uint32_t seed)
    {
        SimConfig c;
        c.workload = workload;
        c.policy = pol;
        c.seed = seed;
        c.quiet = true;
        return c;
    }
} // namespace

// ── textbook examples (Silberschatz, "Operating System Concepts", ch. 5) ─────

TEST(fcfs_convoy_example)
{
    FCFSScheduler s;
    auto r = simulate(s, {{0, 24}, {0, 3}, {0, 3}});
    CHECK(r.pid(1).waiting_time == 0);
    CHECK(r.pid(2).waiting_time == 24);
    CHECK(r.pid(3).waiting_time == 27);
    CHECK_NEAR(r.avg_wait(), 17.0, 1e-9);
    CHECK(r.c.preemptions == 0);
}

TEST(sjf_example)
{
    SJFScheduler s(SJFScheduler::Mode::NON_PREEMPTIVE);
    auto r = simulate(s, {{0, 6}, {0, 8}, {0, 7}, {0, 3}});
    CHECK_NEAR(r.avg_wait(), 7.0, 1e-9);
    CHECK(r.pid(4).start_time == 0);
}

TEST(srtf_example)
{
    SJFScheduler s(SJFScheduler::Mode::SRTF);
    auto r = simulate(s, {{0, 8}, {1, 4}, {2, 9}, {3, 5}});
    CHECK_NEAR(r.avg_wait(), 6.5, 1e-9);
    CHECK(r.pid(1).preemptions == 1);
    CHECK(r.pid(2).completion_time == 5);
}

TEST(round_robin_example)
{
    RRScheduler s(4);
    auto r = simulate(s, {{0, 24}, {0, 3}, {0, 3}});
    CHECK_NEAR(r.avg_wait(), 17.0 / 3.0, 1e-9);
    CHECK(r.pid(1).completion_time == 30);
    CHECK(r.pid(1).preemptions == 1); // only once: afterwards it runs alone
}

TEST(priority_preemptive_example)
{
    PriorityScheduler s(/*aging=*/0);
    // P1(t0,b4,p2) preempted by P2(t1,b2,p1); P3(t2,b1,p3) runs last
    auto r = simulate(s, {{0, 4, 2}, {1, 2, 1}, {2, 1, 3}});
    CHECK(r.pid(1).waiting_time == 2);
    CHECK(r.pid(2).waiting_time == 0);
    CHECK(r.pid(3).waiting_time == 4);
    CHECK(r.pid(1).preemptions == 1);
}

// ── mechanism ────────────────────────────────────────────────────────────────

TEST(context_switch_overhead_is_charged)
{
    FCFSScheduler s;
    auto r = simulate(s, {{0, 5}, {0, 3}, {0, 2}}, /*overhead=*/2);
    CHECK(r.c.context_switches == 2);
    CHECK(r.c.switch_ticks == 4);
    CHECK(r.c.makespan == 10 + 4);
    CHECK(r.pid(2).start_time == 7); // 5 ticks of P1 + 2 switch ticks
    CHECK(r.pid(2).waiting_time == 7);
}

TEST(rr_alone_never_switches)
{
    RRScheduler s(2);
    auto r = simulate(s, {{0, 15}}, 1);
    CHECK(r.c.context_switches == 0);
    CHECK(r.c.preemptions == 0);
    CHECK(r.pid(1).completion_time == 15);
}

TEST(mlfq_new_short_job_preempts_demoted_long_job)
{
    // quanta {10,20,40,inf}: by t=25 the long job has been demoted to level 1
    MLFQScheduler s(/*boost=*/0);
    auto r = simulate(s, {{0, 200}, {25, 2}});
    CHECK(r.pid(2).start_time == 25);
    CHECK(r.pid(2).completion_time == 27);
    CHECK(r.pid(1).preemptions >= 1);
}

TEST(mlfq_boost_happens)
{
    MLFQScheduler s(/*boost=*/100);
    auto r = simulate(s, {{0, 300}, {0, 300}});
    CHECK(s.boosts() >= 5);
    CHECK(r.done.size() == 2);
}

TEST(priority_aging_prevents_starvation)
{
    // One low-priority job and a stream of urgent jobs that saturates the CPU
    std::vector<P> ps = {{0, 5, 9}};
    for (Tick t = 0; t < 400; t += 2)
        ps.push_back({t, 2, 0});

    PriorityScheduler no_aging(0);
    auto starved = simulate(no_aging, ps);
    PriorityScheduler aging(20);
    auto rescued = simulate(aging, ps);

    CHECK(starved.pid(1).completion_time > 400);
    CHECK(rescued.pid(1).completion_time < 300);
}

// ── properties over generated workloads ─────────────────────────────────────

TEST(conservation_and_accounting_all_policies)
{
    const std::vector<SchedulerPolicy> pols = {
        SchedulerPolicy::FCFS, SchedulerPolicy::SJF, SchedulerPolicy::SRTF, SchedulerPolicy::ROUND_ROBIN,
        SchedulerPolicy::PRIORITY, SchedulerPolicy::MLFQ, SchedulerPolicy::ADAPTIVE};
    for (const auto &wl : WorkloadLoader::profile_names())
    {
        for (uint32_t seed : {1u, 2u})
        {
            auto descs = WorkloadLoader::generate(WorkloadLoader::profile(wl), seed, 150);
            Tick total_burst = 0;
            for (auto &d : descs)
                total_burst += d.burst;

            for (auto pol : pols)
            {
                SimConfig cfg = quiet_cfg(wl, pol, seed);
                auto sched = make_scheduler(cfg);
                CPUEngine e(*sched, {});
                auto done = e.run(WorkloadLoader::instantiate(descs));
                const auto &c = e.counters();
                CHECK(done.size() == descs.size());
                CHECK(c.busy_ticks == total_burst);
                CHECK(c.makespan == c.busy_ticks + c.switch_ticks + c.idle_ticks);
                CHECK(c.switch_ticks == c.context_switches); // overhead = 1
                for (auto &p : done)
                {
                    CHECK(p->start_time >= p->arrival_time);
                    CHECK(p->turnaround_time >= p->burst_time);
                    CHECK(p->response_time <= p->waiting_time);
                    CHECK(p->remaining_time == 0);
                }
            }
        }
    }
}

TEST(srtf_is_optimal_for_mean_wait_without_overhead)
{
    const std::vector<SchedulerPolicy> others = {
        SchedulerPolicy::FCFS, SchedulerPolicy::SJF, SchedulerPolicy::ROUND_ROBIN,
        SchedulerPolicy::PRIORITY, SchedulerPolicy::MLFQ, SchedulerPolicy::ADAPTIVE};
    for (const auto &wl : {"mixed", "heavy_tail", "interactive"})
    {
        auto descs = WorkloadLoader::generate(WorkloadLoader::profile(wl), 3, 200);
        auto run = [&](SchedulerPolicy pol)
        {
            auto sched = make_scheduler(quiet_cfg(wl, pol, 3));
            CPUConfig cpu;
            cpu.context_switch_overhead = 0;
            CPUEngine e(*sched, cpu);
            auto done = e.run(WorkloadLoader::instantiate(descs));
            return Statistics::summarize(wl, sched->name(), done, e.counters()).avg_waiting;
        };
        double srtf = run(SchedulerPolicy::SRTF);
        for (auto pol : others)
            CHECK(srtf <= run(pol) + 1e-9);
    }
}

TEST(runs_are_deterministic)
{
    for (auto pol : {SchedulerPolicy::ADAPTIVE, SchedulerPolicy::MLFQ})
    {
        std::ostringstream a, b, sink;
        auto cfg = quiet_cfg("phased", pol, 11);
        Statistics::write_json(a, SimulationController(cfg).run(sink));
        Statistics::write_json(b, SimulationController(cfg).run(sink));
        CHECK(a.str() == b.str());
    }
}

TEST(adaptive_picks_fcfs_for_homogeneous_work)
{
    std::ostringstream sink;
    auto s = SimulationController(quiet_cfg("cpu_bound", SchedulerPolicy::ADAPTIVE, 5)).run(sink);
    CHECK(s.policy_share["FCFS"] > 0.8);
}

TEST(adaptive_picks_mlfq_for_heavy_tail)
{
    std::ostringstream sink;
    auto s = SimulationController(quiet_cfg("heavy_tail", SchedulerPolicy::ADAPTIVE, 5)).run(sink);
    CHECK(s.policy_share["MLFQ"] > 0.8);
}

TEST(adaptive_switches_across_phases)
{
    std::ostringstream sink;
    auto s = SimulationController(quiet_cfg("phased", SchedulerPolicy::ADAPTIVE, 5)).run(sink);
    CHECK(s.policy_switches >= 2);
    CHECK(s.policy_share.count("FCFS") == 1);
    CHECK(s.policy_share.count("MLFQ") == 1);
}

TEST(adaptive_scores_follow_theory)
{
    AdaptiveScheduler a;
    WorkloadMetrics homo;
    homo.completions_seen = 100;
    homo.mean_service = 30;
    homo.service_cv = 0.15;
    CHECK(a.score(homo).front().policy == SchedulerPolicy::FCFS);

    WorkloadMetrics hetero = homo;
    hetero.service_cv = 2.0;
    hetero.interactive_frac = 0.6;
    CHECK(a.score(hetero).front().policy == SchedulerPolicy::MLFQ);

    WorkloadMetrics starving = homo;
    starving.oldest_wait = 3.0 * 25.0 * homo.mean_service;
    CHECK(a.score(starving).front().policy == SchedulerPolicy::ROUND_ROBIN);
}

TEST(bandit_model_roundtrip)
{
    auto path = (std::filesystem::temp_directory_path() / "adaptive_sched_test_model.txt").string();
    std::remove(path.c_str());
    SimConfig cfg = quiet_cfg("mixed", SchedulerPolicy::BANDIT, 3);
    cfg.bandit_model = path;
    cfg.bandit_train = true;
    std::ostringstream sink;
    SimulationController(cfg).run(sink);

    BanditScheduler::Params p;
    BanditScheduler loaded(MetaScheduler::Config{}, p);
    CHECK(loaded.load_model(path));
    uint64_t visits = 0;
    for (int c = 0; c < BanditScheduler::NUM_CONTEXTS; ++c)
        for (int a = 0; a < BanditScheduler::NUM_ARMS; ++a)
            visits += loaded.visits(c, a);
    CHECK(visits > 50);

    // Greedy evaluation with the trained model completes every process
    cfg.bandit_train = false;
    auto s = SimulationController(cfg).run(sink);
    CHECK(s.processes == s.processes_total);
    std::remove(path.c_str());
}

TEST(csv_roundtrip)
{
    auto path = (std::filesystem::temp_directory_path() / "adaptive_sched_test.csv").string();
    auto descs = WorkloadLoader::generate(WorkloadLoader::profile("mixed"), 9, 40);
    WorkloadLoader::to_csv(descs, path);
    auto back = WorkloadLoader::from_csv(path);
    CHECK(back.size() == descs.size());
    for (std::size_t i = 0; i < back.size() && i < descs.size(); ++i)
    {
        CHECK(back[i].arrival == descs[i].arrival);
        CHECK(back[i].burst == descs[i].burst);
        CHECK(back[i].interactive == descs[i].interactive);
    }
    std::remove(path.c_str());
}

TEST(percentile_and_jain)
{
    CHECK_NEAR(Statistics::percentile({1, 2, 3, 4, 5}, 0.5), 3.0, 1e-12);
    CHECK_NEAR(Statistics::percentile({0, 10}, 0.95), 9.5, 1e-12);
    CHECK_NEAR(Statistics::jain_index({1, 1, 1, 1}), 1.0, 1e-12);
    CHECK_NEAR(Statistics::jain_index({1, 0, 0, 0}), 0.25, 1e-12);
}

int main()
{
    int failed_tests = 0;
    for (const auto &t : registry())
    {
        int before = g_failures;
        t.fn();
        bool ok = g_failures == before;
        failed_tests += ok ? 0 : 1;
        std::cout << (ok ? "[ PASS ] " : "[ FAIL ] ") << t.name << "\n";
    }
    std::cout << "\n" << registry().size() - static_cast<std::size_t>(failed_tests) << "/" << registry().size()
              << " tests passed\n";
    return failed_tests ? 1 : 0;
}
