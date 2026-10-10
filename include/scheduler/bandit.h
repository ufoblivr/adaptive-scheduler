#pragma once

#include "adaptive.h"

#include <array>
#include <random>
#include <string>

namespace adaptive_sched
{

    // ---------------------------------------------------------------------------
    // BanditScheduler — a learned meta-scheduler (contextual epsilon-greedy bandit).
    //
    // Instead of hand-written rules, it learns which policy works best in which
    // situation from experience.
    //
    //   Context  : the observed workload state at the start of a window,
    //              discretised into 3 service-time-CV buckets × 3 queue-depth
    //              buckets × 2 priority-informativeness buckets (+1 warm-up
    //              context) = 19 contexts.
    //   Arms     : FCFS, RR, PRIORITY, MLFQ (same options as the rule-based scheduler).
    //   Reward   : completions − arrivals during the window (= −Δ number of jobs in
    //              the system). By Little's law, mean turnaround is proportional
    //              to the time-average number of jobs in the system, so an arm that
    //              drains the system faster is directly reducing the objective.
    //              Arrivals do not depend on the arm; subtracting them only removes
    //              variance.
    //   Update   : incremental sample mean Q(ctx, arm).
    //   Acting   : greedy over arms with at least `min_visits` observations in
    //              this context; if none qualify (a situation unlike training) it
    //              keeps the current policy instead of trusting a noisy estimate.
    //
    // Training mode explores with probability ε and writes the Q-table to a model
    // file; evaluation mode loads the table and acts greedily. The benchmark trains
    // on one set of seeds and workloads and reports results on held-out seeds,
    // including workloads the bandit never saw during training.
    // ---------------------------------------------------------------------------
    class BanditScheduler : public MetaScheduler
    {
    public:
        static constexpr int NUM_CONTEXTS = 19;
        static constexpr int NUM_ARMS = 4;

        struct Params
        {
            bool learn = false;   // update Q and explore
            double epsilon = 0.2; // exploration rate while learning
            uint32_t seed = 7;    // exploration RNG seed
            uint64_t min_visits = 50; // act on a Q-value only if it rests on this many windows;
                                      // in unfamiliar contexts keep the current policy
        };

        BanditScheduler(Config cfg, Params params);

        std::string name() const override { return "BANDIT"; }
        SchedulerPolicy policy() const override { return SchedulerPolicy::BANDIT; }

        bool load_model(const std::string &path);
        void save_model(const std::string &path) const;

        static int context_of(const WorkloadMetrics &m, uint64_t warmup);
        static std::string context_label(int ctx);

        double q(int ctx, int arm) const { return q_[ctx][arm]; }
        uint64_t visits(int ctx, int arm) const { return n_[ctx][arm]; }
        std::string q_table() const;

    protected:
        std::optional<SchedulerPolicy> decide(const WorkloadMetrics &m) override;
        std::string describe_last_decision() const override { return last_note_; }

    private:
        static int arm_index(SchedulerPolicy p);

        Params params_;
        std::mt19937 rng_;
        std::array<std::array<double, NUM_ARMS>, NUM_CONTEXTS> q_{};
        std::array<std::array<uint64_t, NUM_ARMS>, NUM_CONTEXTS> n_{};
        int prev_ctx_ = -1;
        std::string last_note_;
    };

} // namespace adaptive_sched
