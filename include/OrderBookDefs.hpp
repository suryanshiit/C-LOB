#pragma once
#include <cstdint>
#include <cstddef>
#include <new>
#include <vector>
#include <stdexcept>
#include <memory>

// Core Enums
enum class Side : uint8_t { Buy, Sell };

// 1. Data Structure: The Order Entity
// Aligned to 64 bytes to perfectly fit a CPU cache line and prevent false sharing.
struct alignas(64) Order {
    uint64_t order_id;
    uint32_t price; // Fixed-point representation (e.g., USD * 100 or 10000)
    uint32_t quantity;
    Side side;
    
    // Intrusive Doubly-Linked List Pointers for O(1) queue management
    Order* next{nullptr};
    Order* prev{nullptr};
};

// 2. Memory Infrastructure: Fixed-Size Slab Allocator (Memory Pool)
// Pre-allocates a massive pool of Orders at startup. Allocation/Deallocation is O(1).
class OrderMemoryPool {
private:
    size_t m_capacity;
    std::vector<uint8_t> m_storage; // Raw byte backing storage
    Order* m_free_list{nullptr};    // Head of the available chunks linked list

public:
    explicit OrderMemoryPool(size_t capacity) 
        : m_capacity(capacity), m_storage(capacity * sizeof(Order) + 64) 
    {
        // Align the starting pointer to a 64-byte boundary within our vector
        void* raw_ptr = m_storage.data();
        size_t space = m_storage.size();
        void* aligned_ptr = std::align(64, capacity * sizeof(Order), raw_ptr, space);
        
        if (!aligned_ptr) {
            throw std::runtime_error("Failed to satisfy 64-byte memory alignment allocation.");
        }

        Order* current_pool = reinterpret_cast<Order*>(aligned_ptr);

        // Link all blocks together into a free-list chain using the 'next' pointer
        for (size_t i = 0; i < m_capacity - 1; ++i) {
            current_pool[i].next = &current_pool[i + 1];
        }
        current_pool[m_capacity - 1].next = nullptr; // Tail of the pool
        m_free_list = &current_pool[0];
    }

    // Allocate an order out of the pool: O(1) complexity
    template<typename... Args>
    Order* allocate(Args&&... args) {
        if (__builtin_expect((m_free_list == nullptr), 0)) { // Branch prediction hint: pool exhaustion is rare
            throw std::bad_alloc();
        }

        // Pop from the free list
        Order* chunk = m_free_list;
        m_free_list = m_free_list->next;

        // Placement new: initializes object in place without allocating new heap memory
        return ::new (static_cast<void*>(chunk)) Order{std::forward<Args>(args)...};
    }

    // Deallocate an order back to the pool: O(1) complexity
    void deallocate(Order* order) {
        if (!order) return;

        // Explicitly call destructor (if it had non-trivial properties, good practice)
        order->~Order();

        // Push back onto the free list
        order->next = m_free_list;
        order->prev = nullptr;
        m_free_list = order;
    }

    // Delete copy/move constructors to prevent accidental memory duplication
    OrderMemoryPool(const OrderMemoryPool&) = delete;
    OrderMemoryPool& operator=(const OrderMemoryPool&) = delete;
};