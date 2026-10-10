### Mean waiting time (ticks), mean of 30 held-out seeds

| Workload | FCFS | RR | PRIORITY | MLFQ | SJF | SRTF | ADAPTIVE | BANDIT |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| interactive | 280 | 266 | **164** | 263 | 119 | 75 | 182 | 238 |
| cpu_bound | **1,293** | 3,582 | 1,406 | 3,709 | 1,111 | 1,109 | 1,311 | 1,333 |
| batch | **3,846** | 10,932 | 3,957 | 10,436 | 3,530 | 3,529 | 3,945 | 3,972 |
| mixed | 2,100 | 2,029 | 1,952 | **1,835** | 761 | 563 | 1,959 | 1,826 |
| heavy_tail | 2,009 | 926 | 1,780 | **811** | 524 | 198 | 812 | 838 |
| stress | 11,524 | **9,536** | 11,506 | 10,002 | 3,053 | 2,879 | 10,195 | 9,900 |
| phased | 2,709 | 3,086 | **2,670** | 2,918 | 875 | 758 | 2,420 | 2,997 |

Bold = best practical fixed policy for that workload (chosen in hindsight). SJF/SRTF are oracles that know burst lengths in advance.

### Meta-schedulers vs. the best fixed policy for each workload

| Workload | Best fixed | Adaptive vs best | Adaptive wins | Bandit vs best | Bandit wins | Adaptive / SRTF |
|---|---|---:|---:|---:|---:|---:|
| interactive | PRIORITY | +10.3% ± 4.3 | 0/30 | +46.5% ± 6.3 | 0/30 | 2.42x |
| cpu_bound | FCFS | +1.3% ± 0.4 | 0/30 | +3.0% ± 0.5 | 0/30 | 1.18x |
| batch | FCFS | +2.2% ± 1.3 | 0/30 | +3.0% ± 1.3 | 0/30 | 1.12x |
| mixed | MLFQ | +14.8% ± 5.1 | 5/30 | -0.5% ± 0.5 | 20/30 | 3.48x |
| heavy_tail | MLFQ | +0.1% ± 0.2 | 5/30 | +5.7% ± 5.6 | 12/30 | 4.11x |
| stress | RR | +8.6% ± 3.4 | 7/30 | +3.4% ± 0.9 | 2/30 | 3.54x |
| phased | PRIORITY | -8.0% ± 2.2 | 28/30 | +10.6% ± 8.3 | 16/30 | 3.19x |

Negative = lower mean waiting time than the best fixed policy (paired by seed, 95% CI). "wins" = seeds where it beat that policy.

### Robustness: mean waiting time relative to the best fixed policy, per workload

| Policy | Geometric mean ratio | Worst-case ratio | Worst on |
|---|---:|---:|---|
| SRTF | 0.42x | 0.92x | batch |
| SJF | 0.55x | 0.92x | batch |
| ADAPTIVE | 1.03x | 1.11x | interactive |
| BANDIT | 1.09x | 1.45x | interactive |
| PRIORITY | 1.18x | 2.19x | heavy_tail |
| FCFS | 1.29x | 2.48x | heavy_tail |
| RR | 1.52x | 2.84x | batch |
| MLFQ | 1.46x | 2.87x | cpu_bound |

1.00x = as good as the best fixed policy chosen in hindsight for that workload. Below 1.00x is possible for oracles and for meta-schedulers on phase-changing work.

### Interactive response time and fairness (mean of held-out seeds)

| Workload | Metric | FCFS | RR | PRIORITY | MLFQ | SJF | SRTF | ADAPTIVE | BANDIT |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| interactive | resp. (interactive) | 282 | 83 | 70 | 5 | 77 | 8 | 69 | 8 |
| interactive | p95 slowdown | 43.5 | 17.6 | 18.0 | 14.7 | 12.0 | 3.2 | 19.3 | 14.2 |
| mixed | resp. (interactive) | 2,132 | 165 | 1,812 | 9 | 316 | 3 | 1,711 | 10 |
| mixed | p95 slowdown | 205.6 | 24.4 | 200.5 | 18.5 | 31.7 | 4.0 | 196.2 | 18.5 |
| phased | resp. (interactive) | 3,140 | 535 | 2,957 | 86 | 222 | 6 | 1,741 | 1,166 |
| phased | p95 slowdown | 307.2 | 76.2 | 302.5 | 62.1 | 26.9 | 6.8 | 226.4 | 253.3 |
