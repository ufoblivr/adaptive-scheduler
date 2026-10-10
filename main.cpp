#include "include/simulation/simulation_controller.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

using namespace adaptive_sched;

static void print_usage(const char *prog)
{
    std::cout << "Usage: " << prog << " [OPTIONS]\n\n"
              << "Workload:\n"
              << "  --workload <name>      interactive | cpu_bound | batch | mixed | heavy_tail | stress | phased\n"
              << "                         (default: mixed)\n"
              << "  --csv <path>           load processes from CSV (pid,name,arrival,burst,priority,interactive)\n"
              << "  --count <n>            number of processes (scales every phase)\n"
              << "  --load <rho>           override target CPU load (e.g. 0.9)\n"
              << "  --seed <n>             RNG seed for the workload generator (default: 42)\n"
              << "  --dump-workload <path> write the generated workload to CSV and exit\n\n"
              << "Scheduling:\n"
              << "  --policy <name>        fcfs | sjf | srtf | rr | priority | mlfq | adaptive | bandit\n"
              << "                         (default: adaptive)\n"
              << "  --quantum <n>          RR quantum in ticks (default: 20)\n"
              << "  --aging <n>            priority aging interval in ticks, 0 = off (default: 100)\n"
              << "  --ctx-overhead <n>     ticks lost per context switch (default: 1)\n"
              << "  --max-ticks <n>        stop after n ticks (default: run to completion)\n"
              << "  --no-adaptive          alias for --policy mlfq\n\n"
              << "Learned scheduler:\n"
              << "  --bandit-model <path>  Q-table to load (required for --policy bandit)\n"
              << "  --bandit-train         explore + update the Q-table and save it back\n\n"
              << "Output:\n"
              << "  --json                 print a one-line JSON summary (machine-readable, implies --quiet)\n"
              << "  --trace                print the meta-scheduler's per-window decision trace\n"
              << "  --export-csv           write per-process metrics to <output-dir>/metrics.csv\n"
              << "  --export-timeline      write a Gantt timeline to <output-dir>/timeline.csv\n"
              << "  --output-dir <path>    default: ./output\n"
              << "  --quiet                no human-readable output\n"
              << "  --help\n\n"
              << "Examples:\n"
              << "  " << prog << " --workload phased --policy adaptive --trace\n"
              << "  " << prog << " --workload heavy_tail --policy srtf --seed 7 --json\n"
              << "  " << prog << " --csv workloads/mixed.csv --policy rr --quantum 8 --export-csv\n";
}

template <typename T>
static T parse_num(const std::string &flag, const std::string &v)
{
    std::istringstream ss(v);
    T x{};
    if (!(ss >> x) || !ss.eof())
    {
        std::cerr << "Invalid value for " << flag << ": " << v << "\n";
        std::exit(2);
    }
    return x;
}

static SimConfig parse_args(int argc, char **argv)
{
    SimConfig cfg;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto need = [&](const char *flag) -> std::string
        {
            if (i + 1 >= argc)
            {
                std::cerr << flag << " needs a value\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (a == "--help" || a == "-h")
        {
            print_usage(argv[0]);
            std::exit(0);
        }
        else if (a == "--workload")
            cfg.workload = need("--workload");
        else if (a == "--csv")
            cfg.csv_path = need("--csv");
        else if (a == "--count")
            cfg.count = parse_num<int>(a, need("--count"));
        else if (a == "--load")
            cfg.load = parse_num<double>(a, need("--load"));
        else if (a == "--seed")
            cfg.seed = parse_num<uint32_t>(a, need("--seed"));
        else if (a == "--dump-workload")
            cfg.dump_workload = need("--dump-workload");
        else if (a == "--policy")
        {
            std::string p = need("--policy");
            auto pol = parse_policy(p);
            if (!pol)
            {
                std::cerr << "Unknown policy: " << p << "\n";
                std::exit(2);
            }
            cfg.policy = *pol;
        }
        else if (a == "--no-adaptive")
            cfg.policy = SchedulerPolicy::MLFQ;
        else if (a == "--quantum")
            cfg.rr_quantum = parse_num<Tick>(a, need("--quantum"));
        else if (a == "--aging")
            cfg.aging_interval = parse_num<Tick>(a, need("--aging"));
        else if (a == "--ctx-overhead")
            cfg.context_switch_overhead = parse_num<Tick>(a, need("--ctx-overhead"));
        else if (a == "--max-ticks")
            cfg.max_ticks = parse_num<Tick>(a, need("--max-ticks"));
        else if (a == "--bandit-model")
            cfg.bandit_model = need("--bandit-model");
        else if (a == "--bandit-train")
            cfg.bandit_train = true;
        else if (a == "--json")
            cfg.json = cfg.quiet = true;
        else if (a == "--trace")
            cfg.trace = true;
        else if (a == "--export-csv")
            cfg.export_csv = true;
        else if (a == "--export-timeline")
            cfg.export_timeline = true;
        else if (a == "--output-dir")
            cfg.output_dir = need("--output-dir");
        else if (a == "--quiet")
            cfg.quiet = true;
        else
        {
            std::cerr << "Unknown argument: " << a << " (see --help)\n";
            std::exit(2);
        }
    }
    if (cfg.policy == SchedulerPolicy::BANDIT && cfg.bandit_model.empty())
    {
        std::cerr << "--policy bandit needs --bandit-model <path>\n";
        std::exit(2);
    }
    return cfg;
}

int main(int argc, char **argv)
{
    SimConfig cfg = parse_args(argc, argv);
    try
    {
        SimulationController sim(cfg);
        Summary s = sim.run(std::cout);
        if (cfg.json && cfg.dump_workload.empty())
            Statistics::write_json(std::cout, s);
    }
    catch (const std::exception &e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
