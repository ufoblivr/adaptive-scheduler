#pragma once

#include "scheduler.h"

namespace adaptive_sched
{

    // ---------------------------------------------------------------------------
    // Shortest Job First (non-preemptive) and Shortest Remaining Time First
    // (preemptive).
    //
    // Both need to know each job's CPU burst in advance, which a real OS does
    // not. They are included as *oracle baselines*: SRTF minimises mean waiting
    // time, so it is the lower bound the practical policies are measured against.
    // The adaptive and bandit schedulers never use them.
    // ---------------------------------------------------------------------------
    class SJFScheduler : public SingleQueueScheduler
    {
    public:
        enum class Mode
        {
            NON_PREEMPTIVE,
            SRTF
        };

        explicit SJFScheduler(Mode mode = Mode::NON_PREEMPTIVE)
            : SingleQueueScheduler(QueueOrder::SHORTEST_FIRST), mode_(mode) {}

        std::string name() const override { return mode_ == Mode::SRTF ? "SRTF" : "SJF"; }
        SchedulerPolicy policy() const override
        {
            return mode_ == Mode::SRTF ? SchedulerPolicy::SRTF : SchedulerPolicy::SJF;
        }

        bool should_preempt(const Process &running, Tick) const override
        {
            if (mode_ != Mode::SRTF || queue_.empty())
                return false;
            return queue_.front()->remaining_time < running.remaining_time;
        }

    private:
        Mode mode_;
    };

} // namespace adaptive_sched
