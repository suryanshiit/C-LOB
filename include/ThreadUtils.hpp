#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  Thread Affinity & Real-Time Scheduling Utilities
// ─────────────────────────────────────────────────────────────────────────────
//
//  Why pin threads to cores?
//
//  By default, the OS scheduler moves threads between cores freely. Each
//  migration:
//    - Flushes the L1/L2 cache warm-up the thread had built up
//    - Can cause a TLB shootdown (invalidating page table caches)
//    - Adds ~10-100µs of cold-start latency on the new core
//
//  For a matching engine, where we work hard to keep the order book in L2 cache
//  (~12 MB book fits in L3, hot path fits in L1/L2), a single OS migration
//  destroys the entire latency budget.
//
//  pin_thread_to_core() locks a thread to one physical core. The OS will never
//  move it. The L1/L2 cache stays warm across the entire trading session.
//
//  Why SCHED_FIFO?
//  The matching thread must not be preempted mid-order. SCHED_FIFO is a
//  real-time scheduling policy: the thread runs until it blocks or yields.
//  No time-slice interruptions. Requires CAP_SYS_NICE (run as root, or set
//  /etc/security/limits.conf rtprio).

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <pthread.h>
#include <sched.h>
#include <iostream>
#include <string>
#include <stdexcept>

namespace thread_utils {

// ── Core Pinning ─────────────────────────────────────────────────────────────

// Pin the CALLING thread to the given physical core.
// On hyperthreaded CPUs: prefer even core IDs (0, 2, 4...) for the matching
// thread — they map to physical cores, not HT siblings.
//
// In WSL2 or cloud VMs, this may silently fail (kernel ignores the affinity mask).
// We warn but do not throw — the code still runs correctly, just without pinning.
inline void pin_thread_to_core(int core_id, const std::string& thread_name = "") {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    const int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "[ThreadUtils] WARNING: Could not pin "
                  << (thread_name.empty() ? "thread" : thread_name)
                  << " to core " << core_id
                  << " (rc=" << rc << "). Running unpinned.\n"
                  << "  This is normal in WSL2 / cloud VMs / Docker containers.\n"
                  << "  On bare metal: requires no special privileges.\n";
    } else {
        std::cout << "[ThreadUtils] Pinned "
                  << (thread_name.empty() ? "thread" : thread_name)
                  << " to core " << core_id << ".\n";
    }
}

// ── Real-Time Priority ────────────────────────────────────────────────────────

// Attempt to elevate the calling thread to SCHED_FIFO real-time priority.
// This prevents the OS from preempting the matching thread mid-order.
//
// Requires: sudo / CAP_SYS_NICE, or set in /etc/security/limits.conf:
//   * hard rtprio 99
//   * soft rtprio 99
//
// If it fails, we warn and continue — correctness is unaffected.
inline void try_set_realtime_priority(int priority = 80) {
    struct sched_param sp{};
    sp.sched_priority = priority;

    const int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (rc != 0) {
        std::cerr << "[ThreadUtils] WARNING: Could not set SCHED_FIFO (rc=" << rc << ").\n"
                  << "  Run as root or grant CAP_SYS_NICE for real-time scheduling.\n"
                  << "  Continuing with normal (SCHED_OTHER) priority.\n";
    } else {
        std::cout << "[ThreadUtils] Real-time SCHED_FIFO priority " << priority << " set.\n";
    }
}

// ── NUMA-Aware Core Selection ─────────────────────────────────────────────────

// On a dual-socket machine, the parser and matching threads should be on the
// SAME NUMA node so queue reads/writes don't cross the QPI/UPI inter-socket bus
// (which adds ~60-80ns per access).
//
// Recommended layout for a 2-socket system (e.g., two 8-core Xeons):
//   Core 0: Parser / feed handler    (socket 0)
//   Core 2: Matching engine          (socket 0, different physical core)
//   Core 4: Risk / post-trade        (socket 0)
// Avoid cores 8+ (socket 1) for the critical path.

} // namespace thread_utils