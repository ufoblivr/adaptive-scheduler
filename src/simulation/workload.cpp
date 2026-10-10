#include "../../include/simulation/workload.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace adaptive_sched
{

    namespace
    {
        Phase homogeneous(const std::string &label, int count, double load, double mean, double sd,
                          Tick lo, Tick hi)
        {
            Phase p;
            p.label = label;
            p.count = count;
            p.load = load;
            p.dist = BurstDist::NORMAL;
            p.a = mean;
            p.b = sd;
            p.burst_min = lo;
            p.burst_max = hi;
            return p;
        }

        Phase bimodal(const std::string &label, int count, double load, double p_short,
                      double s_lo, double s_hi, double l_lo, double l_hi, Tick interactive_max)
        {
            Phase p;
            p.label = label;
            p.count = count;
            p.load = load;
            p.dist = BurstDist::BIMODAL;
            p.p_short = p_short;
            p.a = s_lo;
            p.b = s_hi;
            p.c = l_lo;
            p.d = l_hi;
            p.burst_min = 1;
            p.burst_max = static_cast<Tick>(l_hi);
            p.interactive_max_burst = interactive_max;
            return p;
        }

        double mean_burst(const Phase &p)
        {
            switch (p.dist)
            {
            case BurstDist::NORMAL:
            case BurstDist::LOGNORMAL:
                return p.a;
            case BurstDist::UNIFORM:
                return (p.a + p.b) / 2.0;
            case BurstDist::BIMODAL:
                return p.p_short * (p.a + p.b) / 2.0 + (1.0 - p.p_short) * (p.c + p.d) / 2.0;
            }
            return p.a;
        }

        Tick draw_burst(const Phase &p, std::mt19937 &rng)
        {
            double x = 1.0;
            switch (p.dist)
            {
            case BurstDist::NORMAL:
                x = std::normal_distribution<double>(p.a, p.b)(rng);
                break;
            case BurstDist::UNIFORM:
                x = static_cast<double>(std::uniform_int_distribution<long>(
                    static_cast<long>(p.a), static_cast<long>(p.b))(rng));
                break;
            case BurstDist::LOGNORMAL:
            {
                double sigma2 = std::log(1.0 + p.b * p.b);
                double mu = std::log(p.a) - sigma2 / 2.0;
                x = std::lognormal_distribution<double>(mu, std::sqrt(sigma2))(rng);
                break;
            }
            case BurstDist::BIMODAL:
            {
                bool is_short = std::bernoulli_distribution(p.p_short)(rng);
                double lo = is_short ? p.a : p.c, hi = is_short ? p.b : p.d;
                x = static_cast<double>(std::uniform_int_distribution<long>(
                    static_cast<long>(lo), static_cast<long>(hi))(rng));
                break;
            }
            }
            long v = std::lround(x);
            v = std::max<long>(v, static_cast<long>(p.burst_min));
            v = std::min<long>(v, static_cast<long>(p.burst_max));
            return static_cast<Tick>(std::max<long>(v, 1));
        }
    } // namespace

    std::vector<std::string> WorkloadLoader::profile_names()
    {
        return {"interactive", "cpu_bound", "batch", "mixed", "heavy_tail", "stress", "phased"};
    }

    WorkloadProfile WorkloadLoader::profile(const std::string &name)
    {
        WorkloadProfile w;
        w.name = name;
        if (name == "interactive")
        {
            w.description = "85% short interactive bursts (10-40), 15% medium (100-300); load 0.8";
            w.phases = {bimodal("interactive", 500, 0.8, 0.85, 10, 40, 100, 300, 40)};
        }
        else if (name == "cpu_bound")
        {
            w.description = "Homogeneous CPU-bound bursts ~N(300,40); load 0.9";
            w.phases = {homogeneous("cpu_bound", 400, 0.9, 300, 40, 150, 500)};
        }
        else if (name == "batch")
        {
            w.description = "Long homogeneous batch jobs ~N(800,60); load 0.95";
            w.phases = {homogeneous("batch", 200, 0.95, 800, 60, 500, 1200)};
        }
        else if (name == "mixed")
        {
            w.description = "70% short interactive (10-60) / 30% long (400-1000); load 0.9";
            w.phases = {bimodal("mixed", 500, 0.9, 0.7, 10, 60, 400, 1000, 60)};
        }
        else if (name == "heavy_tail")
        {
            Phase p;
            p.label = "heavy_tail";
            p.count = 500;
            p.load = 0.9;
            p.dist = BurstDist::LOGNORMAL;
            p.a = 120;
            p.b = 2.5; // coefficient of variation
            p.burst_min = 1;
            p.burst_max = 6000;
            p.interactive_max_burst = 40;
            w.description = "Lognormal bursts, mean 120, CV 2.5; load 0.9";
            w.phases = {p};
        }
        else if (name == "stress")
        {
            w.description = "Mixed workload at load 1.2 (overloaded: the queue grows without bound)";
            w.phases = {bimodal("stress", 500, 1.2, 0.7, 10, 60, 400, 1000, 60)};
        }
        else if (name == "phased")
        {
            w.description = "Four phases: cpu_bound -> mixed -> batch -> interactive";
            w.phases = {
                homogeneous("p1_cpu_bound", 150, 0.9, 300, 40, 150, 500),
                bimodal("p2_mixed", 250, 0.9, 0.7, 10, 60, 400, 1000, 60),
                homogeneous("p3_batch", 60, 0.95, 800, 60, 500, 1200),
                bimodal("p4_interactive", 300, 0.8, 0.85, 10, 40, 100, 300, 40),
            };
        }
        else
        {
            throw std::invalid_argument("Unknown workload profile: " + name);
        }
        return w;
    }

    std::vector<ProcessDescriptor> WorkloadLoader::generate(const WorkloadProfile &profile, uint32_t seed,
                                                            int count_override, double load_override)
    {
        std::mt19937 rng(seed);
        int base_total = 0;
        for (const auto &ph : profile.phases)
            base_total += ph.count;

        std::vector<ProcessDescriptor> out;
        double clock = 0.0;
        PID pid = 0;
        for (const auto &ph : profile.phases)
        {
            int count = ph.count;
            if (count_override > 0 && base_total > 0)
                count = std::max(1, static_cast<int>(std::lround(
                                        static_cast<double>(ph.count) * count_override / base_total)));
            double load = load_override > 0 ? load_override : ph.load;
            double rate = load / mean_burst(ph); // arrivals per tick
            std::exponential_distribution<double> inter(rate);
            std::uniform_int_distribution<int> hi_pri(0, 3), lo_pri(4, 9);

            for (int i = 0; i < count; ++i)
            {
                clock += inter(rng);
                ProcessDescriptor d;
                d.pid = ++pid;
                d.arrival = static_cast<Tick>(clock);
                d.burst = draw_burst(ph, rng);
                d.interactive = ph.interactive_max_burst > 0 && d.burst <= ph.interactive_max_burst;
                d.priority = d.interactive ? hi_pri(rng) : lo_pri(rng);
                d.name = ph.label + "_" + std::to_string(d.pid);
                out.push_back(d);
            }
        }
        return out;
    }

    std::vector<ProcessDescriptor> WorkloadLoader::from_csv(const std::string &path)
    {
        std::ifstream f(path);
        if (!f)
            throw std::runtime_error("Cannot open workload file: " + path);

        std::vector<ProcessDescriptor> descs;
        std::string line;
        std::size_t lineno = 0;
        while (std::getline(f, line))
        {
            ++lineno;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            if (!std::isdigit(static_cast<unsigned char>(line[0])))
                continue; // header

            std::istringstream ss(line);
            std::string tok;
            std::vector<std::string> cols;
            while (std::getline(ss, tok, ','))
                cols.push_back(tok);
            if (cols.size() < 5)
                throw std::runtime_error(path + ":" + std::to_string(lineno) + ": expected >= 5 columns");

            ProcessDescriptor d;
            d.pid = static_cast<PID>(std::stoul(cols[0]));
            d.name = cols[1];
            d.arrival = std::stoull(cols[2]);
            d.burst = std::stoull(cols[3]);
            d.priority = std::stoi(cols[4]);
            d.interactive = cols.size() > 5 && (cols[5] == "1" || cols[5] == "true");
            if (d.burst == 0)
                throw std::runtime_error(path + ":" + std::to_string(lineno) + ": burst must be >= 1");
            descs.push_back(d);
        }
        return descs;
    }

    void WorkloadLoader::to_csv(const std::vector<ProcessDescriptor> &descs, const std::string &path)
    {
        std::ofstream f(path);
        if (!f)
            throw std::runtime_error("Cannot write workload file: " + path);
        f << "pid,name,arrival,burst,priority,interactive\n";
        for (const auto &d : descs)
            f << d.pid << ',' << d.name << ',' << d.arrival << ',' << d.burst << ','
              << d.priority << ',' << (d.interactive ? 1 : 0) << '\n';
    }

    std::vector<ProcessPtr> WorkloadLoader::instantiate(const std::vector<ProcessDescriptor> &descs)
    {
        std::vector<ProcessPtr> out;
        out.reserve(descs.size());
        for (const auto &d : descs)
            out.push_back(std::make_shared<Process>(d.pid, d.name, d.arrival, d.burst, d.priority, d.interactive));
        return out;
    }

} // namespace adaptive_sched
