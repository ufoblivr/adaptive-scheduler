#pragma once

#include "../core/process.h"
#include "../core/ready_queue.h"

#include <functional>
#include <string>
#include <vector>

namespace adaptive_sched
{

    // What the CPU did during one tick; handed to schedulers for bookkeeping.
    enum class TickKind
    {
        IDLE,     // nothing to run
        SWITCH,   // paying context-switch overhead
        EXECUTE   // a process made progress
    };

    struct TickInfo
    {
        Tick now = 0;
        TickKind kind = TickKind::IDLE;
        Process *running = nullptr; // process holding the CPU after this tick (may be null)
        bool completed = false;     // a process finished at the end of this tick
    };

    // ---------------------------------------------------------------------------
    // IScheduler — the policy half of the simulator.
    //
    // The CPUEngine owns the mechanism (time, dispatch, context-switch cost,
    // execution); a scheduler only decides *who* runs and *when to preempt*.
    // The contract each tick, in order:
    //
    //   1. add()            for every process arriving at this tick
    //   2. should_preempt() for the running process; if true → requeue()
    //   3. pick_next()      if the CPU is free
    //   4. (engine executes one tick)
    //   5. on_complete()    if the running process finished
    //   6. on_tick()        always — aging, priority boosts, meta-policy evaluation
    //
    // Every process is always in exactly one place: a ready queue, the CPU, or
    // the completed list. The tests check this conservation property.
    // ---------------------------------------------------------------------------
    class IScheduler
    {
    public:
        virtual ~IScheduler() = default;

        virtual std::string name() const = 0;
        virtual SchedulerPolicy policy() const = 0;

        // A new arrival enters the ready queue
        virtual void add(ProcessPtr p, Tick now) = 0;

        // Remove and return the process to dispatch next (nullptr if none ready)
        virtual ProcessPtr pick_next(Tick now) = 0;

        // Should the running process give up the CPU now?
        virtual bool should_preempt(const Process &running, Tick now) const = 0;

        // A preempted process goes back to the ready queue
        virtual void requeue(ProcessPtr p, Tick now) = 0;

        virtual void on_complete(const ProcessPtr & /*p*/, Tick /*now*/) {}
        virtual void on_tick(const TickInfo & /*info*/) {}

        virtual std::size_t ready_count() const = 0;

        // Migration support for meta-schedulers: hand over every queued process
        virtual std::vector<ProcessPtr> drain() = 0;
        virtual void import(std::vector<ProcessPtr> procs, Tick now) = 0;

        // Inspection (oldest-waiting age etc.)
        virtual void for_each_ready(const std::function<void(const ProcessPtr &)> &f) const = 0;

        // The concrete policy making decisions right now (differs for meta-schedulers)
        virtual SchedulerPolicy active_policy() const { return policy(); }
    };

    // Shared helper for single-queue schedulers
    class SingleQueueScheduler : public IScheduler
    {
    public:
        explicit SingleQueueScheduler(QueueOrder order) : queue_(order) {}

        void add(ProcessPtr p, Tick now) override { enqueue(std::move(p), now); }
        void requeue(ProcessPtr p, Tick now) override { enqueue(std::move(p), now); }

        ProcessPtr pick_next(Tick /*now*/) override
        {
            ProcessPtr p = queue_.pop();
            if (p)
                p->state = ProcessState::RUNNING;
            return p;
        }

        std::size_t ready_count() const override { return queue_.size(); }
        std::vector<ProcessPtr> drain() override { return queue_.drain(); }
        void import(std::vector<ProcessPtr> procs, Tick now) override
        {
            for (auto &p : procs)
                enqueue_preserving_wait(std::move(p), now);
        }
        void for_each_ready(const std::function<void(const ProcessPtr &)> &f) const override
        {
            queue_.for_each(f);
        }

    protected:
        void enqueue(ProcessPtr p, Tick now)
        {
            p->state = ProcessState::READY;
            p->ready_since = now;
            queue_.push(std::move(p));
        }
        // Migration must not reset how long a process has been waiting
        void enqueue_preserving_wait(ProcessPtr p, Tick /*now*/)
        {
            p->state = ProcessState::READY;
            queue_.push(std::move(p));
        }

        ReadyQueue queue_;
    };

} // namespace adaptive_sched
