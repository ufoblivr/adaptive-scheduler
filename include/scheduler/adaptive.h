#pragma once

#include "fcfs.h"
#include "mlfq.h"
#include "priority.h"
#include "rr.h"

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace adaptive_sched
{

    // ---------------------------------------------------------------------------
    // WorkloadMetrics — what a meta-scheduler can observe about the workload.
    //
    // Only signals a real kernel could measure are used: it sees arrivals,
    // completions (and therefore the *observed* service time of finished jobs),
    // queue lengths and waiting ages. It never peeks at a job's remaining burst,
    // which is why SJF/SRTF are not among its options.
    // ---------------------------------------------------------------------------
    struct WorkloadMetrics
    {
        Tick at = 0;
        uint64_t completions_seen = 0;

        double mean_service = 0;   // EMA of observed service time of completed jobs
        double service_cv = 0;     // coefficient of variation of service time (EMA-based)
        double interactive_frac = 0; // EMA over arrivals of the interactive hint
        double priority_corr = 0;  // corr(priority number, observed service time) over recent
                                   // completions; > 0 means urgent jobs tend to be short, so
                                   // priority order approximates shortest-job-first
        double arrival_rate = 0;   // EMA of arrivals per tick
        double est_load = 0;       // arrival_rate × mean_service
        double queue_depth = 0;    // mean ready-queue length over the last window
        double oldest_wait = 0;    // longest current wait in the ready queue (ticks)
        double switch_overhead = 0; // fraction of last window lost to context switches

        // Last window's raw counts (used as the bandit's reward)
        uint64_t window_arrivals = 0;
        uint64_t window_completions = 0;

        WorkloadType type = WorkloadType::UNKNOWN;
    };

    struct PolicyScore
    {
        SchedulerPolicy policy;
        double score; // >= 0, higher is better (not a probability)
    };

    struct SwitchRecord
    {
        Tick at;
        SchedulerPolicy from;
        SchedulerPolicy to;
        WorkloadType type;
        std::string reason;
    };

    // ---------------------------------------------------------------------------
    // MetaScheduler — owns FCFS, RR, Priority and MLFQ instances and delegates every
    // decision to the active one. Every `eval_interval` ticks it summarises the
    // last window into WorkloadMetrics and asks the subclass (decide()) whether
    // to switch. Switching migrates the ready queue to the new policy without
    // losing anyone's remaining work or waiting age.
    // ---------------------------------------------------------------------------
    class MetaScheduler : public IScheduler
    {
    public:
        struct Config
        {
            Tick eval_interval = 200;  // ticks per evaluation window
            Tick cooldown = 800;       // minimum ticks between switches
            uint64_t warmup_completions = 10; // no switching before this many completions
            double ema_alpha_window = 0.3;    // smoothing for per-window signals
            double ema_alpha_job = 0.05;      // smoothing for per-job signals (~last 20 jobs)
            double ema_alpha_corr = 0.02;     // priority/service correlation (~last 50 jobs): a
                                              // structural property, so smoothed harder
            Tick rr_quantum = 20;
            Tick aging_interval = 100;
            SchedulerPolicy initial = SchedulerPolicy::MLFQ;
        };

        explicit MetaScheduler(Config cfg);

        // IScheduler — delegation plus telemetry
        void add(ProcessPtr p, Tick now) override;
        ProcessPtr pick_next(Tick now) override { return active_->pick_next(now); }
        bool should_preempt(const Process &r, Tick now) const override { return active_->should_preempt(r, now); }
        void requeue(ProcessPtr p, Tick now) override { active_->requeue(std::move(p), now); }
        void on_complete(const ProcessPtr &p, Tick now) override;
        void on_tick(const TickInfo &info) override;
        std::size_t ready_count() const override { return active_->ready_count(); }
        std::vector<ProcessPtr> drain() override { return active_->drain(); }
        void import(std::vector<ProcessPtr> procs, Tick now) override { active_->import(std::move(procs), now); }
        void for_each_ready(const std::function<void(const ProcessPtr &)> &f) const override { active_->for_each_ready(f); }
        SchedulerPolicy active_policy() const override { return active_->policy(); }

        const std::vector<SwitchRecord> &switch_history() const { return switches_; }
        const WorkloadMetrics &metrics() const { return metrics_; }
        std::string decision_trace(std::size_t last_n = 20) const;

        static constexpr std::array<SchedulerPolicy, 4> ARMS = {
            SchedulerPolicy::FCFS, SchedulerPolicy::ROUND_ROBIN, SchedulerPolicy::PRIORITY, SchedulerPolicy::MLFQ};

    protected:
        // Return a policy to switch to, or nullopt to stay. Called once per window.
        virtual std::optional<SchedulerPolicy> decide(const WorkloadMetrics &m) = 0;
        virtual std::string describe_last_decision() const { return ""; }

        IScheduler *scheduler_for(SchedulerPolicy p) const;
        Config cfg_;

    private:
        void evaluate(const TickInfo &info);
        void switch_to(SchedulerPolicy target, const TickInfo &info);
        static WorkloadType classify(const WorkloadMetrics &m);

        std::unique_ptr<FCFSScheduler> fcfs_;
        std::unique_ptr<RRScheduler> rr_;
        std::unique_ptr<PriorityScheduler> priority_;
        std::unique_ptr<MLFQScheduler> mlfq_;
        IScheduler *active_ = nullptr;

        WorkloadMetrics metrics_;
        Tick cooldown_until_ = 0;
        uint64_t arrivals_seen_ = 0;
        double svc_m1_ = 0, svc_m2_ = 0; // EMA of x and x^2 of service times
        double pri_m1_ = 0, pri_m2_ = 0, pri_svc_ = 0; // EMA of priority, priority^2, priority*service
        double cs_m1_ = 0, cs_m2_ = 0;                 // slower EMA of service for the correlation

        // per-window accumulators
        uint64_t w_arrivals_ = 0, w_completions_ = 0, w_ticks_ = 0, w_switch_ticks_ = 0;
        double w_queue_sum_ = 0;

        std::vector<SwitchRecord> switches_;
        struct TraceEntry
        {
            Tick at;
            WorkloadMetrics m;
            SchedulerPolicy active;
            std::string note;
        };
        std::deque<TraceEntry> trace_;
    };

    // ---------------------------------------------------------------------------
    // AdaptiveScheduler — rule-based meta-scheduler.
    //
    // Scheduling theory gives clear guidance on mean waiting time:
    //   • service times nearly equal (low CV) → FCFS is hard to beat; time-slicing
    //     only adds switch overhead and delays every completion;
    //   • high service-time variance → favour short jobs; MLFQ approximates this
    //     without knowing burst lengths;
    //   • many interactive arrivals → response time matters, favour MLFQ / RR;
    //   • priorities that predict job length (urgent ⇒ short) → priority
    //     scheduling behaves like shortest-job-first using only the hint;
    //   • a ready process waiting far longer than typical → RR bounds the wait.
    // Each policy gets a continuous score in [0,1] from these signals; a switch
    // needs the winner to beat the incumbent by `hysteresis`, and is followed by
    // a cooldown, so the scheduler does not thrash.
    // ---------------------------------------------------------------------------
    class AdaptiveScheduler : public MetaScheduler
    {
    public:
        struct Rules
        {
            double cv_low = 0.6, cv_high = 1.4;      // heterogeneity ramp
            double inter_low = 0.2, inter_high = 0.6; // interactivity ramp
            double corr_low = 0.4, corr_high = 0.8;   // priority-informativeness ramp
            double starve_factor = 25.0;             // oldest_wait > factor × mean_service → starving
            double hysteresis = 0.15;
            double stability_bonus = 0.05;
        };

        AdaptiveScheduler() : AdaptiveScheduler(Config{}, Rules{}) {}
        AdaptiveScheduler(Config cfg, Rules rules) : MetaScheduler(cfg), rules_(rules) {}

        std::string name() const override { return "ADAPTIVE"; }
        SchedulerPolicy policy() const override { return SchedulerPolicy::ADAPTIVE; }

        std::vector<PolicyScore> score(const WorkloadMetrics &m) const;

    protected:
        std::optional<SchedulerPolicy> decide(const WorkloadMetrics &m) override;
        std::string describe_last_decision() const override { return last_note_; }

    private:
        Rules rules_;
        std::string last_note_;
    };

} // namespace adaptive_sched
