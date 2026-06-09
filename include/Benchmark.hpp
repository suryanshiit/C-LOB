#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  CPU-Cycle Accurate Benchmarking Utilities (rdtscp-based)
// ─────────────────────────────────────────────────────────────────────────────
//
//  Why not std::chrono::high_resolution_clock?
//
//  std::chrono has ~20-50ns overhead per call on most Linux systems. Measuring
//  a 50ns operation with a 20ns timer is useless. rdtscp reads the hardware
//  Time Stamp Counter directly — it has ~5-10 cycle overhead and counts at the
//  CPU's rated frequency (e.g., 3.5 GHz → 0.286 ns per tick).
//
//  Why RDTSCP over RDTSC?
//  RDTSC is not serialising: the CPU can execute it speculatively, before the
//  operations you wanted to time. RDTSCP waits for all prior instructions to
//  retire before reading the counter, giving a precise "after" timestamp.
//  We use CPUID as a compiler/CPU fence before the "start" RDTSC to prevent
//  earlier instructions from leaking past the measurement start.
//
//  The pattern used here is the Intel-recommended approach from:
//  "How to Benchmark Code Execution Times on Intel IA-32 and IA-64 ISA" (2010).

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <string>
#include <time.h>   // clock_gettime, CLOCK_MONOTONIC

namespace bench {

// ── TSC Read ─────────────────────────────────────────────────────────────────

// start_timer: fence with CPUID, then RDTSC.
// CPUID forces all prior instructions to complete before the timestamp is read.
static inline uint64_t start_timer() noexcept {
    uint32_t lo, hi;
    __asm__ volatile (
        "cpuid\n\t"     // serialising instruction: flushes the pipeline
        "rdtsc\n\t"     // now read the counter
        : "=a"(lo), "=d"(hi)
        :: "rbx", "rcx"
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// stop_timer: RDTSCP (serialising on the output side), then CPUID to prevent
// later instructions from executing before the stop timestamp is taken.
static inline uint64_t stop_timer() noexcept {
    uint32_t lo, hi, aux;
    __asm__ volatile (
        "rdtscp\n\t"            // waits for all prior instructions, then reads
        "mov %%eax, %0\n\t"
        "mov %%edx, %1\n\t"
        "mov %%ecx, %2\n\t"
        "cpuid\n\t"             // prevents later instructions reordering above here
        : "=r"(lo), "=r"(hi), "=r"(aux)
        :: "rax", "rbx", "rdx"
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// ── TSC Calibration ──────────────────────────────────────────────────────────

// Returns the TSC frequency in GHz (ticks per nanosecond).
// Measured against CLOCK_MONOTONIC which is accurate to 1ns on modern kernels.
// Call once at startup and cache the result.
inline double calibrate_tsc_ghz() noexcept {
    constexpr long CALIBRATION_NS = 200'000'000L; // 200 ms

    struct timespec t1, t2;
    // Use nanosleep for calibration: more reliable than busy-wait in VMs/WSL
    // where the clock may not advance smoothly during a spin loop.
    struct timespec sleep_req = {0, CALIBRATION_NS};
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const uint64_t c1 = start_timer();
    nanosleep(&sleep_req, nullptr);
    const uint64_t c2 = stop_timer();
    clock_gettime(CLOCK_MONOTONIC, &t2);
    const double wall_ns = static_cast<double>(
        (t2.tv_sec  - t1.tv_sec)  * 1'000'000'000L +
        (t2.tv_nsec - t1.tv_nsec));
    return static_cast<double>(c2 - c1) / wall_ns;
}

// ── Latency Statistics ────────────────────────────────────────────────────────

struct LatencyStats {
    double min_ns{};
    double mean_ns{};
    double median_ns{};
    double p95_ns{};
    double p99_ns{};
    double p999_ns{};
    double max_ns{};
    double stddev_ns{};
    size_t sample_count{};
};

// Compute statistics from a sorted array of nanosecond samples.
template<size_t N>
LatencyStats compute_stats(std::array<double, N>& samples, size_t count) {
    if (count == 0) return {};
    std::sort(samples.begin(), samples.begin() + count);

    LatencyStats s;
    s.sample_count = count;
    s.min_ns    = samples[0];
    s.max_ns    = samples[count - 1];
    s.median_ns = samples[count / 2];
    s.p95_ns    = samples[static_cast<size_t>(count * 0.95)];
    s.p99_ns    = samples[static_cast<size_t>(count * 0.99)];
    s.p999_ns   = samples[static_cast<size_t>(count * 0.999)];

    s.mean_ns = std::accumulate(samples.begin(), samples.begin() + count, 0.0) / count;

    double variance = 0.0;
    for (size_t i = 0; i < count; i++) {
        double d = samples[i] - s.mean_ns;
        variance += d * d;
    }
    s.stddev_ns = std::sqrt(variance / count);
    return s;
}

// ── Reporter ─────────────────────────────────────────────────────────────────

inline void print_stats(const std::string& label, const LatencyStats& s) {
    const int W = 12;
    std::cout << "\n╔══ " << label << " ══\n"
              << "║  Samples : " << s.sample_count << "\n"
              << "║  Min     : " << std::setw(W) << std::fixed << std::setprecision(1) << s.min_ns    << " ns\n"
              << "║  Mean    : " << std::setw(W) << s.mean_ns    << " ns\n"
              << "║  Median  : " << std::setw(W) << s.median_ns  << " ns\n"
              << "║  P95     : " << std::setw(W) << s.p95_ns     << " ns\n"
              << "║  P99     : " << std::setw(W) << s.p99_ns     << " ns\n"
              << "║  P99.9   : " << std::setw(W) << s.p999_ns    << " ns\n"
              << "║  Max     : " << std::setw(W) << s.max_ns     << " ns\n"
              << "║  StdDev  : " << std::setw(W) << s.stddev_ns  << " ns\n"
              << "╚══════════════════════════════\n";
}

// ── Throughput Helper ─────────────────────────────────────────────────────────

inline void print_throughput(const std::string& label, size_t messages, double seconds) {
    const double mps = messages / seconds / 1e6;
    const double ns_per_msg = (seconds * 1e9) / messages;
    std::cout << "╔══ Throughput: " << label << "\n"
              << "║  Messages   : " << messages << "\n"
              << "║  Wall time  : " << std::fixed << std::setprecision(3) << seconds * 1000 << " ms\n"
              << "║  Throughput : " << std::setprecision(2) << mps << " M msgs/sec\n"
              << "║  Per-message: " << std::setprecision(1) << ns_per_msg << " ns/msg\n"
              << "╚══════════════════════════════\n";
}

} // namespace bench