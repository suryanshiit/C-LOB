#include "include/OrderBookDefs.hpp"
#include <iostream>
#include <cassert>

void test_cache_alignment() {
    std::cout << "[Test] Verifying Core Structures Alignment...\n";
    // Ensure the structure size is exactly aligned to cache line multiples
    assert(alignof(Order) == 64);
    assert(sizeof(Order) == 64);
    std::cout << "  -> Success: Order is optimally cache-aligned.\n";
}

void test_pool_allocation_and_deallocation() {
    std::cout << "[Test] Verifying O(1) Memory Pool Performance Mechanics...\n";

    const size_t pool_size = 3;
    OrderMemoryPool pool(pool_size);

    // 1. Allocate up to capacity
    Order* o1 = pool.allocate(1ULL, static_cast<uint32_t>(10050), static_cast<uint32_t>(100), Side::Buy);
    Order* o2 = pool.allocate(2ULL, static_cast<uint32_t> (10055), static_cast<uint32_t>(200), Side::Sell);
    Order* o3 = pool.allocate(3ULL, static_cast<uint32_t> (10060), static_cast<uint32_t> (150), Side::Buy);

    assert(o1->order_id == 1 && o1->price == 10050 && o1->side == Side::Buy);
    assert(o2->order_id == 2 && o2->price == 10055 && o2->side == Side::Sell);
    assert(o3->order_id == 3 && o3->price == 10060 && o3->side == Side::Buy);

    // Check pointer integrity (should be contiguous arrays shifted by sizeof(Order))
    assert(reinterpret_cast<uintptr_t>(o1) % 64 == 0);
    assert(reinterpret_cast<uintptr_t>(o2) % 64 == 0);

    // 2. Test Pool Exhaustion
    bool caught_bad_alloc = false;
    try {
        pool.allocate(4ULL, uint32_t{10000}, uint32_t{10}, Side::Buy);
    } catch (const std::bad_alloc&) {
        caught_bad_alloc = true;
    }
    assert(caught_bad_alloc && "Pool failed to throw bad_alloc on exhaustion.");

    // 3. Deallocate and Re-allocate to confirm recyclability
    pool.deallocate(o2);

    // This allocation should succeed now because we freed o2's block slot
    Order* o4 = pool.allocate(4ULL, uint32_t{10090}, uint32_t{500}, Side::Buy);
    assert(o4->order_id == 4);
    assert(o4 == o2 && "Memory Pool did not recycle the most recently freed pointer slot!");

    std::cout << "  -> Success: Allocator correctly handles life cycles and recycling.\n";
}

void test_intrusive_linking() {
    std::cout << "[Test] Checking Intrusive Queue Operations Pointer Integrity...\n";
    OrderMemoryPool pool(5);

    Order* o1 = pool.allocate(1ULL, uint32_t{100}, uint32_t{10}, Side::Buy);
    Order* o2 = pool.allocate(2ULL, uint32_t{100}, uint32_t{20}, Side::Buy);
    Order* o3 = pool.allocate(3ULL, uint32_t{100}, uint32_t{30}, Side::Buy);

    // Manually link them to mimic an intrusive FIFO priority queue
    o1->next = o2; o2->prev = o1;
    o2->next = o3; o3->prev = o2;

    // Traverse Forward
    Order* curr = o1;
    uint32_t total_qty = 0;
    while(curr) {
        total_qty += curr->quantity;
        curr = curr->next;
    }
    assert(total_qty == 60);

    // Break/Extract o2 from the queue (Simulating an Order Cancel)
    // Intrusive structure allows us to un-link in O(1) without iterating through the list
    o2->prev->next = o2->next;
    o2->next->prev = o2->prev;

    // Verify o1 skips straight to o3 now
    assert(o1->next == o3);
    assert(o3->prev == o1);

    std::cout << "  -> Success: Intrusive pointers safely isolated and manipulated.\n";
}

int main() {
    std::cout << "=== Starting LOB Engine Phase 1 Test Execution Suite ===\n\n";

    test_cache_alignment();
    test_pool_allocation_and_deallocation();
    test_intrusive_linking();

    std::cout << "\n=== All Tests Passed Successfully! System Architecture Intact. ===\n";
    return 0;
}