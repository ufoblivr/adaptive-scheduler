#include "../../include/metrics/statistics.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <stdexcept>

namespace adaptive_sched
{

    double Statistics::percentile(std::vector<double> v, double q)
    {
        if (v.empty())
            return 0.0;
        std::sort(v.begin(), v.end());
        double pos = q * static_cast<double>(v.size() - 1);
        std::size_t lo = static_cast<std::size_t>(std::floor(pos));
        std::size_t hi = std::min(lo + 1, v.size() - 1);
        double frac = pos - static_cast<double>(lo);
        return v[lo] + (v[hi] - v[lo]) * frac;
    }

    double Statistics::jain_index(const std::vector<double> &x)
    {
        if (x.empty())
            return 1.0;
        double s = 0, s2 = 0;
        for (double v : x)
        {
            s += v;
            s2 += v * v;
        }
        return s2 > 0 ? (s * s) / (static_cast<double>(x.size()) * s2) : 1.0;
    }

    Summary Statistics::summarize(const std::string &scenario, const std::string &policy,
                                  const std::vector<ProcessPtr> &completed, const RunCounters &c,
                                  uint64_t policy_switches)
    {
        Summary s;
        s.scenario = scenario;
        s.policy = policy;
        s.processes = completed.size();
        s.makespan = c.makespan;
        s.busy_ticks = c.busy_ticks;
        s.switch_ticks = c.switch_ticks;
        s.idle_ticks = c.idle_ticks;
        s.context_switches = c.context_switches;
        s.preemptions = c.preemptions;
        s.policy_switches = policy_switches;

        if (c.makespan > 0)
        {
            double m = static_cast<double>(c.makespan);
            s.cpu_utilization = static_cast<double>(c.busy_ticks) / m;
            s.switch_overhead = static_cast<double>(c.switch_ticks) / m;
            s.throughput = 1000.0 * static_cast<double>(completed.size()) / m;
            for (std::size_t i = 0; i < c.ticks_by_policy.size(); ++i)
                if (c.ticks_by_policy[i])
                    s.policy_share[to_string(static_cast<SchedulerPolicy>(i))] =
                        static_cast<double>(c.ticks_by_policy[i]) / m;
        }
        if (completed.empty())
            return s;

        std::vector<double> wt, tat, rt, sd, share, rt_inter;
        for (const auto &p : completed)
        {
            wt.push_back(static_cast<double>(p->waiting_time));
            tat.push_back(static_cast<double>(p->turnaround_time));
            rt.push_back(static_cast<double>(p->response_time));
            sd.push_back(p->slowdown());
            share.push_back(1.0 / p->slowdown());
            if (p->is_interactive)
                rt_inter.push_back(static_cast<double>(p->response_time));
        }
        auto mean = [](const std::vector<double> &v)
        { return v.empty() ? 0.0 : std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size()); };

        s.avg_waiting = mean(wt);
        s.p50_waiting = percentile(wt, 0.50);
        s.p95_waiting = percentile(wt, 0.95);
        s.p99_waiting = percentile(wt, 0.99);
        s.max_waiting = *std::max_element(wt.begin(), wt.end());
        s.avg_turnaround = mean(tat);
        s.avg_response = mean(rt);
        s.p95_response = percentile(rt, 0.95);
        s.avg_response_interactive = mean(rt_inter);
        s.avg_slowdown = mean(sd);
        s.p95_slowdown = percentile(sd, 0.95);
        s.jain_fairness = jain_index(share);
        return s;
    }

    void Statistics::print_summary(std::ostream &os, const Summary &s)
    {
        auto line = [&](const std::string &k, double v, const std::string &unit = "")
        {
            os << "  " << std::left << std::setw(26) << k << ": " << std::right << std::fixed
               << std::setprecision(2) << std::setw(12) << v << (unit.empty() ? "" : " " + unit) << "\n";
        };
        os << std::string(64, '=') << "\n"
           << "  " << s.scenario << "  |  policy: " << s.policy << "\n"
           << std::string(64, '=') << "\n";
        line("Processes completed", static_cast<double>(s.processes));
        if (s.processes_total != s.processes)
            line("  (of total in workload)", static_cast<double>(s.processes_total));
        line("Makespan", static_cast<double>(s.makespan), "ticks");
        line("CPU utilization", 100.0 * s.cpu_utilization, "%");
        line("Context-switch overhead", 100.0 * s.switch_overhead, "% of time");
        line("Throughput", s.throughput, "proc / 1000 ticks");
        line("Avg waiting time", s.avg_waiting, "ticks");
        line("p50 waiting", s.p50_waiting, "ticks");
        line("p95 waiting", s.p95_waiting, "ticks");
        line("p99 waiting", s.p99_waiting, "ticks");
        line("Avg turnaround", s.avg_turnaround, "ticks");
        line("Avg response time", s.avg_response, "ticks");
        line("Avg response (interactive)", s.avg_response_interactive, "ticks");
        line("Avg slowdown", s.avg_slowdown, "x");
        line("p95 slowdown", s.p95_slowdown, "x");
        line("Jain fairness", s.jain_fairness);
        line("Context switches", static_cast<double>(s.context_switches));
        line("Preemptions", static_cast<double>(s.preemptions));
        if (s.policy_switches || s.policy_share.size() > 1)
        {
            line("Policy switches", static_cast<double>(s.policy_switches));
            for (const auto &[k, v] : s.policy_share)
                line("  time under " + k, 100.0 * v, "%");
        }
        os << std::string(64, '=') << "\n";
    }

    void Statistics::write_json(std::ostream &os, const Summary &s)
    {
        os << std::setprecision(6) << std::fixed;
        os << "{\"scenario\":\"" << s.scenario << "\",\"policy\":\"" << s.policy << "\""
           << ",\"processes\":" << s.processes << ",\"processes_total\":" << s.processes_total
           << ",\"makespan\":" << s.makespan << ",\"busy_ticks\":" << s.busy_ticks
           << ",\"switch_ticks\":" << s.switch_ticks << ",\"idle_ticks\":" << s.idle_ticks
           << ",\"cpu_utilization\":" << s.cpu_utilization << ",\"switch_overhead\":" << s.switch_overhead
           << ",\"throughput\":" << s.throughput
           << ",\"avg_waiting\":" << s.avg_waiting << ",\"p50_waiting\":" << s.p50_waiting
           << ",\"p95_waiting\":" << s.p95_waiting << ",\"p99_waiting\":" << s.p99_waiting
           << ",\"max_waiting\":" << s.max_waiting
           << ",\"avg_turnaround\":" << s.avg_turnaround << ",\"avg_response\":" << s.avg_response
           << ",\"p95_response\":" << s.p95_response
           << ",\"avg_response_interactive\":" << s.avg_response_interactive
           << ",\"avg_slowdown\":" << s.avg_slowdown << ",\"p95_slowdown\":" << s.p95_slowdown
           << ",\"jain_fairness\":" << s.jain_fairness
           << ",\"context_switches\":" << s.context_switches << ",\"preemptions\":" << s.preemptions
           << ",\"policy_switches\":" << s.policy_switches << ",\"policy_share\":{";
        bool first = true;
        for (const auto &[k, v] : s.policy_share)
        {
            os << (first ? "" : ",") << "\"" << k << "\":" << v;
            first = false;
        }
        os << "}}\n";
    }

    void Statistics::export_process_csv(const std::string &path, const std::vector<ProcessPtr> &completed)
    {
        std::ofstream f(path);
        if (!f)
            throw std::runtime_error("Cannot write " + path);
        f << "pid,name,arrival,burst,priority,interactive,start,completion,waiting,turnaround,"
             "response,slowdown,dispatches,preemptions,completed_under\n";
        auto sorted = completed;
        std::sort(sorted.begin(), sorted.end(), [](const ProcessPtr &a, const ProcessPtr &b)
                  { return a->pid < b->pid; });
        for (const auto &p : sorted)
        {
            f << p->pid << ',' << p->name << ',' << p->arrival_time << ',' << p->burst_time << ','
              << p->base_priority << ',' << (p->is_interactive ? 1 : 0) << ',' << p->start_time << ','
              << p->completion_time << ',' << p->waiting_time << ',' << p->turnaround_time << ','
              << p->response_time << ',' << std::fixed << std::setprecision(4) << p->slowdown() << ','
              << p->dispatches << ',' << p->preemptions << ','
              << (p->completed_under >= 0 ? to_string(static_cast<SchedulerPolicy>(p->completed_under)) : "")
              << '\n';
        }
    }

    void Statistics::export_timeline_csv(const std::string &path, const std::vector<TimelineSegment> &tl)
    {
        std::ofstream f(path);
        if (!f)
            throw std::runtime_error("Cannot write " + path);
        f << "start,end,kind,pid,policy\n";
        for (const auto &seg : tl)
        {
            const char *kind = seg.kind == TickKind::EXECUTE ? "run" : seg.kind == TickKind::SWITCH ? "switch"
                                                                                                    : "idle";
            f << seg.start << ',' << seg.end << ',' << kind << ',' << seg.pid << ',' << to_string(seg.policy) << '\n';
        }
    }

} // namespace adaptive_sched
