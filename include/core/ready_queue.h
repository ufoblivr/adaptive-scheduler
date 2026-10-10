#pragma once

#include "process.h"

#include <algorithm>
#include <deque>
#include <vector>

namespace adaptive_sched
{

    enum class QueueOrder
    {
        FIFO,           // enqueue order
        SHORTEST_FIRST, // smallest remaining_time first (ties: arrival, pid)
        PRIORITY        // smallest effective priority first (ties: arrival, pid)
    };

    // ---------------------------------------------------------------------------
    // ReadyQueue — an ordered ready queue.
    //
    // Backed by a deque kept sorted on insert. Insert is O(n) for the ordered
    // variants, pop-front is O(1), and arbitrary removal / re-sorting (needed by
    // aging) is straightforward, which a std::priority_queue would not allow.
    // Ties are broken deterministically so every run is reproducible.
    // ---------------------------------------------------------------------------
    class ReadyQueue
    {
    public:
        ReadyQueue() = default;
        explicit ReadyQueue(QueueOrder order) : order_(order) {}

        void push(ProcessPtr p)
        {
            if (order_ == QueueOrder::FIFO)
            {
                q_.push_back(std::move(p));
                return;
            }
            // upper_bound keeps FIFO order among equal keys
            auto it = std::upper_bound(q_.begin(), q_.end(), p,
                                       [this](const ProcessPtr &a, const ProcessPtr &b)
                                       { return less(a, b); });
            q_.insert(it, std::move(p));
        }

        ProcessPtr pop()
        {
            if (q_.empty())
                return nullptr;
            ProcessPtr p = std::move(q_.front());
            q_.pop_front();
            return p;
        }

        const ProcessPtr &front() const { return q_.front(); }
        bool empty() const { return q_.empty(); }
        std::size_t size() const { return q_.size(); }

        std::vector<ProcessPtr> drain()
        {
            std::vector<ProcessPtr> out(q_.begin(), q_.end());
            q_.clear();
            return out;
        }

        // Re-establish ordering after keys changed in place (e.g. aging)
        void resort()
        {
            if (order_ == QueueOrder::FIFO)
                return;
            std::stable_sort(q_.begin(), q_.end(),
                             [this](const ProcessPtr &a, const ProcessPtr &b)
                             { return less(a, b); });
        }

        template <typename F>
        void for_each(F &&f) const
        {
            for (const auto &p : q_)
                f(p);
        }

        QueueOrder order() const { return order_; }

    private:
        bool less(const ProcessPtr &a, const ProcessPtr &b) const
        {
            if (order_ == QueueOrder::SHORTEST_FIRST && a->remaining_time != b->remaining_time)
                return a->remaining_time < b->remaining_time;
            if (order_ == QueueOrder::PRIORITY && a->priority != b->priority)
                return a->priority < b->priority;
            if (a->arrival_time != b->arrival_time)
                return a->arrival_time < b->arrival_time;
            return a->pid < b->pid;
        }

        QueueOrder order_ = QueueOrder::FIFO;
        std::deque<ProcessPtr> q_;
    };

} // namespace adaptive_sched
