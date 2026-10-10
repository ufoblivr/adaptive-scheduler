#pragma once

#include "scheduler.h"

namespace adaptive_sched
{

    // First-Come First-Served: non-preemptive FIFO.
    // Minimal overhead; suffers the convoy effect when long jobs precede short ones.
    class FCFSScheduler : public SingleQueueScheduler
    {
    public:
        FCFSScheduler() : SingleQueueScheduler(QueueOrder::FIFO) {}

        std::string name() const override { return "FCFS"; }
        SchedulerPolicy policy() const override { return SchedulerPolicy::FCFS; }

        bool should_preempt(const Process &, Tick) const override { return false; }
    };

} // namespace adaptive_sched
