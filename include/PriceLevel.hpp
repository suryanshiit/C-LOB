#pragma once
#include "OrderBookDefs.hpp" // Contains Phase 1 Order struct

struct alignas(64) PriceLevel {
    Order* head{nullptr};
    Order* tail{nullptr};
    uint32_t volume{0};

    // O(1) Append to the end of the queue (Time-Priority)
    void append(Order* order) {
        order->next = nullptr;
        if (!tail) {
            head = tail = order;
            order->prev = nullptr;
        } else {
            tail->next = order;
            order->prev = tail;
            tail = order;
        }
        volume += order->quantity;
    }

    // O(1) Removal from anywhere in the queue (For Cancels or Full Fills)
    void remove(Order* order) {
        if (order->prev) {
            order->prev->next = order->next;
        } else {
            head = order->next; // Order was at the front
        }

        if (order->next) {
            order->next->prev = order->prev;
        } else {
            tail = order->prev; // Order was at the back
        }

        volume -= order->quantity;
        
        // Clear pointers to prevent dangling references
        order->prev = nullptr;
        order->next = nullptr;
    }

    [[nodiscard]] bool is_empty() const {
        return head == nullptr;
    }
};