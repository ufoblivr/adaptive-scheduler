#!/usr/bin/env python3
"""
AdaptiveSched++ benchmark.

  1. Trains the bandit scheduler on TRAIN seeds, using only the five *stationary*
     workloads (heavy_tail and phased are never seen during training).
  2. Runs every policy on every workload over held-out EVAL seeds.
  3. Aggregates mean / std / 95% CI and paired comparisons, and writes
       output/benchmark_results.json   (raw runs + aggregates, read by the dashboard)
       output/benchmark_summary.md     (tables for the README)

The adaptive scheduler's thresholds were chosen by looking at TRAIN seeds only,
so the EVAL numbers are a held-out estimate for both meta-schedulers.

Standard library only. Usage:
    python3 benchmark.py                 # full run (~1-2 min on 8 cores)
    python3 benchmark.py --quick         # fewer seeds, for a smoke test
"""

import argparse
import json
import math
import os
import statistics
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, "output")

WORKLOADS = ["interactive", "cpu_bound", "batch", "mixed", "heavy_tail", "stress", "phased"]
TRAIN_WORKLOADS = ["interactive", "cpu_bound", "batch", "mixed", "stress"]
FIXED_PRACTICAL = ["fcfs", "rr", "priority", "mlfq"]  # need no knowledge of burst lengths
ORACLES = ["sjf", "srtf"]                             # need burst lengths in advance
META = ["adaptive", "bandit"]
POLICIES = FIXED_PRACTICAL + ORACLES + META
METRICS = ["avg_waiting", "p95_waiting", "p99_waiting", "avg_response", "avg_response_interactive",
           "avg_turnaround", "avg_slowdown", "p95_slowdown", "jain_fairness", "cpu_utilization",
           "switch_overhead", "context_switches", "preemptions", "policy_switches", "makespan"]


def find_binary(explicit):
    candidates = [explicit] if explicit else [
        os.path.join(ROOT, "build", "adaptive_sched"),
        os.path.join(ROOT, "build", "Release", "adaptive_sched.exe"),
        os.path.join(ROOT, "build", "adaptive_sched.exe"),
    ]
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    sys.exit("Simulator binary not found. Build first:\n"
             "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j")


def run(binary, *args):
    res = subprocess.run([binary, *args, "--json"], capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"{' '.join(args)}\n{res.stderr}")
    return json.loads(res.stdout.strip().splitlines()[-1])


def mean_ci(xs):
    m = statistics.fmean(xs)
    sd = statistics.stdev(xs) if len(xs) > 1 else 0.0
    return m, sd, 1.96 * sd / math.sqrt(len(xs)) if xs else 0.0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary")
    ap.add_argument("--train-seeds", type=int, default=40)
    ap.add_argument("--eval-seeds", type=int, default=30)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--quick", action="store_true", help="5 train / 5 eval seeds")
    args = ap.parse_args()
    if args.quick:
        args.train_seeds, args.eval_seeds = 5, 5

    binary = find_binary(args.binary)
    os.makedirs(OUT, exist_ok=True)
    model = os.path.join(OUT, "bandit_model.txt")
    train_seeds = list(range(1, args.train_seeds + 1))
    # Seeds 1001-1030 were used while developing; 2001+ were only used for the final report.
    eval_seeds = list(range(2001, 2001 + args.eval_seeds))

    # ── 1. Train the bandit (sequential: every run updates the same Q-table) ──
    if os.path.exists(model):
        os.remove(model)
    print(f"[1/3] Training bandit on {len(train_seeds)} seeds x {len(TRAIN_WORKLOADS)} stationary workloads ...")
    for s in train_seeds:
        for w in TRAIN_WORKLOADS:
            run(binary, "--workload", w, "--seed", str(s), "--policy", "bandit",
                "--bandit-train", "--bandit-model", model)
    print(f"      model -> {os.path.relpath(model, ROOT)}")

    # ── 2. Evaluate everything on held-out seeds (parallel) ──
    jobs = [(w, p, s) for w in WORKLOADS for p in POLICIES for s in eval_seeds]
    print(f"[2/3] Evaluating {len(POLICIES)} policies x {len(WORKLOADS)} workloads x "
          f"{len(eval_seeds)} held-out seeds = {len(jobs)} runs ...")

    def one(job):
        w, p, s = job
        extra = ["--bandit-model", model] if p == "bandit" else []
        r = run(binary, "--workload", w, "--policy", p, "--seed", str(s), *extra)
        r.update(workload=w, policy_key=p, seed=s)
        return r

    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        runs = list(ex.map(one, jobs))

    # ── 3. Aggregate ──
    print("[3/3] Aggregating ...")
    by = {}
    for r in runs:
        by.setdefault((r["workload"], r["policy_key"]), []).append(r)

    agg = {}
    for (w, p), rs in by.items():
        entry = {}
        for m in METRICS:
            mu, sd, ci = mean_ci([r[m] for r in rs])
            entry[m] = {"mean": mu, "std": sd, "ci95": ci}
        shares = {}
        for r in rs:
            for k, v in r["policy_share"].items():
                shares[k] = shares.get(k, 0.0) + v / len(rs)
        entry["policy_share"] = shares
        agg.setdefault(w, {})[p] = entry

    def wait(w, p, s):
        return next(r["avg_waiting"] for r in by[(w, p)] if r["seed"] == s)

    comparisons = {}
    for w in WORKLOADS:
        best_fixed = min(FIXED_PRACTICAL, key=lambda p: agg[w][p]["avg_waiting"]["mean"])
        comparisons[w] = {"best_fixed": best_fixed}
        for meta in META:
            deltas_best = [100 * (wait(w, meta, s) / wait(w, best_fixed, s) - 1) for s in eval_seeds]
            deltas_mlfq = [100 * (wait(w, meta, s) / wait(w, "mlfq", s) - 1) for s in eval_seeds]
            comparisons[w][meta] = {
                "vs_best_fixed_pct": statistics.fmean(deltas_best),
                "vs_best_fixed_ci95": mean_ci(deltas_best)[2],
                "beats_best_fixed": sum(d < 0 for d in deltas_best),
                "vs_mlfq_pct": statistics.fmean(deltas_mlfq),
                "beats_mlfq": sum(d < 0 for d in deltas_mlfq),
                "vs_srtf_ratio": agg[w][meta]["avg_waiting"]["mean"] / agg[w]["srtf"]["avg_waiting"]["mean"],
                "n": len(eval_seeds),
            }

    # Robustness: how far is each policy from the best fixed policy, per workload?
    robustness = {}
    for p in POLICIES:
        ratios = {w: agg[w][p]["avg_waiting"]["mean"] / agg[w][comparisons[w]["best_fixed"]]["avg_waiting"]["mean"]
                  for w in WORKLOADS}
        robustness[p] = {"ratios": ratios,
                         "worst": max(ratios.values()),
                         "worst_workload": max(ratios, key=ratios.get),
                         "geomean": math.exp(statistics.fmean(math.log(r) for r in ratios.values()))}

    # Gantt-style policy timeline for the phased workload (dashboard)
    timelines = {}
    phase_bounds = []
    seed0 = str(eval_seeds[0])
    tmp_csv = os.path.join(OUT, "phased_workload.csv")
    subprocess.run([binary, "--workload", "phased", "--seed", seed0, "--dump-workload", tmp_csv, "--quiet"], check=True)
    with open(tmp_csv) as f:
        rows = [line.strip().split(",") for line in f.readlines()[1:]]
    last_label = None
    for row in rows:
        label = row[1].rsplit("_", 1)[0]
        if label != last_label:
            phase_bounds.append({"label": label, "start": int(row[2])})
            last_label = label
    for meta in META:
        d = os.path.join(OUT, f"timeline_{meta}")
        extra = ["--bandit-model", model] if meta == "bandit" else []
        subprocess.run([binary, "--workload", "phased", "--policy", meta, "--seed", seed0,
                        "--export-timeline", "--output-dir", d, "--quiet", *extra], check=True)
        segs = []
        with open(os.path.join(d, "timeline.csv")) as f:
            for line in f.readlines()[1:]:
                start, end, _kind, _pid, pol = line.strip().split(",")
                start, end = int(start), int(end)
                if segs and segs[-1][2] == pol and segs[-1][1] == start:
                    segs[-1][1] = end
                else:
                    segs.append([start, end, pol])
        timelines[meta] = segs

    with open(model) as f:
        q_table = [line.split() for line in f if line[0].isdigit()]

    result = {
        "config": {"train_seeds": train_seeds, "eval_seeds": eval_seeds, "workloads": WORKLOADS,
                   "train_workloads": TRAIN_WORKLOADS, "policies": POLICIES,
                   "fixed_practical": FIXED_PRACTICAL, "oracles": ORACLES, "meta": META,
                   "context_switch_overhead": 1},
        "aggregates": agg,
        "comparisons": comparisons,
        "robustness": robustness,
        "phased_timeline": {"seed": int(seed0), "phases": phase_bounds, "segments": timelines},
        "bandit_q_table": q_table,
        "runs": runs,
    }
    with open(os.path.join(OUT, "benchmark_results.json"), "w") as f:
        json.dump(result, f)

    md = render_markdown(result)
    with open(os.path.join(OUT, "benchmark_summary.md"), "w") as f:
        f.write(md)
    print()
    print(md)
    print("Saved output/benchmark_results.json and output/benchmark_summary.md")
    print("Next: python3 generate_dashboard.py")


def render_markdown(res):
    agg, cmp_ = res["aggregates"], res["comparisons"]
    pols = res["config"]["policies"]
    n = len(res["config"]["eval_seeds"])
    lines = [f"### Mean waiting time (ticks), mean of {n} held-out seeds", "",
             "| Workload | " + " | ".join(p.upper() for p in pols) + " |",
             "|---|" + "---:|" * len(pols)]
    for w in res["config"]["workloads"]:
        best_fixed = cmp_[w]["best_fixed"]
        cells = []
        for p in pols:
            v = agg[w][p]["avg_waiting"]["mean"]
            txt = f"{v:,.0f}"
            if p == best_fixed:
                txt = f"**{txt}**"
            cells.append(txt)
        lines.append(f"| {w} | " + " | ".join(cells) + " |")
    lines += ["", "Bold = best practical fixed policy for that workload (chosen in hindsight). "
              "SJF/SRTF are oracles that know burst lengths in advance.", ""]

    lines += ["### Meta-schedulers vs. the best fixed policy for each workload", "",
              "| Workload | Best fixed | Adaptive vs best | Adaptive wins | Bandit vs best | Bandit wins | "
              "Adaptive / SRTF |",
              "|---|---|---:|---:|---:|---:|---:|"]
    for w in res["config"]["workloads"]:
        c = cmp_[w]
        a, b = c["adaptive"], c["bandit"]
        lines.append(f"| {w} | {c['best_fixed'].upper()} | {a['vs_best_fixed_pct']:+.1f}% ± {a['vs_best_fixed_ci95']:.1f} "
                     f"| {a['beats_best_fixed']}/{a['n']} | {b['vs_best_fixed_pct']:+.1f}% ± {b['vs_best_fixed_ci95']:.1f} "
                     f"| {b['beats_best_fixed']}/{b['n']} | {a['vs_srtf_ratio']:.2f}x |")
    lines += ["", "Negative = lower mean waiting time than the best fixed policy (paired by seed, 95% CI). "
              "\"wins\" = seeds where it beat that policy.", ""]

    rob = res["robustness"]
    lines += ["### Robustness: mean waiting time relative to the best fixed policy, per workload", "",
              "| Policy | Geometric mean ratio | Worst-case ratio | Worst on |",
              "|---|---:|---:|---|"]
    for p in sorted(pols, key=lambda p: rob[p]["worst"]):
        lines.append(f"| {p.upper()} | {rob[p]['geomean']:.2f}x | {rob[p]['worst']:.2f}x | {rob[p]['worst_workload']} |")
    lines += ["", "1.00x = as good as the best fixed policy chosen in hindsight for that workload. "
              "Below 1.00x is possible for oracles and for meta-schedulers on phase-changing work.", ""]

    lines += ["### Interactive response time and fairness (mean of held-out seeds)", "",
              "| Workload | Metric | " + " | ".join(p.upper() for p in pols) + " |",
              "|---|---|" + "---:|" * len(pols)]
    for w in ["interactive", "mixed", "phased"]:
        lines.append(f"| {w} | resp. (interactive) | " + " | ".join(
            f"{agg[w][p]['avg_response_interactive']['mean']:,.0f}" for p in pols) + " |")
        lines.append(f"| {w} | p95 slowdown | " + " | ".join(
            f"{agg[w][p]['p95_slowdown']['mean']:,.1f}" for p in pols) + " |")
    lines.append("")
    return "\n".join(lines)


if __name__ == "__main__":
    main()
