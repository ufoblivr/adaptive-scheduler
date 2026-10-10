#include "../../include/execution/cpu.h"

#include <algorithm>

namespace adaptive_sched
{

    CPUEngine::CPUEngine(IScheduler &scheduler, CPUConfig cfg)
        : sched_(scheduler), cfg_(cfg)
    {
    }

    ProcessPtr CPUEngine::dispatch(ProcessPtr next)
    {
        if (last_ran_ && last_ran_ != next)
        {
            switch_left_ = cfg_.context_switch_overhead;
            ++counters_.context_switches;
        }
        next->state = ProcessState::RUNNING;
        next->slice_used = 0;
        ++next->dispatches;
        last_ran_ = next;
        return next;
    }

    void CPUEngine::record(Tick t, TickKind kind, const Process *p)
    {
        if (!cfg_.record_timeline)
            return;
        PID pid = p ? p->pid : 0;
        SchedulerPolicy pol = sched_.active_policy();
        if (!timeline_.empty())
        {
            auto &last = timeline_.back();
            if (last.end == t && last.kind == kind && last.pid == pid && last.policy == pol)
            {
                last.end = t + 1;
                return;
            }
        }
        timeline_.push_back({t, t + 1, kind, pid, pol});
    }

    std::vector<ProcessPtr> CPUEngine::run(std::vector<ProcessPtr> pending)
    {
        std::stable_sort(pending.begin(), pending.end(),
                         [](const ProcessPtr &a, const ProcessPtr &b)
                         {
                             if (a->arrival_time != b->arrival_time)
                                 return a->arrival_time < b->arrival_time;
                             return a->pid < b->pid;
                         });

        const std::size_t n = pending.size();
        std::vector<ProcessPtr> completed;
        completed.reserve(n);

        std::size_t next_arrival = 0;
        ProcessPtr running;
        Tick t = 0;

        while (completed.size() < n)
        {
            if (cfg_.max_ticks && t >= cfg_.max_ticks)
                break;

            // 1. Arrivals
            while (next_arrival < n && pending[next_arrival]->arrival_time <= t)
                sched_.add(pending[next_arrival++], t);

            // 2. Preemption (never in the middle of a context switch)
            if (running && switch_left_ == 0 && sched_.should_preempt(*running, t))
            {
                ProcessPtr prev = std::move(running);
                sched_.requeue(prev, t);
                ProcessPtr next = sched_.pick_next(t);
                if (next == prev)
                {
                    // Nobody better was waiting: same process keeps the CPU with a
                    // fresh quantum. Not a preemption, no switch cost.
                    running = std::move(next);
                    running->state = ProcessState::RUNNING;
                    running->slice_used = 0;
                }
                else
                {
                    ++prev->preemptions;
                    ++counters_.preemptions;
                    if (next)
                        running = dispatch(std::move(next));
                }
            }

            // 3. Dispatch onto a free CPU
            if (!running)
            {
                if (ProcessPtr next = sched_.pick_next(t))
                    running = dispatch(std::move(next));
            }

            // 4. Execute one tick
            TickInfo info;
            info.now = t;
            if (!running)
            {
                info.kind = TickKind::IDLE;
                ++counters_.idle_ticks;
            }
            else if (switch_left_ > 0)
            {
                info.kind = TickKind::SWITCH;
                --switch_left_;
                ++counters_.switch_ticks;
            }
            else
            {
                info.kind = TickKind::EXECUTE;
                ++counters_.busy_ticks;
                if (!running->started())
                    running->start_time = t;
                --running->remaining_time;
                ++running->slice_used;
                if (running->remaining_time == 0)
                {
                    running->finish(t + 1);
                    running->completed_under = static_cast<int>(sched_.active_policy());
                    sched_.on_complete(running, t + 1);
                    completed.push_back(running);
                    info.completed = true;
                }
            }
            ++counters_.ticks_by_policy[static_cast<std::size_t>(sched_.active_policy())];
            record(t, info.kind, running.get());

            // 5. Scheduler bookkeeping (aging, boosts, meta-policy evaluation)
            info.running = running.get();
            sched_.on_tick(info);
            if (info.completed)
                running.reset();

            ++t;
        }

        counters_.makespan = t;
        return completed;
    }

} // namespace adaptive_sched
