#include "../../include/scheduler/bandit.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace adaptive_sched
{

    BanditScheduler::BanditScheduler(Config cfg, Params params)
        : MetaScheduler(cfg), params_(params), rng_(params.seed)
    {
    }

    int BanditScheduler::arm_index(SchedulerPolicy p)
    {
        for (int i = 0; i < NUM_ARMS; ++i)
            if (ARMS[static_cast<std::size_t>(i)] == p)
                return i;
        return -1;
    }

    int BanditScheduler::context_of(const WorkloadMetrics &m, uint64_t warmup)
    {
        if (m.completions_seen < warmup)
            return NUM_CONTEXTS - 1; // warm-up
        int cv = m.service_cv < 0.5 ? 0 : (m.service_cv < 1.2 ? 1 : 2);
        int qd = m.queue_depth < 2.0 ? 0 : (m.queue_depth < 8.0 ? 1 : 2);
        int pc = m.priority_corr < 0.5 ? 0 : 1;
        return pc * 9 + cv * 3 + qd;
    }

    std::string BanditScheduler::context_label(int ctx)
    {
        if (ctx == NUM_CONTEXTS - 1)
            return "warm-up";
        static const char *cv[] = {"cv<0.5", "cv 0.5-1.2", "cv>1.2"};
        static const char *qd[] = {"queue<2", "queue 2-8", "queue>8"};
        static const char *pc[] = {"prio uninformative", "prio informative"};
        return std::string(cv[(ctx % 9) / 3]) + ", " + qd[ctx % 3] + ", " + pc[ctx / 9];
    }

    std::optional<SchedulerPolicy> BanditScheduler::decide(const WorkloadMetrics &m)
    {
        // 1. Credit the window that just ended to (previous context, arm that ran)
        int cur_arm = arm_index(active_policy());
        if (params_.learn && prev_ctx_ >= 0 && cur_arm >= 0)
        {
            double reward = static_cast<double>(m.window_completions) - static_cast<double>(m.window_arrivals);
            auto &n = n_[prev_ctx_][cur_arm];
            auto &q = q_[prev_ctx_][cur_arm];
            ++n;
            q += (reward - q) / static_cast<double>(n);
        }

        // 2. Choose an arm for the next window
        int ctx = context_of(m, cfg_.warmup_completions);
        prev_ctx_ = ctx;

        std::ostringstream os;
        os << "ctx=[" << context_label(ctx) << "] ";

        if (params_.learn && std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < params_.epsilon)
        {
            int a = std::uniform_int_distribution<int>(0, NUM_ARMS - 1)(rng_);
            os << "explore " << to_string(ARMS[static_cast<std::size_t>(a)]);
            last_note_ = os.str();
            return ARMS[static_cast<std::size_t>(a)];
        }

        int best = -1;
        for (int a = 0; a < NUM_ARMS; ++a)
        {
            if (n_[ctx][a] < (params_.learn ? 1 : params_.min_visits))
                continue;
            if (best < 0 || q_[ctx][a] > q_[ctx][best])
                best = a;
        }
        os << std::fixed << std::setprecision(2);
        for (int a = 0; a < NUM_ARMS; ++a)
        {
            os << to_string(ARMS[static_cast<std::size_t>(a)]) << "=";
            if (n_[ctx][a])
                os << q_[ctx][a] << " ";
            else
                os << "? ";
        }
        last_note_ = os.str();
        if (best < 0)
        {
            last_note_ += "| unfamiliar context: keep current";
            return std::nullopt;
        }
        return ARMS[static_cast<std::size_t>(best)];
    }

    bool BanditScheduler::load_model(const std::string &path)
    {
        std::ifstream f(path);
        if (!f)
            return false;
        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream ss(line);
            int ctx, arm;
            uint64_t n;
            double q;
            if (ss >> ctx >> arm >> n >> q && ctx >= 0 && ctx < NUM_CONTEXTS && arm >= 0 && arm < NUM_ARMS)
            {
                n_[ctx][arm] = n;
                q_[ctx][arm] = q;
            }
        }
        return true;
    }

    void BanditScheduler::save_model(const std::string &path) const
    {
        std::ofstream f(path);
        f << "# AdaptiveSched++ bandit Q-table: context arm visits q\n";
        f << std::setprecision(10);
        for (int c = 0; c < NUM_CONTEXTS; ++c)
            for (int a = 0; a < NUM_ARMS; ++a)
                f << c << ' ' << a << ' ' << n_[c][a] << ' ' << q_[c][a] << '\n';
    }

    std::string BanditScheduler::q_table() const
    {
        std::ostringstream os;
        os << std::left << std::setw(46) << "context";
        for (int a = 0; a < NUM_ARMS; ++a)
            os << std::setw(18) << to_string(ARMS[static_cast<std::size_t>(a)]);
        os << "\n";
        for (int c = 0; c < NUM_CONTEXTS; ++c)
        {
            os << std::setw(46) << context_label(c);
            for (int a = 0; a < NUM_ARMS; ++a)
            {
                std::ostringstream cell;
                cell << std::fixed << std::setprecision(2) << q_[c][a] << " (n=" << n_[c][a] << ")";
                os << std::setw(18) << cell.str();
            }
            os << "\n";
        }
        return os.str();
    }

} // namespace adaptive_sched
