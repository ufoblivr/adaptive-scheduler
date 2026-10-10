#!/usr/bin/env python3
"""
Builds output/adaptivesched_dashboard.html from output/benchmark_results.json.

The page is a single self-contained HTML file (data embedded; Plotly.js loaded
from its CDN). Standard library only. Run benchmark.py first.
"""

import json
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, "output", "benchmark_results.json")
DST = os.path.join(ROOT, "output", "adaptivesched_dashboard.html")
PAGES = os.path.join(ROOT, "docs", "index.html")  # served by GitHub Pages (Settings -> Pages -> main /docs)


def main():
    if not os.path.exists(SRC):
        sys.exit("output/benchmark_results.json not found - run `python3 benchmark.py` first.")
    with open(SRC) as f:
        res = json.load(f)
    res.pop("runs", None)  # keep the page small; aggregates are enough
    html = TEMPLATE.replace("/*__DATA__*/", json.dumps(res))
    os.makedirs(os.path.dirname(PAGES), exist_ok=True)
    for path in (DST, PAGES):
        with open(path, "w", encoding="utf-8") as f:
            f.write(html)
        print(f"Dashboard written to {os.path.relpath(path, ROOT)}")


TEMPLATE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>AdaptiveSched++ Benchmark</title>
<script src="https://cdn.plot.ly/plotly-2.27.0.min.js"></script>
<style>
  :root {
    --bg: #f7f7f5; --card: #ffffff; --ink: #1d1d1b; --muted: #6b6b66; --line: #e3e2dd;
    --good: #1f7a4d; --bad: #b23b2e; --accent: #2f5fb3;
  }
  @media (prefers-color-scheme: dark) {
    :root { --bg: #151514; --card: #1f1f1d; --ink: #ecebe6; --muted: #9a9993; --line: #33332f;
            --good: #5cc28f; --bad: #e07a6c; --accent: #7aa2f0; }
  }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--ink);
         font: 15px/1.5 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
  main { max-width: 1180px; margin: 0 auto; padding: 32px 16px 64px; }
  h1 { font-size: 28px; margin: 0 0 4px; letter-spacing: -0.01em; }
  h2 { font-size: 18px; margin: 0 0 4px; }
  p.lede { color: var(--muted); margin: 0 0 24px; max-width: 760px; }
  p.note { color: var(--muted); font-size: 13px; margin: 6px 0 0; }
  .kpis { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 12px; margin-bottom: 20px; }
  .kpi, .card { background: var(--card); border: 1px solid var(--line); border-radius: 10px; padding: 16px; }
  .kpi .v { font-size: 26px; font-weight: 650; font-variant-numeric: tabular-nums; }
  .kpi .l { color: var(--muted); font-size: 13px; }
  .good { color: var(--good); } .bad { color: var(--bad); }
  .card { margin-bottom: 16px; }
  .row { display: flex; gap: 12px; align-items: center; flex-wrap: wrap; margin: 8px 0; }
  select { font: inherit; padding: 4px 8px; border-radius: 6px; border: 1px solid var(--line);
           background: var(--card); color: var(--ink); }
  .plot { width: 100%; min-height: 360px; }
  table { border-collapse: collapse; width: 100%; font-variant-numeric: tabular-nums; font-size: 13px; }
  th, td { padding: 6px 8px; border-bottom: 1px solid var(--line); text-align: right; white-space: nowrap; }
  th:first-child, td:first-child { text-align: left; }
  .scroll { overflow-x: auto; }
  td.best { font-weight: 700; color: var(--accent); }
</style>
</head>
<body>
<main>
  <h1>AdaptiveSched++</h1>
  <p class="lede" id="lede"></p>
  <div class="kpis" id="kpis"></div>

  <div class="card">
    <h2>How far is each policy from the best fixed policy?</h2>
    <p class="note">Mean waiting time ÷ that of the best practical fixed policy for the workload (chosen in hindsight).
      1.00 = matches it. SJF/SRTF are oracles that know job lengths in advance.</p>
    <div id="heat" class="plot"></div>
  </div>

  <div class="card">
    <h2>Per-workload comparison</h2>
    <div class="row">
      <label>Metric <select id="metric"></select></label>
      <label>Workload <select id="workload"></select></label>
    </div>
    <div id="bars" class="plot"></div>
    <p class="note">Bars are means over held-out seeds; whiskers are 95% confidence intervals.</p>
  </div>

  <div class="card">
    <h2>Which policy is active over time (phased workload)</h2>
    <p class="note" id="phase-note"></p>
    <div id="timeline" class="plot" style="min-height:260px"></div>
  </div>

  <div class="card">
    <h2>What the bandit learned</h2>
    <p class="note">Mean reward (completions − arrivals per 200-tick window) for each context × policy, from training.
      Cells with fewer than 50 visits are ignored when acting.</p>
    <div id="qtable" class="plot" style="min-height:520px"></div>
  </div>

  <div class="card">
    <h2>All aggregates</h2>
    <div class="row"><label>Metric <select id="metric2"></select></label></div>
    <div class="scroll"><table id="tbl"></table></div>
  </div>
</main>

<script>
const R = /*__DATA__*/;
const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
const dark = matchMedia('(prefers-color-scheme: dark)').matches;
const W = R.config.workloads, P = R.config.policies, A = R.aggregates, C = R.comparisons, ROB = R.robustness;
const label = p => ({rr:'RR', fcfs:'FCFS', sjf:'SJF', srtf:'SRTF', priority:'PRIORITY', mlfq:'MLFQ', adaptive:'ADAPTIVE', bandit:'BANDIT'})[p] || p;
const COLORS = {fcfs:'#8c8c86', rr:'#c49a3a', priority:'#9467bd', mlfq:'#3a7fc4', sjf:'#b5b5ae', srtf:'#d0d0ca',
                adaptive:'#1f9d6a', bandit:'#d0603f'};
const POLCOLOR = {FCFS:'#8c8c86', RR:'#c49a3a', PRIORITY:'#9467bd', MLFQ:'#3a7fc4'};
const layoutBase = () => ({
  paper_bgcolor: 'rgba(0,0,0,0)', plot_bgcolor: 'rgba(0,0,0,0)',
  font: {color: css('--ink'), family: 'system-ui, sans-serif', size: 12},
  margin: {l: 60, r: 16, t: 16, b: 48},
  xaxis: {gridcolor: css('--line'), zerolinecolor: css('--line')},
  yaxis: {gridcolor: css('--line'), zerolinecolor: css('--line')},
  legend: {orientation: 'h', y: -0.2},
});
const cfg = {displayModeBar: false, responsive: true};
const fmt = (v, d=0) => v.toLocaleString(undefined, {maximumFractionDigits: d, minimumFractionDigits: d});

// ── lede + KPIs ──
const nEval = R.config.eval_seeds.length;
document.getElementById('lede').textContent =
  `${P.length} policies × ${W.length} workloads × ${nEval} held-out seeds. The bandit was trained on ` +
  `${R.config.train_seeds.length} other seeds using only ${R.config.train_workloads.join(', ')} — ` +
  `heavy_tail and phased are unseen. Context-switch overhead: 1 tick.`;
const practical = R.config.fixed_practical;
const bestFixedWorst = Math.min(...practical.map(p => ROB[p].worst));
const ph = C.phased.adaptive;
const kpis = [
  {v: `${ph.vs_best_fixed_pct > 0 ? '+' : ''}${fmt(ph.vs_best_fixed_pct,1)}%`, cls: ph.vs_best_fixed_pct < 0 ? 'good' : 'bad',
   l: `Adaptive vs best fixed policy on the phased workload (wins ${ph.beats_best_fixed}/${ph.n} seeds)`},
  {v: `${fmt(ROB.adaptive.worst,2)}×`, cls: 'good',
   l: `Adaptive's worst-case ratio to the best fixed policy (worst on ${ROB.adaptive.worst_workload})`},
  {v: `${fmt(bestFixedWorst,2)}×`, cls: 'bad',
   l: `Best worst-case ratio any single fixed policy achieves`},
  {v: `${fmt(ROB.bandit.worst,2)}×`, cls: ROB.bandit.worst < bestFixedWorst ? 'good' : 'bad',
   l: `Learned bandit's worst-case ratio (worst on ${ROB.bandit.worst_workload})`},
];
document.getElementById('kpis').innerHTML = kpis.map(k =>
  `<div class="kpi"><div class="v ${k.cls}">${k.v}</div><div class="l">${k.l}</div></div>`).join('');

// ── heatmap ──
{
  const order = [...P].sort((a, b) => ROB[a].geomean - ROB[b].geomean);
  const z = order.map(p => W.map(w => ROB[p].ratios[w]));
  const text = z.map(r => r.map(v => v.toFixed(2) + '×'));
  Plotly.newPlot('heat', [{
    type: 'heatmap', x: W, y: order.map(label), z: z.map(r => r.map(Math.log2)), text,
    texttemplate: '%{text}', hovertemplate: '%{y} on %{x}: %{text}<extra></extra>',
    colorscale: [[0, '#2f8f5b'], [0.5, dark ? '#2a2a27' : '#f4f3ef'], [1, '#c0503f']], zmid: 0, zmin: -1.5, zmax: 1.5,
    showscale: false,
  }], {...layoutBase(), margin: {l: 90, r: 16, t: 8, b: 40}, height: 380,
       yaxis: {autorange: 'reversed'}}, cfg);
}

// ── bar chart ──
const METRICS = {avg_waiting: 'Mean waiting time (ticks)', p95_waiting: 'p95 waiting time (ticks)',
  avg_response_interactive: 'Mean response time, interactive jobs (ticks)', p95_slowdown: 'p95 slowdown (×)',
  jain_fairness: "Jain's fairness index", switch_overhead: 'Share of time lost to context switches',
  policy_switches: 'Policy switches per run'};
for (const id of ['metric', 'metric2']) {
  document.getElementById(id).innerHTML = Object.entries(METRICS).map(([k, v]) => `<option value="${k}">${v}</option>`).join('');
}
document.getElementById('workload').innerHTML = ['all', ...W].map(w => `<option>${w}</option>`).join('');

function drawBars() {
  const m = document.getElementById('metric').value, w = document.getElementById('workload').value;
  let traces;
  if (w === 'all') {
    traces = P.map(p => ({type: 'bar', name: label(p), x: W, y: W.map(x => A[x][p][m].mean),
      error_y: {type: 'data', array: W.map(x => A[x][p][m].ci95), thickness: 1, width: 2},
      marker: {color: COLORS[p]}}));
  } else {
    traces = [{type: 'bar', x: P.map(label), y: P.map(p => A[w][p][m].mean),
      error_y: {type: 'data', array: P.map(p => A[w][p][m].ci95), thickness: 1, width: 3},
      marker: {color: P.map(p => COLORS[p])}}];
  }
  const logy = w === 'all' && ['avg_waiting', 'p95_waiting', 'avg_response_interactive', 'p95_slowdown'].includes(m);
  Plotly.react('bars', traces, {...layoutBase(), barmode: 'group', height: 400,
    yaxis: {...layoutBase().yaxis, title: METRICS[m] + (logy ? ' — log scale' : ''), type: logy ? 'log' : 'linear'}}, cfg);
}
document.getElementById('metric').onchange = drawBars;
document.getElementById('workload').onchange = drawBars;
drawBars();

// ── timeline ──
{
  const T = R.phased_timeline, rows = Object.keys(T.segments);
  const traces = [];
  const seen = new Set();
  rows.forEach((meta) => {
    for (const [s, e, pol] of T.segments[meta]) {
      traces.push({type: 'bar', orientation: 'h', y: [label(meta)], x: [e - s], base: [s],
        marker: {color: POLCOLOR[pol] || '#999'}, name: pol, legendgroup: pol, showlegend: !seen.has(pol),
        hovertemplate: `${label(meta)}: ${pol} from %{base} to ${e}<extra></extra>`});
      seen.add(pol);
    }
  });
  const end = Math.max(...rows.map(r => T.segments[r].at(-1)[1]));
  const shapes = T.phases.map(p => ({type: 'line', x0: p.start, x1: p.start, y0: -0.5, y1: rows.length - 0.5,
    line: {color: css('--muted'), dash: 'dot', width: 1}}));
  const ann = T.phases.map(p => ({x: p.start, y: rows.length - 0.45, text: p.label.replace(/^p\d_/, ''), showarrow: false,
    xanchor: 'left', yanchor: 'bottom', font: {size: 11, color: css('--muted')}}));
  Plotly.newPlot('timeline', traces, {...layoutBase(), barmode: 'stack', height: 260, shapes, annotations: ann,
    margin: {l: 90, r: 16, t: 28, b: 48}, xaxis: {...layoutBase().xaxis, title: 'simulation time (ticks)', range: [0, end]}}, cfg);
  document.getElementById('phase-note').textContent =
    `Seed ${T.seed}. Dotted lines mark when each phase's first job arrives; queued work from the previous phase ` +
    `is still draining at that moment, so switches lag the boundary.`;
}

// ── Q-table ──
{
  const arms = ['FCFS', 'RR', 'PRIORITY', 'MLFQ'];
  const cvs = ['cv<0.5', 'cv 0.5-1.2', 'cv>1.2'], qds = ['queue<2', 'queue 2-8', 'queue>8'],
        pcs = ['prio uninformative', 'prio informative'];
  const ctxLabel = c => c === 18 ? 'warm-up' : `${cvs[Math.floor((c % 9) / 3)]}, ${qds[c % 3]}, ${pcs[Math.floor(c / 9)]}`;
  const q = {}, n = {};
  for (const [c, a, visits, val] of R.bandit_q_table) { q[`${c},${a}`] = +val; n[`${c},${a}`] = +visits; }
  const ctxs = [...Array(19).keys()];
  const z = ctxs.map(c => arms.map((_, a) => n[`${c},${a}`] >= 50 ? q[`${c},${a}`] : null));
  const text = ctxs.map(c => arms.map((_, a) => `${(q[`${c},${a}`] ?? 0).toFixed(2)}<br>n=${n[`${c},${a}`] ?? 0}`));
  Plotly.newPlot('qtable', [{type: 'heatmap', x: arms, y: ctxs.map(ctxLabel), z, text, texttemplate: '%{text}',
    hovertemplate: '%{y} / %{x}: %{text}<extra></extra>', zmid: 0, showscale: false,
    colorscale: [[0, '#c0503f'], [0.5, dark ? '#2a2a27' : '#f4f3ef'], [1, '#2f8f5b']],
    textfont: {size: 10}}],
    {...layoutBase(), height: 560, margin: {l: 300, r: 16, t: 8, b: 40}, yaxis: {autorange: 'reversed'}}, cfg);
}

// ── table ──
function drawTable() {
  const m = document.getElementById('metric2').value;
  const higherBetter = m === 'jain_fairness';
  let h = '<tr><th>Workload</th>' + P.map(p => `<th>${label(p)}</th>`).join('') + '</tr>';
  for (const w of W) {
    const vals = P.map(p => A[w][p][m].mean);
    const practicalVals = P.map((p, i) => practical.includes(p) || R.config.meta.includes(p) ? vals[i] : null)
                           .filter(v => v !== null);
    const best = higherBetter ? Math.max(...practicalVals) : Math.min(...practicalVals);
    h += `<tr><td>${w}</td>` + P.map((p, i) => {
      const d = m === 'switch_overhead' || m === 'jain_fairness' ? 3 : 0;
      const isBest = vals[i] === best && !R.config.oracles.includes(p);
      return `<td class="${isBest ? 'best' : ''}">${fmt(vals[i], d)} <span style="color:var(--muted)">±${fmt(A[w][p][m].ci95, d)}</span></td>`;
    }).join('') + '</tr>';
  }
  document.getElementById('tbl').innerHTML = h;
}
document.getElementById('metric2').onchange = drawTable;
drawTable();
</script>
</body>
</html>
"""

if __name__ == "__main__":
    main()
