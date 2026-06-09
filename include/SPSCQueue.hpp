#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <new>

// ─────────────────────────────────────────────────────────────────────────────
//  Single-Producer Single-Consumer Lock-Free Ring Buffer
// ─────────────────────────────────────────────────────────────────────────────
//
//  Design principles:
//
//  1. Power-of-2 capacity
//     Replacing (idx % Capacity) with (idx & MASK) eliminates a hardware division
//     instruction on every push/pop, saving ~20-40 cycles each call.
//
//  2. Separate cache lines for producer vs. consumer state
//     The write index is only modified by the producer thread; the read index is
//     only modified by the consumer thread. If they shared a cache line, every
//     write on one core would invalidate the line on the other core via the MESI
//     protocol — "false sharing". With separate lines, the two cores never
//     compete for the same cache line.
//
//  3. Acquire/release atomics (no mutex, no full memory fence)
//     - memory_order_release on the store: all prior writes are visible to any
//       thread that subsequently loads this atomic with memory_order_acquire.
//     - memory_order_acquire on the load: sees all writes that happened before
//       the matching release store on the other thread.
//     This is the minimal ordering needed for correct SPSC — no lock, no fence.
//
//  4. Per-slot ready flag (instead of a single sequence counter)
//     Each slot has its own ready flag on its own cache line. The producer sets
//     it to true after writing data; the consumer checks it before reading. This
//     avoids any ABA problem and keeps the hot read path to a single cache-line
//     load on the consumer side.
//
//  Guarantee: ZERO mutexes, ZERO system calls in the push/pop hot path.

template<typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
        "SPSCQueue capacity must be a power of 2 (e.g. 1024, 65536).");

    static constexpr size_t MASK       = Capacity - 1;
    static constexpr size_t CACHE_LINE = 64;

    // Each slot holds the payload plus a ready flag.
    // Padding to 64 bytes means producer writes and consumer reads can never
    // land on the same cache line, eliminating false sharing within the array.
    struct alignas(CACHE_LINE) Slot {
        T    data;
        // ready: false = slot is free (consumer may have just read it, or never written)
        //        true  = slot has data ready for consumer
        std::atomic<bool> ready{false};
    };

    alignas(CACHE_LINE) Slot m_slots[Capacity];

    // Write cursor — only the PRODUCER thread increments this.
    // Lives on its own cache line so the consumer never touches it.
    alignas(CACHE_LINE) std::atomic<uint64_t> m_write_idx{0};

    // Read cursor — only the CONSUMER thread increments this.
    // Lives on its own cache line so the producer never touches it.
    alignas(CACHE_LINE) std::atomic<uint64_t> m_read_idx{0};

public:
    SPSCQueue()  = default;
    ~SPSCQueue() = default;

    // Non-copyable / non-movable: internal pointers and atomics must stay stable.
    SPSCQueue(const SPSCQueue&)            = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // ── Producer API ─────────────────────────────────────────────────────────

    // try_push: called by the PRODUCER thread only.
    // Returns false immediately if the queue is full (non-blocking).
    // Never blocks, never allocates, never calls the OS.
    [[nodiscard]] bool try_push(const T& val) noexcept {
        const uint64_t w = m_write_idx.load(std::memory_order_relaxed);
        const uint64_t r = m_read_idx.load(std::memory_order_acquire);

        // Full check: if distance between write and read cursors equals capacity,
        // the ring has wrapped all the way around and we'd overwrite unread data.
        if (w - r >= Capacity) return false;

        m_slots[w & MASK].data = val;
        // release: makes the data write above visible before the flag is seen as true
        m_slots[w & MASK].ready.store(true, std::memory_order_release);
        m_write_idx.store(w + 1, std::memory_order_release);
        return true;
    }

    // Blocking push: spin until space is available. Use for benchmarks where
    // dropping messages is unacceptable.
    void push(const T& val) noexcept {
        while (!try_push(val)) {
            // Pause hint: tells the CPU this is a spin-wait loop.
            // Reduces power consumption and branch misprediction penalty.
            // On x86: emits the PAUSE instruction (~5-40 cycles of intentional stall).
            __builtin_ia32_pause();
        }
    }

    // ── Consumer API ─────────────────────────────────────────────────────────

    // try_pop: called by the CONSUMER thread only.
    // Returns false immediately if the queue is empty (non-blocking).
    [[nodiscard]] bool try_pop(T& val) noexcept {
        const uint64_t r = m_read_idx.load(std::memory_order_relaxed);
        // acquire: if ready==true, all writes before the matching release are visible
        if (!m_slots[r & MASK].ready.load(std::memory_order_acquire)) return false;

        val = m_slots[r & MASK].data;
        // release: marks the slot as free for the producer to reuse
        m_slots[r & MASK].ready.store(false, std::memory_order_release);
        m_read_idx.store(r + 1, std::memory_order_release);
        return true;
    }

    // Blocking pop: spin until an item is available.
    T pop() noexcept {
        T val;
        while (!try_pop(val)) {
            __builtin_ia32_pause();
        }
        return val;
    }

    // ── Diagnostics ──────────────────────────────────────────────────────────

    // Approximate queue size — only safe to call from the consumer thread.
    [[nodiscard]] size_t size_approx() const noexcept {
        const uint64_t w = m_write_idx.load(std::memory_order_acquire);
        const uint64_t r = m_read_idx.load(std::memory_order_relaxed);
        return static_cast<size_t>(w - r);
    }

    [[nodiscard]] bool empty() const noexcept { return size_approx() == 0; }
    [[nodiscard]] bool full()  const noexcept { return size_approx() >= Capacity; }
};