#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace adaptive_sched
{

    using Tick = uint64_t;
    using PID = uint32_t;

    enum class ProcessState
    {
        NEW,
        READY,
        RUNNING,
        TERMINATED
    };

    // Coarse workload label the adaptive scheduler assigns to each evaluation
    // window. Used for logging/explainability; the policy scores themselves are
    // continuous functions of the observed metrics.
    enum class WorkloadType
    {
        UNKNOWN,     // not enough completions observed yet
        HOMOGENEOUS, // similar service times: FCFS-friendly
        HETEROGENEOUS, // high service-time variance: favour short jobs
        INTERACTIVE, // many interactive arrivals: favour response time
        STARVING     // some ready process has waited far too long
    };

    enum class SchedulerPolicy
    {
        FCFS,
        SJF,
        SRTF,
        ROUND_ROBIN,
        PRIORITY,
        MLFQ,
        ADAPTIVE,
        BANDIT
    };

    inline std::string to_string(ProcessState s)
    {
        switch (s)
        {
        case ProcessState::NEW:
            return "NEW";
        case ProcessState::READY:
            return "READY";
        case ProcessState::RUNNING:
            return "RUNNING";
        case ProcessState::TERMINATED:
            return "TERMINATED";
        }
        return "UNKNOWN";
    }

    inline std::string to_string(WorkloadType w)
    {
        switch (w)
        {
        case WorkloadType::UNKNOWN:
            return "UNKNOWN";
        case WorkloadType::HOMOGENEOUS:
            return "HOMOGENEOUS";
        case WorkloadType::HETEROGENEOUS:
            return "HETEROGENEOUS";
        case WorkloadType::INTERACTIVE:
            return "INTERACTIVE";
        case WorkloadType::STARVING:
            return "STARVING";
        }
        return "UNKNOWN";
    }

    inline std::string to_string(SchedulerPolicy p)
    {
        switch (p)
        {
        case SchedulerPolicy::FCFS:
            return "FCFS";
        case SchedulerPolicy::SJF:
            return "SJF";
        case SchedulerPolicy::SRTF:
            return "SRTF";
        case SchedulerPolicy::ROUND_ROBIN:
            return "RR";
        case SchedulerPolicy::PRIORITY:
            return "PRIORITY";
        case SchedulerPolicy::MLFQ:
            return "MLFQ";
        case SchedulerPolicy::ADAPTIVE:
            return "ADAPTIVE";
        case SchedulerPolicy::BANDIT:
            return "BANDIT";
        }
        return "UNKNOWN";
    }

    inline std::optional<SchedulerPolicy> parse_policy(const std::string &s)
    {
        if (s == "fcfs")
            return SchedulerPolicy::FCFS;
        if (s == "sjf")
            return SchedulerPolicy::SJF;
        if (s == "srtf")
            return SchedulerPolicy::SRTF;
        if (s == "rr")
            return SchedulerPolicy::ROUND_ROBIN;
        if (s == "priority")
            return SchedulerPolicy::PRIORITY;
        if (s == "mlfq")
            return SchedulerPolicy::MLFQ;
        if (s == "adaptive")
            return SchedulerPolicy::ADAPTIVE;
        if (s == "bandit")
            return SchedulerPolicy::BANDIT;
        return std::nullopt;
    }

} // namespace adaptive_sched
