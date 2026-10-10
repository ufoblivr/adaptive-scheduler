#pragma once

#include "scheduler.h"

namespace adaptive_sched
{

    // Round-Robin with a fixed quantum. When the quantum expires the process
    // goes to the tail of the queue — but only if someone else is waiting;
    // otherwise it keeps the CPU and no context switch is paid.
    class RRScheduler : public SingleQueueScheduler
    {
    public:
        explicit RRScheduler(Tick quantum = 20)
            : SingleQueueScheduler(QueueOrder::FIFO), quantum_(quantum ? quantum : 1) {}

        std::string name() const override { return "RR"; }
        SchedulerPolicy policy() const override { return SchedulerPolicy::ROUND_ROBIN; }

        bool should_preempt(const Process &running, Tick) const override
        {
            return running.slice_used >= quantum_ && !queue_.empty();
        }

        Tick quantum() const { return quantum_; }

    private:
        Tick quantum_;
    };

} // namespace adaptive_sched
