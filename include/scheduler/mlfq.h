#pragma once

#include "scheduler.h"

#include <algorithm>
#include <array>

namespace adaptive_sched
{

    // ---------------------------------------------------------------------------
    // Multi-Level Feedback Queue (OSTEP-style rules).
    //
    //   Rule 1: higher level always runs first (a ready higher-level process
    //           preempts a lower-level one).
    //   Rule 2: same level → round-robin.
    //   Rule 3: new processes enter level 0.
    //   Rule 4: using up the level's quantum demotes the process one level.
    //   Rule 5: every `boost_interval` ticks, everything moves back to level 0
    //           (prevents starvation and lets phase-changing jobs recover).
    //
    // MLFQ needs no knowledge of burst lengths, yet approximates "short jobs
    // first" because short jobs finish before being demoted.
    // ---------------------------------------------------------------------------
    class MLFQScheduler : public IScheduler
    {
    public:
        static constexpr std::size_t NUM_LEVELS = 4;
        // 0 = run to completion at the bottom level
        static constexpr std::array<Tick, NUM_LEVELS> DEFAULT_QUANTA = {10, 20, 40, 0};

        explicit MLFQScheduler(Tick boost_interval = 1000,
                               std::array<Tick, NUM_LEVELS> quanta = DEFAULT_QUANTA)
            : quanta_(quanta), boost_interval_(boost_interval) {}

        std::string name() const override { return "MLFQ"; }
        SchedulerPolicy policy() const override { return SchedulerPolicy::MLFQ; }

        void add(ProcessPtr p, Tick now) override
        {
            p->queue_level = 0;
            push(std::move(p), now, true);
        }

        ProcessPtr pick_next(Tick) override
        {
            for (auto &lvl : levels_)
            {
                if (!lvl.empty())
                {
                    ProcessPtr p = lvl.pop();
                    p->state = ProcessState::RUNNING;
                    return p;
                }
            }
            return nullptr;
        }

        bool should_preempt(const Process &running, Tick) const override
        {
            std::size_t lvl = clamp_level(running.queue_level);
            for (std::size_t i = 0; i < lvl; ++i)
                if (!levels_[i].empty())
                    return true; // Rule 1
            return quantum_expired(running);
        }

        void requeue(ProcessPtr p, Tick now) override
        {
            if (quantum_expired(*p)) // Rule 4
                p->queue_level = static_cast<int>(std::min(clamp_level(p->queue_level) + 1, NUM_LEVELS - 1));
            push(std::move(p), now, true);
        }

        void on_tick(const TickInfo &info) override
        {
            if (boost_interval_ == 0 || (info.now + 1) % boost_interval_ != 0)
                return;
            // Rule 5 — collect lower levels in level order, append to level 0
            for (std::size_t i = 1; i < NUM_LEVELS; ++i)
            {
                for (auto &p : levels_[i].drain())
                {
                    p->queue_level = 0;
                    levels_[0].push(std::move(p));
                }
            }
            if (info.running && !info.completed)
            {
                info.running->queue_level = 0;
                info.running->slice_used = 0;
            }
            ++boosts_;
        }

        std::size_t ready_count() const override
        {
            std::size_t n = 0;
            for (const auto &l : levels_)
                n += l.size();
            return n;
        }

        std::vector<ProcessPtr> drain() override
        {
            std::vector<ProcessPtr> out;
            for (auto &l : levels_)
                for (auto &p : l.drain())
                    out.push_back(std::move(p));
            return out;
        }

        void import(std::vector<ProcessPtr> procs, Tick now) override
        {
            for (auto &p : procs)
                push(std::move(p), now, false);
        }

        void for_each_ready(const std::function<void(const ProcessPtr &)> &f) const override
        {
            for (const auto &l : levels_)
                l.for_each(f);
        }

        std::array<std::size_t, NUM_LEVELS> level_sizes() const
        {
            std::array<std::size_t, NUM_LEVELS> s{};
            for (std::size_t i = 0; i < NUM_LEVELS; ++i)
                s[i] = levels_[i].size();
            return s;
        }
        uint64_t boosts() const { return boosts_; }
        Tick quantum_for(std::size_t level) const { return quanta_[clamp_level(static_cast<int>(level))]; }

    private:
        static std::size_t clamp_level(int lvl)
        {
            if (lvl < 0)
                return 0;
            return std::min(static_cast<std::size_t>(lvl), NUM_LEVELS - 1);
        }

        bool quantum_expired(const Process &p) const
        {
            Tick q = quanta_[clamp_level(p.queue_level)];
            return q != 0 && p.slice_used >= q;
        }

        void push(ProcessPtr p, Tick now, bool reset_wait)
        {
            p->state = ProcessState::READY;
            if (reset_wait)
                p->ready_since = now;
            std::size_t lvl = clamp_level(p->queue_level);
            p->queue_level = static_cast<int>(lvl);
            levels_[lvl].push(std::move(p));
        }

        std::array<Tick, NUM_LEVELS> quanta_;
        Tick boost_interval_;
        std::array<ReadyQueue, NUM_LEVELS> levels_; // default-constructed FIFO queues
        uint64_t boosts_ = 0;
    };

} // namespace adaptive_sched
