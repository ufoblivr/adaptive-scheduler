#include "../../include/scheduler/adaptive.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace adaptive_sched
{

    namespace
    {
        double ramp(double x, double lo, double hi)
        {
            if (hi <= lo)
                return x >= hi ? 1.0 : 0.0;
            return std::clamp((x - lo) / (hi - lo), 0.0, 1.0);
        }
        double ema(double prev, double sample, double alpha, bool init)
        {
            return init ? sample : (1.0 - alpha) * prev + alpha * sample;
        }
    } // namespace

    // =========================================================================
    // MetaScheduler
    // =========================================================================
    MetaScheduler::MetaScheduler(Config cfg)
        : cfg_(cfg),
          fcfs_(std::make_unique<FCFSScheduler>()),
          rr_(std::make_unique<RRScheduler>(cfg.rr_quantum)),
          priority_(std::make_unique<PriorityScheduler>(cfg.aging_interval)),
          mlfq_(std::make_unique<MLFQScheduler>())
    {
        active_ = scheduler_for(cfg_.initial);
        if (!active_)
            active_ = mlfq_.get();
    }

    IScheduler *MetaScheduler::scheduler_for(SchedulerPolicy p) const
    {
        switch (p)
        {
        case SchedulerPolicy::FCFS:
            return fcfs_.get();
        case SchedulerPolicy::ROUND_ROBIN:
            return rr_.get();
        case SchedulerPolicy::PRIORITY:
            return priority_.get();
        case SchedulerPolicy::MLFQ:
            return mlfq_.get();
        default:
            return nullptr;
        }
    }

    void MetaScheduler::add(ProcessPtr p, Tick now)
    {
        ++w_arrivals_;
        ++arrivals_seen_;
        double x = p->is_interactive ? 1.0 : 0.0;
        double a = std::max(cfg_.ema_alpha_job, 1.0 / static_cast<double>(arrivals_seen_));
        metrics_.interactive_frac += a * (x - metrics_.interactive_frac);
        active_->add(std::move(p), now);
    }

    void MetaScheduler::on_complete(const ProcessPtr &p, Tick now)
    {
        ++w_completions_;
        ++metrics_.completions_seen;
        // Observed service time. Bias-corrected EMA: an exact running mean for the
        // first 1/alpha jobs, then an exponential moving average, so early
        // estimates are not dominated by the very first job.
        double x = static_cast<double>(p->burst_time);
        double a = std::max(cfg_.ema_alpha_job, 1.0 / static_cast<double>(metrics_.completions_seen));
        double pr = static_cast<double>(p->base_priority);
        svc_m1_ += a * (x - svc_m1_);
        svc_m2_ += a * (x * x - svc_m2_);
        double ac = std::max(cfg_.ema_alpha_corr, 1.0 / static_cast<double>(metrics_.completions_seen));
        pri_m1_ += ac * (pr - pri_m1_);
        pri_m2_ += ac * (pr * pr - pri_m2_);
        pri_svc_ += ac * (pr * x - pri_svc_);
        cs_m1_ += ac * (x - cs_m1_);
        cs_m2_ += ac * (x * x - cs_m2_);
        active_->on_complete(p, now);
    }

    void MetaScheduler::on_tick(const TickInfo &info)
    {
        active_->on_tick(info);
        ++w_ticks_;
        if (info.kind == TickKind::SWITCH)
            ++w_switch_ticks_;
        w_queue_sum_ += static_cast<double>(active_->ready_count());
        if ((info.now + 1) % cfg_.eval_interval == 0)
            evaluate(info);
    }

    WorkloadType MetaScheduler::classify(const WorkloadMetrics &m)
    {
        if (m.completions_seen == 0 || m.mean_service <= 0)
            return WorkloadType::UNKNOWN;
        if (m.oldest_wait > std::max(100.0, 25.0 * m.mean_service))
            return WorkloadType::STARVING;
        if (m.interactive_frac > 0.5)
            return WorkloadType::INTERACTIVE;
        if (m.service_cv > 1.0)
            return WorkloadType::HETEROGENEOUS;
        return WorkloadType::HOMOGENEOUS;
    }

    void MetaScheduler::evaluate(const TickInfo &info)
    {
        const bool first_window = metrics_.at == 0;
        const double ticks = static_cast<double>(std::max<uint64_t>(w_ticks_, 1));

        WorkloadMetrics &m = metrics_;
        m.at = info.now + 1;
        m.arrival_rate = ema(m.arrival_rate, static_cast<double>(w_arrivals_) / ticks, cfg_.ema_alpha_window, first_window);
        m.mean_service = svc_m1_;
        m.service_cv = svc_m1_ > 0 ? std::sqrt(std::max(0.0, svc_m2_ - svc_m1_ * svc_m1_)) / svc_m1_ : 0.0;
        m.est_load = m.arrival_rate * m.mean_service;
        {
            double var_s = std::max(0.0, cs_m2_ - cs_m1_ * cs_m1_);
            double var_p = std::max(0.0, pri_m2_ - pri_m1_ * pri_m1_);
            double cov = pri_svc_ - pri_m1_ * cs_m1_;
            m.priority_corr = (var_s > 0 && var_p > 0) ? std::clamp(cov / std::sqrt(var_s * var_p), -1.0, 1.0) : 0.0;
        }
        m.queue_depth = w_queue_sum_ / ticks;
        m.switch_overhead = static_cast<double>(w_switch_ticks_) / ticks;
        m.window_arrivals = w_arrivals_;
        m.window_completions = w_completions_;

        double oldest = 0;
        active_->for_each_ready([&](const ProcessPtr &p)
                                { oldest = std::max(oldest, static_cast<double>(info.now - std::min(info.now, p->ready_since))); });
        m.oldest_wait = oldest;
        m.type = classify(m);

        SchedulerPolicy before = active_->policy();
        std::optional<SchedulerPolicy> want = decide(m);
        std::string note = describe_last_decision();

        bool allowed = m.completions_seen >= cfg_.warmup_completions && info.now >= cooldown_until_;
        if (want && *want != before && scheduler_for(*want))
        {
            if (allowed)
            {
                switch_to(*want, info);
                switches_.back().reason = note;
                note = "SWITCH " + to_string(before) + " -> " + to_string(*want) + " | " + note;
            }
            else
            {
                note = "held (warm-up/cooldown) | " + note;
            }
        }

        trace_.push_back({m.at, m, active_->policy(), note});
        if (trace_.size() > 200)
            trace_.pop_front();

        w_arrivals_ = w_completions_ = w_ticks_ = w_switch_ticks_ = 0;
        w_queue_sum_ = 0;
    }

    void MetaScheduler::switch_to(SchedulerPolicy target, const TickInfo &info)
    {
        IScheduler *next = scheduler_for(target);
        SchedulerPolicy from = active_->policy();
        next->import(active_->drain(), info.now);
        active_ = next;
        // The running process starts a fresh slice under the new policy
        if (info.running && !info.completed)
            info.running->slice_used = 0;
        cooldown_until_ = info.now + cfg_.cooldown;
        switches_.push_back({info.now + 1, from, target, metrics_.type, ""});
    }

    std::string MetaScheduler::decision_trace(std::size_t last_n) const
    {
        std::ostringstream os;
        os << "=== Decision trace (last " << std::min(last_n, trace_.size()) << " windows) ===\n";
        std::size_t start = trace_.size() > last_n ? trace_.size() - last_n : 0;
        for (std::size_t i = start; i < trace_.size(); ++i)
        {
            const auto &e = trace_[i];
            os << "  t=" << std::setw(6) << e.at << "  " << std::setw(13) << to_string(e.m.type)
               << "  active=" << std::setw(4) << to_string(e.active) << std::fixed << std::setprecision(2)
               << "  cv=" << e.m.service_cv << " inter=" << e.m.interactive_frac
               << " pcorr=" << e.m.priority_corr
               << " q=" << e.m.queue_depth << "  " << e.note << "\n";
        }
        return os.str();
    }

    // =========================================================================
    // AdaptiveScheduler — rule-based scoring
    // =========================================================================
    std::vector<PolicyScore> AdaptiveScheduler::score(const WorkloadMetrics &m) const
    {
        const double h = ramp(m.service_cv, rules_.cv_low, rules_.cv_high);
        const double i = ramp(m.interactive_frac, rules_.inter_low, rules_.inter_high);
        const double c = ramp(m.priority_corr, rules_.corr_low, rules_.corr_high);
        const double s = m.mean_service > 0
                             ? ramp(m.oldest_wait / (rules_.starve_factor * m.mean_service), 1.0, 2.0)
                             : 0.0;

        std::vector<PolicyScore> out = {
            {SchedulerPolicy::FCFS, 1.00 - 0.90 * h - 0.50 * i - 0.50 * s},
            {SchedulerPolicy::MLFQ, 0.35 + 0.60 * h + 0.30 * i - 0.20 * s - 0.25 * c},
            {SchedulerPolicy::PRIORITY, 0.25 + 0.90 * c * std::max(h, i) - 0.30 * s},
            {SchedulerPolicy::ROUND_ROBIN, 0.25 + 0.20 * h + 0.45 * i + 0.60 * s - 0.25 * (1.0 - h)},
        };
        for (auto &ps : out)
        {
            if (ps.policy == active_policy())
                ps.score += rules_.stability_bonus;
            ps.score = std::max(ps.score, 0.0);
        }
        std::stable_sort(out.begin(), out.end(), [](const PolicyScore &a, const PolicyScore &b)
                         { return a.score > b.score; });
        return out;
    }

    std::optional<SchedulerPolicy> AdaptiveScheduler::decide(const WorkloadMetrics &m)
    {
        auto scores = score(m);
        double current = 0;
        for (const auto &ps : scores)
            if (ps.policy == active_policy())
                current = ps.score;

        std::ostringstream os;
        os << std::fixed << std::setprecision(2);
        for (const auto &ps : scores)
            os << to_string(ps.policy) << "=" << ps.score << " ";
        last_note_ = os.str();

        const auto &best = scores.front();
        if (best.policy != active_policy() && best.score - current >= rules_.hysteresis)
            return best.policy;
        return std::nullopt;
    }

} // namespace adaptive_sched
