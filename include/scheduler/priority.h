#pragma once

#include "scheduler.h"

namespace adaptive_sched
{

    // ---------------------------------------------------------------------------
    // Preemptive priority scheduling with aging.
    //
    // Lower number = more urgent. A newly ready process with strictly better
    // effective priority preempts the running one.
    //
    // Aging: every `aging_interval` ticks a process spends waiting in the ready
    // queue lowers its effective priority number by one (to a floor of 0), so a
    // low-priority process cannot starve forever. The boost is kept while the
    // process runs and reset to base priority if it is preempted.
    // aging_interval = 0 disables aging (useful for textbook tests).
    // ---------------------------------------------------------------------------
    class PriorityScheduler : public SingleQueueScheduler
    {
    public:
        explicit PriorityScheduler(Tick aging_interval = 100)
            : SingleQueueScheduler(QueueOrder::PRIORITY), aging_interval_(aging_interval) {}

        std::string name() const override { return "PRIORITY"; }
        SchedulerPolicy policy() const override { return SchedulerPolicy::PRIORITY; }

        // A process keeps its aged priority while it runs (so it can actually
        // make progress), and drops back to its base priority if preempted.
        void requeue(ProcessPtr p, Tick now) override
        {
            p->priority = p->base_priority;
            p->aging_credit = 0;
            enqueue(std::move(p), now);
        }

        bool should_preempt(const Process &running, Tick) const override
        {
            return !queue_.empty() && queue_.front()->priority < running.priority;
        }

        void on_tick(const TickInfo &) override
        {
            if (aging_interval_ == 0 || queue_.empty())
                return;
            bool changed = false;
            queue_.for_each([&](const ProcessPtr &p)
                            {
                if (++p->aging_credit >= aging_interval_)
                {
                    p->aging_credit = 0;
                    if (p->priority > 0)
                    {
                        --p->priority;
                        changed = true;
                    }
                } });
            if (changed)
                queue_.resort();
        }

    private:
        Tick aging_interval_;
    };

} // namespace adaptive_sched
